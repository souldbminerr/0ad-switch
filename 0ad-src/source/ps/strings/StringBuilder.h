/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDED_PS_STRINGBUILDER
#define INCLUDED_PS_STRINGBUILDER

#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <version>

#if defined(__cpp_lib_to_chars)
#include <charconv>
#else
#include <fmt/core.h>
#endif

namespace PS
{

/**
 * A helper class to build string in-place without additional cost of
 * allocations, locales and format parsing. Prefer to use the class with stack
 * allocated buffers over standard tools in high-load cases.
 * TODO: in case of overflow it might use a heap allocated storage.
 */
class StringBuilder
{
public:
	StringBuilder(std::span<char> buffer) noexcept
		: m_Buffer{buffer}, m_BufferBegin{buffer.data()}
	{
		ENSURE(!m_Buffer.empty());
	}

	template<typename T>
	typename std::enable_if<std::is_arithmetic_v<T>>::type Append(const T value) noexcept
	{
		ENSURE(m_Buffer.size() > 0);
#if defined(__cpp_lib_to_chars)
		const std::to_chars_result result{std::to_chars(m_Buffer.data(), m_Buffer.data() + m_Buffer.size() - 1, value)};
		ENSURE(result.ec == std::errc());
		m_Buffer = m_Buffer.subspan(result.ptr - m_Buffer.data());
		// to_chars doesn't write terminating null.
		m_Buffer.front() = 0;
#else
		// TODO: switch to std::to_chars after minimal libcxx supports it.
		const fmt::format_to_n_result result{
			fmt::format_to_n(m_Buffer.data(), m_Buffer.size() - 1, "{}", value)};
		ENSURE(m_Buffer.data() != result.out);
		m_Buffer = m_Buffer.subspan(result.size);
		// format_to_n doesn't write terminating null.
		m_Buffer.front() = 0;
#endif
	}

	void Append(const char ch)
	{
		ENSURE(m_Buffer.size() > 1);
		m_Buffer.front() = ch;
		m_Buffer = m_Buffer.subspan(1);
		m_Buffer.front() = 0;
	}

	void Append(const std::string_view str) noexcept
	{
		ENSURE(m_Buffer.size() >= static_cast<size_t>(str.size()) + 1);
		std::copy(str.begin(), str.end(), m_Buffer.begin());
		m_Buffer = m_Buffer.subspan(str.size());
		m_Buffer.front() = 0;
	}

	/**
	 * Returns a string_view to a null-terminated string.
	 */
	std::string_view Str() noexcept
	{
		return {m_BufferBegin, m_Buffer.data()};
	}

private:
	std::span<char> m_Buffer;
	char* m_BufferBegin;
};

} // namespace PS

#endif // INCLUDED_PS_STRINGBUILDER
