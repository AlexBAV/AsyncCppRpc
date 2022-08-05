//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include <corsl/future.h>
#include <corsl/cancel.h>
#include <winrt/base.h>

namespace crpc::sockets
{
	struct ITcpSocket
	{
		virtual ~ITcpSocket() = default;
		virtual corsl::future<void> connect(std::wstring host, int port) = 0;

		virtual corsl::future<uint32_t> send(winrt::array_view<const uint8_t> data) = 0;
		virtual corsl::future<std::vector<uint8_t>> receive(const corsl::cancellation_source &csource) = 0;

		virtual void close() = 0;
	};

	struct ITcpSocketListener
	{
		virtual ~ITcpSocketListener() = default;
		virtual corsl::future<void> bind(const std::wstring &host, int port) = 0;
		virtual corsl::future<void> bind(int port) = 0;
		virtual corsl::future<std::shared_ptr<ITcpSocket>> listen(const corsl::cancellation_source &csource) = 0;
	};
}
