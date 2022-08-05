//-------------------------------------------------------------------------------------------------------
// AsyncCppRpc - Light-weight asynchronous transport-agnostic C++ RPC library
// Copyright (C) 2022 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

namespace crpc::details
{
	struct method_id
	{
		uint32_t v{};

		constexpr method_id() = default;

		constexpr method_id(uint32_t v) noexcept :
			v{ v }
		{
		}

		constexpr uint32_t get() const noexcept
		{
			return v;
		}

		explicit constexpr operator bool() const noexcept
		{
			return !!v;
		}

		constexpr auto operator<=>(const method_id &) const = default;
		constexpr bool operator==(const method_id &) const = default;
	};

	using payload_t = std::vector<std::byte>;

	namespace fnv
	{
		template<class Char>
		concept fnvchar = std::same_as<char, Char> || std::same_as<wchar_t, Char>;

		template<class String>
		concept fnvstring = std::ranges::range<String> && fnvchar<std::ranges::range_value_t<String>>;

		template<fnvstring String>
		[[nodiscard]]
		inline constexpr uint32_t fnv_hash(const String &text) noexcept
		{
			const constexpr uint32_t prime = 16777619u;

			uint32_t val{ 2166136261u };
			for (auto c : text)
			{
				val ^= static_cast<uint32_t>(static_cast<char>(c));
				val *= prime;
			}

			return val;
		}
	}
}
