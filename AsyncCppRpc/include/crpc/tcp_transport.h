//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "impl/transport.h"
#include "impl/sockets_impl_win8.h"

namespace crpc
{
	namespace details::tcp
	{
		struct tcp_config
		{
			std::wstring address;
			uint16_t port;
		};

		struct tcp_message_header : message_header
		{
			uint32_t payload_size;
		};

		class tcp_transport
		{
			corsl::cancellation_source cancel;
			std::unique_ptr<sockets::ITcpSocket> socket;
			std::vector<uint8_t> receive_buffer;

		public:
			void set_cancellation_token(const corsl::cancellation_source &src)
			{
				cancel = src.create_connected_source();
			}

			const corsl::cancellation_source &get_cancellation_token() const
			{
				return cancel;
			}

			corsl::future<> read_next()
			{
				auto data = co_await socket->receive(cancel);
				if (data.empty())
					corsl::throw_win32_error(WSAECONNRESET);
				receive_buffer.insert(receive_buffer.end(), data.begin(), data.end());
			}

			corsl::future<message_t> read()
			{
				while (receive_buffer.size() < sizeof(tcp_message_header))
					co_await read_next();
				auto *m = reinterpret_cast<tcp_message_header *>(receive_buffer.data());
				const auto full_size = sizeof(tcp_message_header) + m->payload_size;
				while (receive_buffer.size() < full_size)
					co_await read_next();

				m = reinterpret_cast<tcp_message_header *>(receive_buffer.data());

				// TODO: implement payload_t cache allocation
				const auto *data = reinterpret_cast<const std::byte *>(m + 1);
				message_t result{ *m, {data, data + m->payload_size } };
				receive_buffer.erase(receive_buffer.begin(), receive_buffer.begin() + full_size);
				co_return std::move(result);
			}

			corsl::future<> write(message_t message)
			{
				tcp_message_header header{ message, static_cast<uint32_t>(message.payload.size()) };
				co_await socket->send({ reinterpret_cast<const uint8_t *>(&header), sizeof(header) });
				const auto *data = reinterpret_cast<const uint8_t *>(message.payload.data());
				co_await socket->send({ data, data + message.payload.size() });
			}

			corsl::future<> connect(const tcp_config &config)
			{
				socket = std::make_unique<sockets::win8::TcpSocket>();
				co_await socket->connect(config.address, static_cast<int>(config.port));
			}

			tcp_transport() = default;
			tcp_transport(std::unique_ptr<sockets::ITcpSocket> &&socket) noexcept :
				socket{ std::move(socket) }
			{}
		};

		class tcp_listener
		{
			std::unique_ptr<sockets::ITcpSocketListener> listener;

		public:
			corsl::future<> create_server(const tcp_config &config)
			{
				listener = std::make_unique<sockets::win8::TcpSocketListener>();
				if (config.address.empty())
					co_await listener->bind(static_cast<int>(config.port));
				else
					co_await listener->bind(config.address, static_cast<int>(config.port));
			}

			corsl::future<tcp_transport> wait_client(const corsl::cancellation_source &cancel)
			{
				co_return tcp_transport{ co_await listener->listen(cancel) };
			}
		};

		static_assert(concepts::transport<tcp_transport>);
	}

	namespace transports::tcp
	{
		using config_t = details::tcp::tcp_config;
		using details::tcp::tcp_transport;
		using details::tcp::tcp_listener;
	}
}
