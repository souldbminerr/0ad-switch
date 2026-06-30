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

#include "lib/self_test.h"

#include "ps/strings/StringBuilder.h"

#include <array>
#include <limits>

class TestStringBuilder : public CxxTest::TestSuite
{
public:
	template<typename T>
	void Check(const T value, const std::string_view str)
	{
		char buffer[64] = {};

		PS::StringBuilder builder{{std::begin(buffer), std::end(buffer)}};
		builder.Append(value);
		TS_ASSERT_EQUALS(builder.Str(), str);
	}

	void test_append()
	{
		Check<int>(0, "0");
		Check<int>(std::numeric_limits<int>::min(), "-2147483648");
		Check<int>(std::numeric_limits<int>::max(), "2147483647");

		Check<uint64_t>(std::numeric_limits<uint64_t>::min(), "0");
		Check<uint64_t>(std::numeric_limits<uint64_t>::max(), "18446744073709551615");

		Check<float>(0.0f, "0");
		Check<float>(1.0f, "1");
		Check<float>(-1.0f, "-1");
		Check<float>(0.5f, "0.5");
		Check<float>(1e-3f, "0.001");
	}

	void test_overflow()
	{
		char buffer[8] = {};
		buffer[3] = 1;
		buffer[4] = 2;
		PS::StringBuilder builder{{std::begin(buffer), std::begin(buffer) + 4}};
		builder.Append("abc");
		TS_ASSERT_EQUALS(buffer[0], 'a');
		TS_ASSERT_EQUALS(buffer[3], 0);
		TS_ASSERT_EQUALS(buffer[4], 2);
		TS_ASSERT_EQUALS(builder.Str(), "abc");
	}
};
