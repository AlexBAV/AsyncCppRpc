#pragma once

inline void log(std::string_view text) noexcept
{
	std::cout << text;
}

inline void log(std::wstring_view text) noexcept
{
	std::wcout << text;
}
