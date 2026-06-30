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

#include "lib/allocators/STLAllocators.h"
#include "ps/memory/LinearAllocator.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

class TestLinearAllocator : public CxxTest::TestSuite
{
public:
	void test_construction()
	{
		PS::Memory::LinearAllocator allocator{16, 16};

		for (size_t index{0}; index < 4; ++index)
		{
			void* ptr{allocator.allocate(4, 4)};
			TS_ASSERT(ptr);
			TS_ASSERT_EQUALS(allocator.GetSize(), 4u * (index + 1));
			allocator.deallocate(ptr, 4);
			TS_ASSERT_EQUALS(allocator.GetSize(), 4u * (index + 1));
			TS_ASSERT_EQUALS(allocator.GetCapacity(), 16u);
		}
		TS_ASSERT_EQUALS(allocator.GetSize(), 16u);
		allocator.Release();
		TS_ASSERT_EQUALS(allocator.GetSize(), 0u);
	}

	void test_scoped_construction()
	{
		PS::Memory::LinearAllocator allocator{32, 32};

		for (size_t index{0}; index < 4; ++index)
		{
			TS_ASSERT_EQUALS(allocator.GetSize(), 0u);
			PS::Memory::ScopedLinearAllocator scopedAllocator{allocator};
			scopedAllocator.deallocate(scopedAllocator.allocate(32, 4), 32);
			TS_ASSERT_EQUALS(allocator.GetSize(), 32u);
		}
		TS_ASSERT_EQUALS(allocator.GetSize(), 0u);

		{
			PS::Memory::ScopedLinearAllocator scopedAllocator1{allocator};
			PS::Memory::ScopedLinearAllocator scopedAllocator2{allocator};
			ProxyAllocator<int, PS::Memory::ScopedLinearAllocator> proxyAllocator1{scopedAllocator1};
			ProxyAllocator<int, PS::Memory::ScopedLinearAllocator> proxyAllocator2{scopedAllocator2};
			TS_ASSERT_EQUALS(proxyAllocator1, proxyAllocator1);
			TS_ASSERT_DIFFERS(proxyAllocator1, proxyAllocator2);
		}
	}

	void test_increase_capacity()
	{
		PS::Memory::LinearAllocator allocator{4, 32};
		TS_ASSERT_EQUALS(allocator.GetCapacity(), 4u);
		allocator.deallocate(allocator.allocate(32, 4), 32);
		TS_ASSERT_EQUALS(allocator.GetSize(), 32u);
		TS_ASSERT_EQUALS(allocator.GetCapacity(), 32u);
	}

	void test_exception()
	{
		PS::Memory::LinearAllocator allocator{4, 4};
		TS_ASSERT_THROWS((allocator.allocate(8, 8)), PS::CapacityExceededException&);

		{
		PS::Memory::LinearAllocator allocator{4, 8};
		allocator.deallocate(allocator.allocate(4, 4), 4);
		allocator.deallocate(allocator.allocate(4, 4), 4);
		allocator.deallocate(allocator.allocate(4, 4), 4);
		TS_ASSERT_THROWS(allocator.allocate(16, 4), PS::CapacityExceededException&);
		}
	}
};
