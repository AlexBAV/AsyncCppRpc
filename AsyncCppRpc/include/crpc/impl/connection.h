//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include "marshal.h"
#include "transport.h"

namespace crpc
{
	namespace details
	{
		namespace validation
		{
			template<class M>
			concept client_marshaller = requires
			{
				typename M::is_client_marshaller;
			};

			template<class M>
			concept server_marshaller = requires
			{
				typename M::is_server_marshaller;
			};

			template<class T>
			using is_client = std::bool_constant<client_marshaller<T>>;

			template<class T>
			using is_server = std::bool_constant<server_marshaller<T>>;
		}

		template<class Marshaller>
		consteval bool has_only_void_methods()
		{
			return Marshaller::only_void_methods;
		}

		template<class M, class D>
		using resolver = mp11::mp_invoke_q<M, D>;

		template<concepts::transport Transport, class... MarshalHelpers>
		class connection : public resolver<MarshalHelpers, connection<Transport, MarshalHelpers...>>...
		{
			// Validate passed marshaller types
			using Marshallers = mp11::mp_list<resolver<MarshalHelpers, connection>...>;
			static_assert(mp11::mp_size<Marshallers>::value == 1 || mp11::mp_size<Marshallers>::value == 2, "Supported configurations: client-only, server-only or both client and server");
			static constexpr const auto clients_count = mp11::mp_count_if<Marshallers, validation::is_client>::value;
			static constexpr const auto servers_count = mp11::mp_count_if<Marshallers, validation::is_server>::value;

			static_assert(1 >= clients_count,
				"Error: two or more client marshallers specified.");
			static_assert(1 >= servers_count,
				"Error: two or more server marshallers specified.");

			static constexpr const auto has_server = servers_count != 0;

			static constexpr const bool reader_not_required = !has_server && has_only_void_methods<mp11::mp_first<Marshallers>>();

			corsl::cancellation_source cancel;

			std::optional<Transport> transport;

			corsl::async_queue<message_t> write_queue;
			std::map<uint32_t, corsl::promise<payload_t> *> completions;
			mutable corsl::srwlock completions_lock, stop_lock;
			corsl::future<> reader_task, writer_task;
			std::move_only_function<void(HRESULT) const> on_error_;
			std::atomic<uint32_t> last_call_id{};
			bool running{};

			//
			void error_on_background(HRESULT hr)
			{
				error_on_background_(std::move(on_error_), hr);
			}

			static corsl::fire_and_forget error_on_background_(std::move_only_function<void(HRESULT) const> on_error, HRESULT hr)
			{
				if (on_error)
				{
					co_await corsl::resume_background();
					on_error(hr);
				}
			}

			corsl::future<> writer()
			{
				corsl::cancellation_token token{ co_await cancel };
				corsl::cancellation_subscription s{ token, [&]
					{
						write_queue.cancel();
					} };

				while (!token.is_cancelled())
				{
					auto message = co_await write_queue.next();
					try
					{
						co_await transport->write(std::move(message));
					}
					catch (const corsl::hresult_error &e)
					{
						cancel.cancel();
						error_on_background(e.code());
						break;
					}
				}
			}

			corsl::future<> reader()
			{
				if constexpr (reader_not_required)
					co_return;
				else
				{
					corsl::cancellation_token token{ co_await cancel };
					// we will force the background thread in case data is already coming from the transport as this coroutine should return early
					co_await corsl::resume_background();

					while (!token.is_cancelled())
					{
						try
						{
							auto message = co_await transport->read();
							if (message.type == call_type::response || message.type == call_type::response_error)
							{
								// this is a reply to a message we sent
								std::scoped_lock l{ completions_lock };
								if (auto it = completions.find(message.call_id); it != completions.end())
								{
									auto promise = it->second;
									completions.erase(it);
									if (promise)
									{
										if (message.type == call_type::response_error) [[unlikely]]
										{
											if (message.payload.size() == sizeof(HRESULT))
											{
												HRESULT code;
												Reader{ message.payload } >> code;
												promise->set_exception_async(std::make_exception_ptr(corsl::hresult_error{ code }));
											}
											else
												promise->set_exception_async(std::make_exception_ptr(corsl::hresult_error{E_FAIL}));
										}
										else
											promise->set_async(std::move(message.payload));
									}
								}
							}
							else
							{
								// this is a request from a client to server
								execute_request(std::move(message));
							}
						}
						catch (const corsl::hresult_error &e)
						{
							cancel.cancel();
							error_on_background(e.code());
							break;
						}
					}
				}
			}

			corsl::future<> execute_request(message_t message)
			{
				HRESULT hr{};
				if constexpr (has_server)
				{
					try
					{
						if (message.type == call_type::void_request)
							this->void_dispatch(message.id, std::move(message.payload));
						else
						{
							auto result = co_await this->dispatch(message.id, std::move(message.payload));
							write_queue.push(message_t{ message_header{message.call_id, call_type::response, message.id }, std::move(result) });
						}
					}
					catch (const corsl::hresult_error &e)
					{
						hr = e.code();
					}
					catch (...)
					{
						hr = E_FAIL;
					}
				}
				else
				{
					hr = E_INVALIDARG;
				}
				// Error occurred, return error code to the caller, unless this is a void call
				if (message.type != call_type::void_request)
				{
					message.payload.resize(sizeof(HRESULT));
					memcpy(message.payload.data(), &hr, sizeof(HRESULT));
					write_queue.push(message_t{ message_header{message.call_id, call_type::response_error, message.id}, std::move(message.payload) });
				}
				co_return;
			}

		public:
			connection() = default;

			connection(Transport &&transport) requires !has_server
			{
				start(std::move(transport));
			}

			~connection()
			{
				stop();
			}

			const corsl::cancellation_source& get_cancellation_token() const
			{
				return cancel;
			}

			template<class F>
			void on_error(F &&f)
			{
				on_error_ = std::move(f);
			}

			void start(Transport &&transport_)
			{
				assert(!transport);
				transport.emplace(std::move(transport_));
				transport->set_cancellation_token(cancel);

				reader_task = reader();
				writer_task = writer();
				std::scoped_lock l{ stop_lock };
				running = true;
			}

			void stop()
			{
				std::scoped_lock l{ stop_lock };
				if (running)
				{
					cancel.cancel();
					corsl::block_wait(corsl::when_all(std::move(writer_task), std::move(reader_task)));
					transport = {};
					cancel = {};
					std::scoped_lock l2{ completions_lock };
					for (auto &p : completions)
						p.second->set_exception(std::make_exception_ptr(corsl::operation_cancelled{}));
					completions.clear();
					running = false;
				}
			}

			corsl::future<payload_t> do_call(method_id name, payload_t payload)
			{
				assert(transport);

				corsl::cancellation_token token{ co_await cancel };

				auto call_id = last_call_id.fetch_add(1, std::memory_order_relaxed);
				corsl::promise<payload_t> promise;
				{
					std::scoped_lock l{ completions_lock };
					completions.emplace(call_id, &promise);
				}
				write_queue.push(message_t{ message_header{call_id, call_type::request, name}, std::move(payload) });
				co_return co_await promise.get_future();
			}

			void do_void_call(method_id name, payload_t payload)
			{
				assert(transport);

				auto call_id = last_call_id.fetch_add(1);
				write_queue.push(message_t{ message_header{call_id, call_type::void_request, name}, std::move(payload) });
			}

			explicit operator bool() const noexcept
			{
				std::shared_lock l{ stop_lock };
				return running;
			}
		};
	}

	using details::connection;
}
