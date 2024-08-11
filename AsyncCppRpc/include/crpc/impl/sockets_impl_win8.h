//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "sockets.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "windowsapp")

namespace crpc::sockets::win8
{
	namespace impl
	{
		constexpr const uint32_t max_read_buffer_size = 65536;

		namespace net = winrt::Windows::Networking;
		namespace streams = winrt::Windows::Storage::Streams;

		inline constexpr int to_integer(std::wstring_view text)
		{
			int result{};
			for (auto c : text)
			{
				if ('0' <= c && c <= '9')
					result = result * 10 + c - '0';
				else
					break;
			}
			return result;
		}

		class TcpSocket : public ITcpSocket
		{
			winrt::Windows::Networking::Sockets::StreamSocket socket;
			streams::IOutputStream output_stream{ socket.OutputStream() };
			streams::IInputStream input_stream{ socket.InputStream() };

		public:
			TcpSocket()
			{
				socket.Control().KeepAlive(true);
			}

			TcpSocket(winrt::Windows::Networking::Sockets::StreamSocket &&socket) noexcept :
				socket{ socket }
			{
			}

			virtual corsl::future<void> connect(std::wstring host, int port) override
			{
				using namespace winrt::Windows::Networking::Sockets;
				try
				{
					co_await socket.ConnectAsync(
						net::HostName{ host },
						winrt::hstring(std::to_wstring(port)));
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

			virtual void close() override
			{
				socket.Close();
			}

			virtual corsl::future<uint32_t> send(winrt::array_view<const uint8_t> data) override
			{
				try
				{
					co_return co_await output_stream.WriteAsync(winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(data));
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

			virtual corsl::future<std::vector<uint8_t>> receive(const corsl::cancellation_source &csource) override
			{
				corsl::cancellation_token token{ co_await csource };
				try
				{
					streams::Buffer buffer{ max_read_buffer_size };
					auto read_op = input_stream.ReadAsync(buffer, max_read_buffer_size, streams::InputStreamOptions::Partial);
					corsl::cancellation_subscription sub{ token,[&]
					{
						read_op.Cancel();
					} };

					auto result = co_await read_op;

					winrt::com_array<uint8_t> array;
					winrt::Windows::Security::Cryptography::CryptographicBuffer::CopyToByteArray(result, array);
					co_return std::vector<uint8_t>{ array.begin(), array.end() };
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}
		};

		class TcpSocketListener : public ITcpSocketListener
		{
			winrt::Windows::Networking::Sockets::StreamSocketListener listener;
			corsl::async_queue<std::unique_ptr<ITcpSocket>> clients;
			winrt::event_token token;

		public:
			TcpSocketListener()
			{
				listener.Control().KeepAlive(true);
				token = listener.ConnectionReceived([this](const auto &, const auto &args)
				{
					clients.push(std::make_unique<TcpSocket>(args.Socket()));
				});
			}

			~TcpSocketListener()
			{
				listener.ConnectionReceived(token);
			}

			virtual int get_port() const override
			{
				return to_integer(listener.Information().LocalPort());
			}

			virtual corsl::future<void> bind(const std::wstring &host, int port) override
			{
				using namespace winrt::Windows::Networking::Sockets;
				try
				{
					co_await listener.BindEndpointAsync(net::HostName{ host }, winrt::hstring(std::to_wstring(port)));
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

			virtual corsl::future<int> bind(const std::wstring &host) override
			{
				using namespace winrt::Windows::Networking::Sockets;
				try
				{
					co_await listener.BindEndpointAsync(net::HostName{ host }, winrt::hstring{});
					co_return to_integer(listener.Information().LocalPort());
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

#if defined(WINRT_Windows_Networking_Connectivity_H)
			virtual corsl::future<void> bind(const winrt::Windows::Networking::Connectivity::NetworkAdapter &adapter, int port) override
			{
				try
				{
					co_await listener.BindServiceNameAsync(winrt::hstring(std::to_wstring(port)), net::Sockets::SocketProtectionLevel::PlainSocket, adapter);
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}
#endif

			virtual corsl::future<void> bind(int port) override
			{
				using namespace winrt::Windows::Networking::Sockets;
				try
				{
					co_await listener.BindServiceNameAsync(winrt::hstring(std::to_wstring(port)));
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

			virtual corsl::future<int> bind() override
			{
				using namespace winrt::Windows::Networking::Sockets;
				try
				{
					co_await listener.BindServiceNameAsync(winrt::hstring{});
					co_return to_integer(listener.Information().LocalPort());
				}
				catch (const winrt::hresult_error &er)
				{
					corsl::throw_error(er.code());
				}
			}

			virtual corsl::future<std::unique_ptr<ITcpSocket>> listen(const corsl::cancellation_source &csource) override
			{
				corsl::cancellation_token token_{ co_await csource };
				corsl::cancellation_subscription sub{ token_,[&]
				{
					listener.CancelIOAsync();
					clients.cancel();
				} };
				co_return co_await clients.next();
			}
		};
	}

	using impl::TcpSocket;
	using impl::TcpSocketListener;
}
