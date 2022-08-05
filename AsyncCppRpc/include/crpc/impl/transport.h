//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"
#include "method_id.h"

namespace crpc
{
	namespace details
	{
		enum class call_type : uint32_t
		{
			request,
			void_request,
			response,
			response_error,
		};

		struct message_header
		{
			uint32_t call_id : 30;
			call_type type : 2;
			method_id id;
		};

		struct message_t : message_header
		{
			payload_t payload;

			message_t() = default;
			message_t(const message_header &header, payload_t &&payload) noexcept :
				message_header{ header },
				payload(std::move(payload))
			{
			}
		};

		namespace concepts
		{
			template<class T>
			concept transport = std::is_default_constructible_v<T> && requires(T & v, const T & cv, const corsl::cancellation_source & cancel, message_t message)
			{
				{ v.set_cancellation_token(cancel) } -> std::same_as<void>;
				{ cv.get_cancellation_token() } -> std::same_as<const corsl::cancellation_source &>;
				{ v.read() } -> std::same_as<corsl::future<message_t>>;
				{ v.write(message) } -> std::same_as<corsl::future<>>;
			};
		}

		struct __declspec(novtable) dynamic_transport_base
		{
			virtual ~dynamic_transport_base() = default;
			virtual void set_cancellation_token(const corsl::cancellation_source &src) = 0;
			virtual const corsl::cancellation_source &get_cancellation_token() const = 0;
			virtual corsl::future<message_t> read() = 0;	// propagates HRESULT exception on error
			virtual corsl::future<> write(message_t message) = 0;	// propagates HRESULT exception on error
		};

		struct dynamic_transport
		{
			std::shared_ptr<dynamic_transport_base> impl;

			dynamic_transport() = default;
			dynamic_transport(std::shared_ptr<dynamic_transport_base> impl) noexcept :
				impl{ std::move(impl) }
			{}

			void set_cancellation_token(const corsl::cancellation_source &src)
			{
				impl->set_cancellation_token(src);
			}

			const corsl::cancellation_source &get_cancellation_token() const
			{
				return impl->get_cancellation_token();
			}

			corsl::future<message_t> read()
			{
				return impl->read();
			}

			corsl::future<> write(message_t message)
			{
				return impl->write(std::move(message));
			}
		};

		template<concepts::transport T>
		class dynamic_transport_impl : public dynamic_transport_base, public T
		{
			T *getT()
			{
				return static_cast<T *>(this);
			}

			const T *getT() const
			{
				return static_cast<const T *>(this);
			}

			virtual void set_cancellation_token(const corsl::cancellation_source &src) override
			{
				getT()->set_cancellation_token(src);
			}

			virtual const corsl::cancellation_source &get_cancellation_token() const override
			{
				return getT()->get_cancellation_token();
			}

			virtual corsl::future<message_t> read() override
			{
				return getT()->read();
			}

			virtual corsl::future<> write(message_t message) override
			{
				return getT()->write(std::move(message));
			}
		public:
			using T::T;
		};
	}
	using details::concepts::transport;
	using details::dynamic_transport;
	using details::dynamic_transport_impl;
}
