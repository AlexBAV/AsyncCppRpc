//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#include "dependencies.h"
#include "cista_reflection/to_tuple.h"

namespace crpc
{
	namespace details
	{
		namespace sr = std::ranges;
		namespace rv = std::views;
		namespace mp11 = boost::mp11;

		// Writer
		template<class T, class Writer>
		concept supports_custom_write_internal = requires(const T & v, Writer & w)
		{
			v.serialize_write(w);
		};

		template<class T, class Writer>
		concept supports_custom_write_external = requires(const T & v, Writer & w)
		{
			serialize_write(w, v);
		};

		template<class T>
		concept has_emplace_back = requires(T & v, typename T::value_type &&p)
		{
			v.emplace_back(std::move(p));
		};

		template<class T>
		concept has_reserve = requires(T & v, size_t count)
		{
			v.reserve(count);
		};

		template<class T>
		struct safe_value_type
		{
			using type = std::remove_cv_t<T>;
		};

		template<class K, class V>
		struct safe_value_type<std::pair<const K, V>>
		{
			using type = std::pair<K, V>;
		};

		template<class T>
		using safe_value_type_t = typename safe_value_type<T>::type;

		struct empty_serializer_state
		{};

		template<class State>
		struct state_holder
		{
			using StoredState = State;
			static constexpr const bool has_state = true;
			State &state;

			state_holder(State &state) noexcept :
				state{ state }
			{}
		};

		template<>
		struct state_holder<empty_serializer_state>
		{
			using StoredState = empty_serializer_state;
			static constexpr const bool has_state = false;
		};

		template<class State = empty_serializer_state>
		class Writer : public state_holder<State>
		{
			using Container = std::vector<std::byte>;
			using state_holder = state_holder<State>;

			Container storage;

			void add(const std::byte *begin, std::size_t size)
			{
				storage.insert(storage.end(), begin, begin + size);
			}

			template<class Iterator, class Sentinel>
			void write(const Iterator &begin, const Sentinel &end, std::true_type)
			{
				if (begin != end)
					add(reinterpret_cast<const std::byte *>(std::addressof(*begin)), sizeof(*begin) * std::distance(begin, end));
			}

			template<class Iterator, class Sentinel>
			void write(Iterator begin, const Sentinel &end, std::false_type)
			{
				for (; begin != end; ++begin)
					write(*begin);
			}

			// serialization of std::error_code is prohibited because it is not portable
			void write(const std::error_code &) = delete;

			// vector
			template<class T, class Alloc>
			void write(const std::vector<T, Alloc> &val)
			{
				write_range(val);
			}

			// string
			template<class Char, class Traits, class Alloc>
			void write(const std::basic_string<Char, Traits, Alloc> &val)
			{
				write(static_cast<uint32_t>(val.size()));
				write(val.begin(), val.end(), std::true_type{});
			}

			template<class Char, class Traits>
			void write(const std::basic_string_view<Char, Traits> &val)
			{
				write(static_cast<uint32_t>(val.size()));
				write(val.begin(), val.end(), std::true_type{});
			}

			// optional
			template<class T>
			void write(const std::optional<T> &val)
			{
				const auto present = static_cast<bool>(val);
				write(present);
				if (present)
					write(*val);
			}

			// expected
			template<class V, class E>
			void write(const std::expected<V, E> &val)
			{
				const auto present = static_cast<bool>(val);
				write(present);
				if (present)
					write(*val);
				else
					write(val.error());
			}

			// pair
			template<class P1, class P2>
			void write(const std::pair<P1, P2> &val)
			{
				write(val.first);
				write(val.second);
			}

			// tuple
			template<class...Args>
			void write(const std::tuple<Args...> &val)
			{
				write_tuple_helper(val, std::make_index_sequence<sizeof...(Args)>());
			}

			template<class Tuple, size_t...I>
			void write_tuple_helper(const Tuple &val, std::index_sequence<I...>)
			{
				(..., write(std::get<I>(val)));
			}

			// variant
			template<class...Ts>
			void write(const std::variant<Ts...> &val)
			{
				const auto index = static_cast<uint16_t>(val.index());
				write(index);
				mp11::mp_with_index<sizeof...(Ts)>(static_cast<size_t>(index), [&](auto I)
					{
						write(std::get<I>(val));
					});
			}

			template<sr::common_range T>
			void write_range(const T &val)
			{
				using value_type = sr::range_value_t<T>;
				write(static_cast<uint32_t>(val.size()));
				write(sr::begin(val), sr::end(val), std::is_trivially_copyable<value_type>{});
			}

			// generic object write
			template<class T>
			void write(const T &val)
			{
				if constexpr (supports_custom_write_internal<T, Writer>)
				{
					val.serialize_write(*this);
				}
				else if constexpr (supports_custom_write_external<T, Writer>)
				{
					serialize_write(*this, val);
				}
				else if constexpr (boost::describe::has_describe_members<T>::value)
				{
					mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>([&]<typename D>(D)
					{
						write(val.*D::pointer);
					});
				}
				else if constexpr (sr::common_range<T>)
				{
					write_range(val);
				}
				else if constexpr (std::is_trivially_copyable_v<T>)
					add(reinterpret_cast<const std::byte *>(&val), sizeof(val));
				else
				{
					// use cista reflection
					write(cista::to_tuple(val));
				}
			}

		public:
			struct is_serializer_writer;

			Writer() requires (!state_holder::has_state) = default;
			Writer(std::vector<std::byte> &&storage) noexcept requires (!state_holder::has_state) :
				storage{ std::move(storage) }
			{}

			Writer(State &state) noexcept requires state_holder::has_state : 
				state_holder{ state }
			{}

			Writer(std::vector<std::byte> &&storage, State &state) noexcept requires state_holder::has_state :
				storage{ std::move(storage) },
				state_holder{ state }
			{}


			const Container &get() const &noexcept
			{
				return storage;
			}

			Container &&get() && noexcept
			{
				return std::move(storage);
			}

			template<class T>
			Writer &operator <<(const T &val)
			{
				write(val);
				return *this;
			}

			State &state() const noexcept requires state_holder::has_state
			{
				return state_holder::state;
			}
		};

		// deduction guides
		Writer()->Writer<>;
		Writer(std::vector<std::byte> &&storage)->Writer<>;

		template<class State>
		Writer(State &state)->Writer<State>;

		template<class...Args>
		inline auto create_writer(const Args &...args)
		{
			Writer w;
			(w << ... << args);
			return w;
		}

		template<class State, class...Args>
		inline auto create_writer_with_state(State &state, const Args &...args)
		{
			if constexpr (std::same_as<State, empty_serializer_state>)
			{
				Writer w;
				(w << ... << args);
				return w;
			}
			else
			{
				Writer w{ state };
				(w << ... << args);
				return w;
			}
		}

		template<class...Args>
		inline auto create_writer_on(std::vector<std::byte> &&data, const Args &...args)
		{
			Writer w{ std::move(data) };
			(w << ... << args);
			return w;
		}

		template<class State, class...Args>
		inline Writer<> create_writer_on_with_state(std::vector<std::byte> &&data, State &state, const Args &...args)
		{
			if constexpr (std::same_as<State, empty_serializer_state>)
			{
				Writer w{ std::move(data) };
				(w << ... << args);
				return w;
			}
			else
			{
				Writer w{ std::move(data), state };
				(w << ... << args);
				return w;
			}
		}

		// Reader
		template<class Container>
		concept container_has_resize = requires(Container & c, size_t newsize)
		{
			c.resize(newsize);
		};

		template<class T, class Reader>
		concept supports_custom_read_internal = requires(T & v, Reader & r)
		{
			v.serialize_read(r);
		};

		template<class T, class Reader>
		concept supports_custom_read_external = requires(T & v, Reader & r)
		{
			serialize_read(r, v);
		};

		template<class State = empty_serializer_state>
		class Reader : public state_holder<State>
		{
			using span = std::span<const std::byte>;
			using iterator = typename span::iterator;
			using state_holder = state_holder<State>;

			span range;
			iterator it;

			void read(std::byte *destination, size_t size)
			{
				std::copy(it, it + size, destination);
				it += size;
			}

			template<class Iterator, class Sentinel>
			void read(const Iterator &begin, const Sentinel &end, std::true_type)
			{
				const auto size = sizeof(*begin) * std::distance(begin, end);
				if (size)
				{
					std::copy(it, it + size, reinterpret_cast<std::byte *>(std::addressof(*begin)));
					it += size;
				}
			}

			template<class Iterator, class Sentinel>
			void read(Iterator begin, const Sentinel &end, std::false_type)
			{
				for (; begin != end; ++begin)
					read(*begin);
			}

			// string
			template<class Char, class Traits, class Alloc>
			void read(std::basic_string<Char, Traits, Alloc> &val)
			{
				uint32_t count;
				read(count);
				val.resize_and_overwrite(count, [&](Char *data, size_t size)
					{
						read(reinterpret_cast<std::byte *>(data), sizeof(Char) * size);
						return size;
					});
			}

			// serialization of std::error_code is prohibited because it is not portable
			void read(std::error_code &) = delete;

			// vector
			template<class T>
			void read(std::vector<T> &val)
			{
				read_range(val);
			}

			// optional
			template<class T>
			void read(std::optional<T> &val)
			{
				bool present{};
				read(present);
				if (present)
				{
					val.emplace();
					read(*val);
				}
			}

			// expected
			template<class T, class E>
			void read(std::expected<T, E> &val)
			{
				bool present{};
				read(present);
				if (present)
				{
					val.emplace();
					read(*val);
				}
				else
				{
					E err;
					read(err);
					val = std::unexpected(std::move(err));
				}
			}

			// tuple
			template<class...Args>
			void read(std::tuple<Args...> &val)
			{
				read_tuple_helper(val, std::make_index_sequence<sizeof...(Args)>());
			}

			template<class Tuple, size_t...I>
			void read_tuple_helper(Tuple &val, std::index_sequence<I...>)
			{
				(..., read(std::get<I>(val)));
			}

			// pair
			template<class P1, class P2>
			void read(std::pair<P1, P2> &val)
			{
				read(val.first);
				read(val.second);
			}

			// variant
			template<class... Ts>
			void read(std::variant<Ts...> &val)
			{
				using variant_t = std::variant<Ts...>;

				uint16_t index;
				read(index);
				boost::mp11::mp_with_index<sizeof...(Ts)>(index, [&](auto I)
					{
						using T = std::variant_alternative_t<I, variant_t>;
						T result;
						read(result);
						val = std::move(result);
					});
			}

			template<class T>
			void read(T &val)
			{
				if constexpr (supports_custom_read_internal<T, Reader>)
				{
					val.serialize_read(*this);
				}
				else if constexpr (supports_custom_read_external<T, Reader>)
				{
					// use ADL
					serialize_read(*this, val);
				}
				else if constexpr (boost::describe::has_describe_members<T>::value)
				{
					mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>([&]<typename D>(D)
					{
						read(val.*D::pointer);
					});
				}
				else if constexpr (sr::common_range<T>)
				{
					read_range(val);
				}
				else if constexpr (std::is_trivially_copyable_v<T>)
					read(reinterpret_cast<std::byte *>(&val), sizeof(val));
				else
				{
					// use cista reflection
					auto tuple = cista::to_tuple(val);
					read(tuple);
				}
			}

			template<sr::common_range T>
			void read_range(T &val)
			{
				using value_type = sr::range_value_t<T>;
				uint32_t count;
				read(count);
				read_range(val, count, std::is_trivially_copyable<value_type>{});
			}

			template<class T>
			void read_range(T &val, size_t count, std::true_type)
			{
				if constexpr (container_has_resize<T>)
					val.resize(count);
				read(val.begin(), val.end(), std::true_type{});
			}

			template<class T>
			void read_range(T &val, size_t count, std::false_type)
			{
				using value_type = safe_value_type_t<sr::range_value_t<T>>;
				val.clear();
				if constexpr (has_reserve<T>)
					val.reserve(count);
				while (count--)
				{
					value_type v;
					read(v);
					if constexpr (has_emplace_back<T>)
						val.emplace_back(std::move(v));
					else
						val.insert(std::move(v));
				}
			}

		public:
			struct is_serializer_reader;

			Reader(span range) noexcept requires (!state_holder::has_state) :
				range{ range },
				it{ sr::begin(range) }
			{
			}

			Reader(span range, empty_serializer_state &) noexcept requires (!state_holder::has_state) :
				range{ range },
				it{ sr::begin(range) }
			{
			}

			Reader(span range, State &state) noexcept requires state_holder::has_state :
				state_holder{ state },
				range{ range },
				it{ sr::begin(range) }
			{
			}

			Reader(const Reader &) = delete;
			Reader &operator =(const Reader &) = delete;
			Reader(Reader &&) = default;
			Reader &operator =(Reader &&) = default;

			template<class T>
			Reader &operator >>(T &val)
			{
				read(val);
				return *this;
			}

			auto get_remaining() const noexcept
			{
				return std::span{ it, range.end() };
			}

			State &state() const noexcept requires state_holder::has_state
			{
				return state_holder::state;
			}

			void read_bytes(std::span<std::byte> destination)
			{
				read_range(destination);
			}
		};

		template<class State>
		Reader(std::span<const std::byte> range, State &state)->Reader<State>;
		Reader(std::span<const std::byte> range, empty_serializer_state &state)->Reader<>;
		Reader(std::span<const std::byte> range)->Reader<>;

		namespace concepts
		{
			template<class Writer>
			concept writer = requires
			{
				typename Writer::is_serializer_writer;
			};

			template<class Reader>
			concept reader = requires
			{
				typename Reader::is_serializer_reader;
			};

			template<class ReaderOrWriter>
			concept reader_or_writer = reader<ReaderOrWriter> || writer<ReaderOrWriter>;

			template<class ReaderOrWriter, class State>
			concept has_state = std::same_as<State, typename ReaderOrWriter::StoredState>;

			template<class ReaderOrWriter>
			concept has_no_state = has_state<ReaderOrWriter, empty_serializer_state>;
		}
	}

	using details::Writer;
	using details::Reader;
	using details::create_writer;
	using details::create_writer_with_state;
	using details::create_writer_on;
	using details::create_writer_on_with_state;

	using details::concepts::reader;
	using details::concepts::writer;
	using details::concepts::has_state;
	using details::concepts::has_no_state;
}
