//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/dependencies.h"
#include "impl/transport.h"

#include <wil/resource.h>

namespace crpc
{
	namespace details::pipe
	{
		struct pipe_message_header : message_header
		{
			uint32_t payload_size;
		};

		constexpr const size_t MaxSupportedRead = 65536;

		inline auto prepare_handle(const wil::unique_handle &pipe) noexcept
		{
			SetFileCompletionNotificationModes(pipe.get(), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
			return corsl::resumable_io{ pipe.get() };
		}

		class pipe_transport
		{
			wil::unique_handle pipe;
			corsl::resumable_io pipe_io;
			corsl::cancellation_source cancel;
			bool bServer;

			//

			auto write(std::span<const std::byte> data)
			{
				assert(data.size() <= MaxSupportedRead && "due to bug in Win8 or later, read may actually read more than 64K, but will only report 64K");
				return pipe_io.start_pending([=](OVERLAPPED& o)
					{
						if (::WriteFile(pipe.get(), data.data(), static_cast<DWORD>(data.size()), nullptr, &o))
							return false;
						else
						{
							auto err = GetLastError();
							if (err == ERROR_IO_PENDING)
								return true;
							else
								corsl::throw_win32_error(err);
						}
					});
			}

			auto read(std::span<std::byte> data)
			{
				assert(data.size() <= MaxSupportedRead && "due to bug in Win8 or later, read may actually read more than 64K, but will only report 64K");
				return pipe_io.start_pending([=](OVERLAPPED& o)
					{
						if (::ReadFile(pipe.get(), data.data(), static_cast<DWORD>(data.size()), nullptr, &o))
							return false;
						else
						{
							auto err = GetLastError();
							if (err == ERROR_IO_PENDING)
								return true;
							else
								corsl::throw_win32_error(err);
						}
					});
			}

		public:
			pipe_transport() = default;
			pipe_transport(wil::unique_handle pipe, bool server) noexcept :
				pipe{ std::move(pipe) },
				pipe_io{ prepare_handle(this->pipe) },
				bServer{ server }
			{
			}

			pipe_transport(wil::unique_handle pipe, corsl::resumable_io pipe_io, bool server) noexcept :
				pipe{ std::move(pipe) },
				pipe_io{ std::move(pipe_io) },
				bServer{ server }
			{
			}

			void set_cancellation_token(const corsl::cancellation_source& src)
			{
				cancel = src.create_connected_source();
			}

			const corsl::cancellation_source& get_cancellation_token() const
			{
				return cancel;
			}

			// Read & Write
			corsl::future<> write(message_t message)
			{
				if (!pipe)
					corsl::throw_error(E_FAIL);

				pipe_message_header pmh{ message };
				pmh.payload_size = static_cast<uint32_t>(message.payload.size());

				co_await write(as_bytes(std::span{ &pmh, 1 }));

				auto *data = message.payload.data();
				auto size = message.payload.size();
				while (size)
				{
					auto towrite = std::min(size, MaxSupportedRead);
					co_await write(std::span{ data,towrite });
					data += towrite;
					size -= towrite;
				}
			}

			corsl::future<message_t> read()
			{
				if (!pipe)
					corsl::throw_error(E_FAIL);

				pipe_message_header pmh;
				if (sizeof(pmh) != co_await read(as_writable_bytes(std::span{ &pmh,1 })))
					corsl::throw_error(E_INVALIDARG);

				auto size = pmh.payload_size;
				payload_t payload(size);
				auto* data = payload.data();

				while (size)
				{
					const auto toread = std::min(static_cast<uint32_t>(MaxSupportedRead), size);
					const auto read_bytes = co_await read(std::span{ data,toread });
					data += read_bytes;
					size -= read_bytes;
				}
				co_return message_t{ pmh,std::move(payload) };
			}

			void close_connection()
			{
				if (bServer)
				{
					if (pipe)
					{
						FlushFileBuffers(pipe.get());
						DisconnectNamedPipe(pipe.get());
					}
				}
				else
					pipe.reset();
			}

			void impersonate()
			{
				corsl::check_win32_api(ImpersonateNamedPipeClient(pipe.get()));
			}

			void revert_to_self()
			{
				corsl::check_win32_api(RevertToSelf());
			}

			//CToken get_client_token(DWORD DesiredAccess, bool impersonate = false, bool openasself = false) const
			//{
			//	CToken result;
			//	result.OpenNamedPipeClientToken(pipe.get(), DesiredAccess, impersonate, openasself);
			//	return result;
			//}
		};
	}

	namespace transports::pipe
	{
		using details::pipe::pipe_transport;

		inline pipe_transport create_client(std::wstring_view server, std::wstring_view name, winrt::Windows::Foundation::TimeSpan Timeout, unsigned Retries)
		{
			using namespace std::literals;
			const auto path = L"\\\\"s + std::wstring{ server } + L"\\pipe\\"s + std::wstring{ name };

			while (Retries--)
			{
				wil::unique_handle pipe{ ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr) };
				if (!pipe)
				{
					auto err = GetLastError();
					if (err == ERROR_FILE_NOT_FOUND)
					{
						auto res = WaitNamedPipeW(path.c_str(), static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(Timeout).count()));
						if (!res)
						{
							auto error = GetLastError();
							if (error != ERROR_SEM_TIMEOUT)
								corsl::throw_win32_error(error);
						}
					}
					else
						corsl::throw_win32_error(err);
				}
				else
				{
					DWORD mode = PIPE_READMODE_BYTE;
					corsl::check_win32_api(SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr));
					return { std::move(pipe), false };
				}
			}
			corsl::throw_win32_error(ERROR_TIMEOUT);
		}

		template<class F = std::identity>
		struct create_server_params
		{
			const SECURITY_DESCRIPTOR *sd{};
			uint32_t def_buffer_size{ 4096 * 4096 };
			uint32_t out_buffer_size{ def_buffer_size };
			uint32_t in_buffer_size{ def_buffer_size };
			uint32_t default_timeout{};
			F on_after_wait_pending{};
			bool local_only{ false };
		};

		template<class F = std::identity>
		inline corsl::future<pipe_transport> create_server(std::wstring_view name, const corsl::cancellation_source &cancel, const create_server_params<F> &params = {})
		{
			corsl::cancellation_token token{ co_await cancel };

			SECURITY_ATTRIBUTES sa{
				sizeof(sa),
				const_cast<void *>(static_cast<const void *>(params.sd)),
				false
			};
			auto* psa = params.sd ? &sa : nullptr;
			wil::unique_handle pipe{ CreateNamedPipeW((L"\\\\.\\pipe\\"s + std::wstring{ name }).c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | (params.local_only ? 0 : PIPE_ACCEPT_REMOTE_CLIENTS),
				PIPE_UNLIMITED_INSTANCES, params.out_buffer_size, params.in_buffer_size, params.default_timeout, psa) };

			if (!pipe)
				corsl::throw_last_error();

			auto io = details::pipe::prepare_handle(pipe);

			std::atomic<OVERLAPPED*> pover{ nullptr };
			corsl::cancellation_subscription subscription{ token,[h = pipe.get(), &pover]
			{
				auto * p = pover.load(std::memory_order_relaxed);
				if (p)
					CancelIoEx(h, p);
			} };
			co_await io.start_pending([&](OVERLAPPED& o)
				{
					pover.store(&o, std::memory_order_relaxed);
					auto res = ConnectNamedPipe(pipe.get(), &o);
					if (!res)
					{
						auto error = GetLastError();
						if (error == ERROR_IO_PENDING)
						{
							params.on_after_wait_pending();
							return true;
						}
						else if (error == ERROR_PIPE_CONNECTED)
							return false;	// operation completed successfully
						else
							corsl::throw_last_error();
					}
					else
						return false;
				});

			co_return pipe_transport{ std::move(pipe),std::move(io),true };
		}
	}
}
