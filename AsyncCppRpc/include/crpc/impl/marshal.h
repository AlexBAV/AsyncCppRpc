//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "serializer.h"
#include "method_id.h"

namespace crpc
{
	namespace details
	{
		template<typename T>
		inline constexpr bool dependent_false = false;

		template<class T>
		struct method;

		template<class T>
		struct is_string_view : std::false_type
		{};

		template<class E, class Traits>
		struct is_string_view<std::basic_string_view<E, Traits>> : std::true_type
		{};

		template<class T>
		struct is_span : std::false_type
		{};

		template<class T, size_t Extent>
		struct is_span<std::span<T, Extent>> : std::true_type
		{};

		template<class T>
		struct wrapper
		{
			using type = T;
		};

		template<class T>
		inline consteval auto get_storage_type()
		{
			if constexpr (is_span<T>::value)
				return wrapper<std::vector<T>>{};
			else if constexpr (is_string_view<T>::value)
				return wrapper<std::basic_string<typename T::value_type>>{};
			else
				return wrapper<T>{};
		}

		template<class T>
		using to_storage_type = typename decltype(get_storage_type<T>())::type;

		template<class R, class...Args>
		struct method<R(Args...)> : std::move_only_function<R(Args...)>
		{
			struct is_method_test;

			using result_type = R;
			using stored_args_t = mp11::mp_transform<to_storage_type, std::tuple<std::decay_t<Args>...>>;
			static constexpr const size_t args_count = sizeof...(Args);

			using std::move_only_function<R(Args...)>::move_only_function;
		};

		template<class T>
		concept method_descriptor = requires
		{
			typename T::is_method_test;
		};

		template<class MD>
		inline consteval method_id get_method_id() noexcept
		{
			return { fnv::fnv_hash(std::string_view{MD::name}) };
		}

		template<class R>
		concept future = corsl::is_future_v<R>;

		template<class R>
		concept valid_return = std::same_as<void, R> || future<R>;

		template<class MD>
		using is_void_method = std::bool_constant<std::same_as<void, typename MD::result_type>>;

		template<class Derived, class Interface>
		class marshal_client : public Interface
		{
			static_assert(boost::describe::has_describe_members<Interface>::value, "Interface type must be described using BOOST_DESCRIBE_STRUCT");

			auto &get_state() noexcept
			{
				return static_cast<Derived *>(this)->get_serializer_state();
			}

			template<class R>
			inline R unmarshal(std::span<const std::byte> data)
			{
				R result;
				Reader{ data, get_state() } >> result;
				return result;
			}

			template<method_descriptor M>
			auto build_call_member(method_id name)
			{
				using FR = M::result_type;
				static_assert(valid_return<FR>, "Interface method return type must be a future or void");
				constexpr bool is_void = std::same_as<FR, void>;
				constexpr auto count = M::args_count;

				if constexpr (is_void)
				{
					if constexpr (count == 0)
					{
						return [this, name]() -> FR
						{
							void_call(name, {});
						};
					}
					else if constexpr (count == 1)
					{
						return[this, name]<typename P1>(P1 && p1) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1)).get());
						};
					}
					else if constexpr (count == 2)
					{
						return[this, name]<typename P1, typename P2>(P1 && p1, P2 && p2) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2)).get());
						};
					}
					else if constexpr (count == 3)
					{
						return[this, name]<typename P1, typename P2, typename P3>(P1 && p1, P2 && p2, P3 && p3) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3)).get());
						};
					}
					else if constexpr (count == 4)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4>(P1 && p1, P2 && p2, P3 && p3, P4 && p4) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4)).get());
						};
					}
					else if constexpr (count == 5)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5>(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5)).get());
						};
					}
					else if constexpr (count == 6)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>(P1 && p1, P2 && p2, P3 && p3, P4 && p4,
							P5 && p5, P6 && p6) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5), std::forward<P6>(p6)).get());
						};
					}
					else if constexpr (count == 7)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>(P1 && p1, P2 && p2, P3 && p3,
							P4 && p4, P5 && p5, P6 && p6, P7 && p7) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7)).get());
						};
					}
					else if constexpr (count == 8)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>(P1 && p1,
							P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8)).get());
						};
					}
					else if constexpr (count == 9)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
							(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8, P9 && p9) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9)).get());
						};
					}
					else if constexpr (count == 10)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9,
							typename P10>
							(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8, P9 && p9, P10 && p10) -> FR
						{
							void_call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
								std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9),
								std::forward<P10>(p10)).get());
						};
					}
					else
						static_assert(dependent_false<M>, "The number of method parameters is too big.");
				}
				else
				{
					using R = typename FR::result_type;
					constexpr bool is_future_void = std::same_as<R, void>;

					if constexpr (count == 0)
					{
						return [this, name]() -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, {});
							else
								co_return unmarshal<R>(co_await call(name, {}));
						};
					}
					else if constexpr (count == 1)
					{
						return[this, name]<typename P1>(P1 && p1) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1)).get()));
						};
					}
					else if constexpr (count == 2)
					{
						return[this, name]<typename P1, typename P2>(P1 && p1, P2 && p2) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2)).get()));
						};
					}
					else if constexpr (count == 3)
					{
						return[this, name]<typename P1, typename P2, typename P3>(P1 && p1, P2 && p2, P3 && p3) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3)).get()));
						};
					}
					else if constexpr (count == 4)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4>(P1 && p1, P2 && p2, P3 && p3, P4 && p4) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4)).get()));
						};
					}
					else if constexpr (count == 5)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5>(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5)).get()));
						};
					}
					else if constexpr (count == 6)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>(P1 && p1, P2 && p2, P3 && p3, P4 && p4,
							P5 && p5, P6 && p6) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5), std::forward<P6>(p6)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5), std::forward<P6>(p6)).get()));
						};
					}
					else if constexpr (count == 7)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>(P1 && p1, P2 && p2, P3 && p3,
							P4 && p4, P5 && p5, P6 && p6, P7 && p7) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7)).get()));
						};
					}
					else if constexpr (count == 8)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>(P1 && p1,
							P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8)).get()));
						};
					}
					else if constexpr (count == 9)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
							(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8, P9 && p9) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9)).get()));
						};
					}
					else if constexpr (count == 10)
					{
						return[this, name]<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9,
							typename P10>
							(P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7, P8 && p8, P9 && p9, P10 && p10) -> FR
						{
							if constexpr (is_future_void)
								co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3), std::forward<P4>(p4),
									std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9),
									std::forward<P10>(p10)).get());
							else
								co_return unmarshal<R>(co_await call(name, create_writer_with_state(get_state(), std::forward<P1>(p1), std::forward<P2>(p2), std::forward<P3>(p3),
									std::forward<P4>(p4), std::forward<P5>(p5), std::forward<P6>(p6), std::forward<P7>(p7), std::forward<P8>(p8), std::forward<P9>(p9),
									std::forward<P10>(p10)).get()));
						};
					}
					else
						static_assert(dependent_false<M>, "The number of method parameters is too big.");
				}
			}

			corsl::future<payload_t> call(method_id name, payload_t data)
			{
				return static_cast<Derived *>(this)->do_call(name, std::move(data));
			}

			void void_call(method_id name, payload_t data)
			{
				return static_cast<Derived *>(this)->do_void_call(name, std::move(data));
			}

			template<class M>
			using get_method_descriptor = std::decay_t<decltype(std::declval<Interface *>()->*M::pointer)>;

			using methods = boost::describe::describe_members<Interface, boost::describe::mod_public>;
			static_assert(mp11::mp_size<methods>::value >= 1, "Interface must contain at least one method");

		public:
			struct is_client_marshaller;
			static constexpr const bool only_void_methods = mp11::mp_all_of<
				mp11::mp_transform<get_method_descriptor, methods>, is_void_method>::value;

			marshal_client()
			{
				auto *pT = static_cast<Interface *>(this);
				mp11::mp_for_each<methods>([&]<typename M>(M)
				{
					using Member = std::decay_t<decltype(pT->*M::pointer)>;
					pT->*M::pointer = build_call_member<Member>(get_method_id<M>());
				});
			}
		};

		struct method_map_entry
		{
			method_id name;
			int ordinal;
		};

		template<class Methods>
		inline consteval auto build_method_map()
		{
			using size = mp11::mp_size<Methods>;
			std::array<method_map_entry, size::value> result{};
			mp11::mp_for_each<Methods>([&result, ord = 0]<typename M>(M) mutable
			{
				result[ord] = { get_method_id<M>(), ord };
				++ord;
			});
			sr::sort(result, sr::less{}, [](const auto& e) { return e.name; });
			return result;
		}

		template<class Derived, class Interface>
		class marshal_server
		{
			static_assert(boost::describe::has_describe_members<Interface>::value, "Interface type must be described using BOOST_DESCRIBE_STRUCT");
			using Methods = boost::describe::describe_members<Interface, boost::describe::mod_public>;
			static_assert(mp11::mp_size<Methods>::value >= 1, "Interface must contain at least one method");

			static constexpr const auto static_method_map{ build_method_map<Methods>() };
			Interface implementation;

			template<class M>
			using get_method_descriptor = std::decay_t<decltype(std::declval<Interface *>()->*M::pointer)>;

			//
			auto &get_state() noexcept
			{
				return static_cast<Derived *>(this)->get_serializer_state();
			}

		protected:
			corsl::future<payload_t> dispatch(method_id name, std::vector<std::byte> data)
			{
				if (auto it = sr::lower_bound(static_method_map, name, sr::less{}, [](const auto& e) { return e.name; }); it != static_method_map.end() && it->name == name)
				{
					co_return co_await mp11::mp_with_index<mp11::mp_size<Methods>::value>(static_cast<size_t>(it->ordinal), [&]<typename I>(I) -> corsl::future<payload_t>
					{
						using M = mp11::mp_at<Methods, I>;
						using Member = std::decay_t<decltype(implementation.*M::pointer)>;
						using FR = typename Member::result_type;
						static_assert(valid_return<FR>, "Interface method return type must be a future or void");
						typename Member::stored_args_t tuple;
						Reader{ data, get_state() } >> tuple;

						constexpr bool is_void = std::same_as<FR, void>;

						if constexpr (!is_void)
						{
							using R = typename FR::result_type;
							constexpr bool is_future_void = std::same_as<R, void>;

							if constexpr (is_future_void)
							{
								co_await std::apply(implementation.*M::pointer, std::move(tuple));
								co_return payload_t{};
							}
							else
							{
								data.clear();
								auto result = co_await std::apply(implementation.*M::pointer, std::move(tuple));
								co_return create_writer_on_with_state(std::move(data), get_state(), result).get();
							}
						}
						else
						{
							co_return payload_t{};
						}
					});
				}
				else
				{
					corsl::throw_error(E_NOTIMPL);
				}
			}

			void void_dispatch(method_id name, std::vector<std::byte> data)
			{
				if (auto it = sr::lower_bound(static_method_map, name, sr::less{}, [](const auto& e) { return e.name; }); it != static_method_map.end() && it->name == name)
				{
					mp11::mp_with_index<mp11::mp_size<Methods>::value>(static_cast<size_t>(it->ordinal), [&]<typename I>(I)
					{
						using M = mp11::mp_at<Methods, I>;
						using Member = std::decay_t<decltype(implementation.*M::pointer)>;
						using FR = typename Member::result_type;
						static_assert(valid_return<FR>, "Interface method return type must be a future or void");
						constexpr bool is_void = std::same_as<FR, void>;

						if constexpr (is_void)
						{
							typename Member::stored_args_t tuple;
							Reader{ data, get_state() } >> tuple;

							std::apply(implementation.*M::pointer, std::move(tuple));
						}
					});
				}
				else
				{
					corsl::throw_error(E_NOTIMPL);
				}
			}

		public:
			struct is_server_marshaller;
			static constexpr const bool only_void_methods = mp11::mp_all_of<
				mp11::mp_transform<get_method_descriptor, Methods>, is_void_method>::value;

			marshal_server() = default;
			marshal_server(Interface &&impl) noexcept :
				implementation{ std::move(impl) }
			{}

			void set_implementation(Interface &&impl) noexcept
			{
				implementation = std::move(impl);
			}

			auto &get_implementation() noexcept
			{
				return implementation;
			}
		};
	}

	using details::method;

	template<class Interface>
	struct client_of
	{
		struct is_marshaller_trait;

		template<class Derived>
		using fn = details::marshal_client<Derived, Interface>;
	};

	template<class Interface>
	struct server_of
	{
		struct is_marshaller_trait;

		template<class Derived>
		using fn = details::marshal_server<Derived, Interface>;
	};

	template<class T>
	concept is_marshaller = requires
	{
		typename T::is_marshaller_trait;
	};

	template<class T>
	using is_marshaller_t = std::bool_constant<is_marshaller<T>>;
}
