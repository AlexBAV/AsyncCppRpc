//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/transport.h"

namespace crpc
{
	namespace details::copydata
	{
		struct copydata_message_header : message_header
		{
			uint32_t payload_size;
		};

		constexpr const auto CALL_ID = fnv::fnv_hash("AsyncCppRpc-CopyData-Transport"sv);

		class copydata_transport
		{
			corsl::cancellation_source cancel;
			HWND other_party, this_party;
			bool sync_write;
			corsl::async_queue<message_t> input_queue;

		public:
			copydata_transport() = default;

			copydata_transport(HWND other_party, HWND this_party = {}, bool sync_write = false) noexcept :
				other_party{ other_party },
				this_party{ this_party },
				sync_write{ sync_write }
			{}

			copydata_transport(copydata_transport &&o) noexcept :
				cancel{ std::move(o.cancel) },
				other_party{ o.other_party },
				this_party{ o.this_party },
				sync_write{ o.sync_write }
			{}

			void initialize(HWND other_party, HWND this_party = {}, bool sync_write = false) noexcept
			{
				this->other_party = other_party;
				this->this_party = this_party;
				this->sync_write = sync_write;
			}

			void set_cancellation_token(const corsl::cancellation_source &cancel_)
			{
				cancel = cancel_.create_connected_source();
			}

			corsl::future<message_t> read()
			{
				corsl::cancellation_token token{ co_await cancel };
				corsl::cancellation_subscription s{ token, [this]
					{
						input_queue.cancel();
					} };

				co_return co_await input_queue.next();
			}

			corsl::future<> write(message_t message)
			{
				copydata_message_header header{ message };
				header.payload_size = static_cast<uint32_t>(message.payload.size());

				std::vector<std::byte> send_buffer;
				send_buffer.resize(sizeof(copydata_message_header) + message.payload.size());
				memcpy(send_buffer.data(), &header, sizeof(header));
				memcpy(send_buffer.data() + sizeof(header), message.payload.data(), message.payload.size());

				COPYDATASTRUCT cs
				{
					.dwData = CALL_ID,
					.cbData = static_cast<DWORD>(send_buffer.size()),
					.lpData = const_cast<void *>(static_cast<const void *>(send_buffer.data()))
				};

				if (sync_write)
					::SendMessageW(other_party, WM_COPYDATA, reinterpret_cast<WPARAM>(this_party), reinterpret_cast<LPARAM>(&cs));
				else
				{
					corsl::promise<> result;

					::SendMessageCallbackW(other_party, WM_COPYDATA, reinterpret_cast<WPARAM>(this_party), reinterpret_cast<LPARAM>(&cs),
						[]([[maybe_unused]] HWND w, [[maybe_unused]] UINT message, ULONG_PTR context, [[maybe_unused]] LRESULT result)
						{
							auto *promise = reinterpret_cast<corsl::promise<> *>(context);
							promise->set();
						}, reinterpret_cast<ULONG_PTR>(&result));
					co_await result.get_future();
				}
			}

			bool on_copydata(const MSG &msg) noexcept
			{
				if (msg.message == WM_COPYDATA)
					return on_copydata(reinterpret_cast<HWND>(msg.wParam), *reinterpret_cast<COPYDATASTRUCT *>(msg.lParam));
				else
					return false;
			}

			bool on_copydata([[maybe_unused]] HWND caller, const COPYDATASTRUCT &cs) noexcept
			{
				if (cs.dwData == CALL_ID)
				{
					assert(!other_party || caller == other_party);

					if (cs.cbData >= sizeof(copydata_message_header))
					{
						auto *header = reinterpret_cast<copydata_message_header *>(cs.lpData);
						if (header->payload_size + sizeof(copydata_message_header) == cs.cbData)
						{
							auto begin = reinterpret_cast<std::byte *>(header + 1);
							input_queue.push(message_t{ *header, { begin, begin + header->payload_size } });
							return true;
						}
					}
				}
				return false;
			}
		};

		static_assert(concepts::transport<copydata_transport>);
	}

	namespace transports::copydata
	{
		using details::copydata::copydata_transport;
	}
}
