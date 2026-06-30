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

#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace PS
{
/**
 * Simplifed version of std::ranges::contains (C++23) as we don't support the
 * original one yet. The naming intentionally follows the STL version to make
 * the future replacement easier with less changing.
 * It supports only a subset of std::ranges::contains functionality.
 */
template<typename Range, typename T = typename Range::value_type>
bool contains(Range&& range, const T& value)
{
	return std::any_of(range.begin(), range.end(), [&](const auto& elem)
		{
			return elem == value;
		});
}

/**
 * @brief Replaces all occurrences of a substring within a string with a given value.
 *
 * This function searches the input string `base` for all instances of the specified
 * `tag` and replaces them with the provided `value`. The replacement is performed
 * in-place and modifies the original string.
 *
 * @param base The string in which to perform replacements. Modified in-place.
 * @param tag The substring to search for (e.g., a placeholder like L"{civ}").
 * @param value The string to replace each occurrence of `tag` with.
 */
inline void ReplaceSubrange(std::wstring& base, std::wstring_view tag, std::wstring_view value)
{
	size_t pos = 0;
	while ((pos = base.find(tag, pos)) != std::wstring::npos)
	{
		base.replace(pos, tag.length(), value);
		pos += value.length();
	}
}

} // namespace PS
#endif // ALGORITHM_H
