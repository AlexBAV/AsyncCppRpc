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
		enum class captured_on
		{
			send,
			receive,
			stop,
		};

		template<class State>
		struct with_serializer_state
		{
			struct is_with_serializer_state;
			using type = State;
		};

		template<class Trait>
		concept is_with_serializer = requires
		{
			typename Trait::is_with_serializer_state;
		};

		template<class Trait>
		using is_with_serializer_t = std::bool_constant<is_with_serializer<Trait>>;

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

		template<class TraitsList>
		consteval auto get_serializer_state_helper()
		{
			using State = mp11::mp_filter<is_with_serializer_t, TraitsList>;
			static_assert(mp11::mp_size<State>::value < 2, "You can either specify a single `with_serializer_state` trait, or none at all");
			if constexpr (mp11::mp_size<State>::value == 1)
				return wrapper<typename mp11::mp_front<State>::type>{};
			else
				return wrapper<empty_serializer_state>{};
		}

		template<class TraitsList>
		using get_serializer_state = typename decltype(get_serializer_state_helper<TraitsList>())::type;

		template<class Connection>
		struct resolver
		{
			template<class Q>
			using fn = mp11::mp_invoke_q<Q, Connection>;
		};

		template<class TraitsList>
		using MarshalHelpersOnly = mp11::mp_filter<is_marshaller_t, TraitsList>;

		template<class MarshalHelpers, class Connection>
		using ToMarshallers = mp11::mp_transform_q<resolver<Connection>, MarshalHelpers>;

		template<class List>
		using Inherit = mp11::mp_apply<mp11::mp_inherit, List>;

		template<concepts::transport Transport, class... Traits>
		class connection : 
			public Inherit<ToMarshallers<MarshalHelpersOnly<mp11::mp_list<Traits...>>, connection<Transport, Traits...>>>,
			public get_serializer_state<mp11::mp_list<Traits...>>
		{
			// Validate passed marshaller types
			using Marshallers = ToMarshallers<MarshalHelpersOnly<mp11::mp_list<Traits...>>, connection>;
			using serializer_state = get_serializer_state<mp11::mp_list<Traits...>>;

			static_assert(mp11::mp_size<Marshallers>::value == 1 || mp11::mp_size<Marshallers>::value == 2, "Supported configurations: client-only, server-only or both client and server");
			static constexpr const auto clients_count = mp11::mp_count_if<Marshallers, validation::is_client>::value;
			static constexpr const auto servers_count = mp11::mp_count_if<Marshallers, validation::is_server>::value;

			static_assert(1 >= clients_count,
				"Error: two or more client marshallers specified.");
			static_assert(1 >= servers_count,
				"Error: two or more server marshallers specified.");

			static constexpr const auto has_server = servers_count != 0;
			static constexpr const bool reader_not_required = !has_server && has_only_void_methods<mp11::mp_first<Marshallers>>();
			static constexpr const bool writer_not_required = clients_count == 0 && has_only_void_methods<mp11::mp_first<Marshallers>>();

			corsl::cancellation_source cancel;

			std::optional<Transport> transport;

			corsl::async_queue<message_t> write_queue;
			std::map<uint32_t, corsl::promise<payload_t> *> completions;
			mutable corsl::srwlock completions_lock, stop_lock;
			corsl::future<> reader_task, writer_task;
			
			// error handling
			using error_handler_t = std::move_only_function<void(HRESULT, captured_on) const>;
			struct error_info
			{
				HRESULT hr;
				captured_on state;
			};
			error_handler_t error_handler{};
			std::optional<error_info> last_error;

			std::atomic<uint32_t> last_call_id{};
			corsl::mutex error_lock;
			bool running{};

			//
			void error_on_background(HRESULT hr, captured_on on)
			{
				std::scoped_lock l{ error_lock };
				if (auto err = std::exchange(error_handler, {}))
					error_on_background_(std::move(err), hr, on);
				else
					last_error = { hr,on };
			}

			static corsl::fire_and_forget error_on_background_(error_handler_t on_error, HRESULT hr, captured_on on)
			{
				if (on_error)
				{
					co_await corsl::resume_background();
					on_error(hr, on);
				}
			}

			corsl::future<> writer()
			{
				if constexpr (writer_not_required)
					co_return;
				else
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
							error_on_background(e.code(), captured_on::send);
							break;
						}
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

					std::atomic<std::int64_t> outstanding_requests{1};
					corsl::promise<> finished;

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
												Reader{ message.payload, get_serializer_state() } >> code;
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
								execute_request(std::move(message), outstanding_requests, finished);
							}
						}
						catch (const corsl::hresult_error &e)
						{
							cancel.cancel();
							error_on_background(e.code(), captured_on::receive);
							break;
						}
					}

					// We must ensure that reader_task exits only when all outstanding requests are completed (or cancelled)

					if (1 == outstanding_requests.fetch_sub(1, std::memory_order_relaxed))
						finished.set();

					// We cannot co_await now, cancellation could have already been requested
					finished.get_future().wait();
				}
			}

			corsl::future<> execute_request(message_t message, std::atomic<std::int64_t> &outstanding_requests, corsl::promise<> &finished)
			{
				HRESULT hr{};
				bool pending{};
				if constexpr (has_server)
				{
					try
					{
						if (message.type == call_type::void_request)
							this->void_dispatch(message.id, std::move(message.payload));
						else
						{
							pending = true;
							outstanding_requests.fetch_add(1, std::memory_order_release);

							corsl::cancellation_token token{ co_await cancel };
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

				if (!cancel.is_cancelled())
				{
					// Error occurred, return error code to the caller, unless this is a void call
					if (message.type != call_type::void_request)
					{
						message.payload.resize(sizeof(HRESULT));
						memcpy(message.payload.data(), &hr, sizeof(HRESULT));
						write_queue.push(message_t{ message_header{message.call_id, call_type::response_error, message.id}, std::move(message.payload) });
					}
				}

				if (pending)
				{
					if (1 == outstanding_requests.fetch_sub(1, std::memory_order_relaxed))
						finished.set();
				}
				co_return;
			}

		public:
			template<class...Args>
			connection(Args &&...args) :
				serializer_state{ std::forward<Args>(args)... }
			{}

			template<class... Args>
			connection(Transport &&transport, Args &&...args) requires !has_server :
				serializer_state{ std::forward<Args>(args)... }
			{
				start(std::move(transport));
			}

			~connection()
			{
				stop();
			}

			auto &get_serializer_state() noexcept
			{
				return static_cast<serializer_state &>(*this);
			}

			auto &get_transport() noexcept
			{
				return transport;
			}

			const corsl::cancellation_source& get_cancellation_token() const
			{
				return cancel;
			}

			template<class F>
			void on_error(F &&f) noexcept
			{
				std::scoped_lock l{ error_lock };
				if (auto err = std::exchange(last_error, {}))
				{
					// the error has already been registered, call the handler immediately
					f(err->hr, err->state);
				}
				else
					error_handler = std::move(f);
			}

			void on_error() noexcept
			{
				std::scoped_lock l{ error_lock };
				error_handler = {};
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
				if (std::scoped_lock l{ stop_lock }; running)
				{
					error_on_background(E_ABORT, captured_on::stop);
					cancel.cancel();	// this will also prevent all future calls from being made through this connection
					corsl::block_wait(corsl::when_all(std::move(writer_task), std::move(reader_task)));
					{
						std::scoped_lock l2{ completions_lock };
						for (auto &p : completions)
							p.second->set_exception_async(std::make_exception_ptr(corsl::operation_cancelled{}));
						completions.clear();
					}
					transport.reset();
					cancel = {};
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

				if (cancel.is_cancelled())
					throw corsl::operation_cancelled{};

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

	using details::captured_on;
	using details::connection;
	using details::with_serializer_state;
}
