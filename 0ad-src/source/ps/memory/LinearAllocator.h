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

#ifndef INCLUDED_PS_LINEARALLOCATOR
#define INCLUDED_PS_LINEARALLOCATOR

#include "lib/bits.h"
#include "ps/containers/StaticVector.h"

#include <cstddef>
#include <fmt/format.h>
#include <memory>

namespace PS::Memory
{

/**
 * Linear allocator (also known as an arena allocator or bump allocator) is
 * a simple and fast system that allocates memory sequentially from a single
 * growing memory buffer. Each allocation only moves a free pointer forward.
 * Releasing all allocations happens at the Release call.
 *
 * It's not thread-safe.
 *
 * It's intendent to use for allocations with a short lifespan (f.e. function
 * scoped).
 *
 * There is std::pmr::monotonic_buffer_resource but it doesn't provide
 * information about the current capacity and doesn't allow to limit the
 * capacity. Also avoiding virtual calls for frequent allocations.
 */
class LinearAllocator
{
public:
	LinearAllocator(const std::size_t initialSize, const std::size_t maxCapacity)
		: m_Buffer{std::make_unique<std::byte[]>(initialSize)}, m_Capacity{initialSize}, m_MaxCapacity{maxCapacity}
	{
		ENSURE(initialSize > 0);
	}

	~LinearAllocator()
	{
		Release();
	}

	void* allocate(std::size_t n, std::size_t alignment)
	{
		m_Size = ROUND_UP(m_Size, alignment);
		if (m_Size + n > m_Capacity)
		{
			m_BuffersToFree.emplace_back(std::move(m_Buffer));
			m_Size = 0;
			m_Capacity = std::min(std::max(round_up_to_pow2(n), m_Capacity * 2), m_MaxCapacity);
			if (n > m_Capacity)
			{
				throw CapacityExceededException{fmt::format(
					"Tried to allocate from LinearAllocator with a size of {} but the capacity is only {}",
					n, m_Capacity)};
			}
			m_Buffer = std::make_unique<std::byte[]>(m_Capacity);
		}
		void* ptr{m_Buffer.get() + m_Size};
		m_Size += n;
		++m_AllocationCount;
		return ptr;
	}

	void deallocate([[maybe_unused]] void* ptr, [[maybe_unused]] std::size_t n)
	{
		// Do nothing. All allocations will be removed in Release.
		ENSURE(m_AllocationCount > 0);
		--m_AllocationCount;
	}

	std::size_t GetSize() const { return m_Size; }

	std::size_t GetCapacity() const { return m_Capacity; }

	void Release()
	{
		ENSURE(m_AllocationCount == 0);
		m_Size = 0u;
		m_BuffersToFree.clear();
	}

private:
	std::size_t m_Size{0u}, m_Capacity, m_MaxCapacity;
	std::unique_ptr<std::byte[]> m_Buffer;
	// We keep track of the allocation count to catch incorrect usages.
	std::uint32_t m_AllocationCount{0u};
	// initialSize * 2^16 bytes should be enough for all allocations.
	PS::StaticVector<std::unique_ptr<std::byte[]>, 16> m_BuffersToFree;
};

/**
 * @see LinearAllocator
 *
 * Scoped linear allocators ensures that the base allocator is empty.
 *
 * TODO: currently it gives a very simple tree structure of one root node -
 * base allocator and many leaves. We need to implement a tree-like call
 * structure. It allows to reuse memory between different "tree" branches.
 * While a "node" is locked nobody can't allocate from that allocator.
 */
class ScopedLinearAllocator
{
public:
	ScopedLinearAllocator(LinearAllocator& allocator)
		: m_Allocator{allocator}
	{
		ENSURE(m_Allocator.GetSize() == 0);
	}

	~ScopedLinearAllocator()
	{
		ENSURE(m_AllocationCount == 0);
		m_Allocator.Release();
	}

	void* allocate(std::size_t n, std::size_t alignment)
	{
		++m_AllocationCount;
		return m_Allocator.allocate(n, alignment);
	}

	void deallocate(void* ptr, std::size_t n)
	{
		m_Allocator.deallocate(ptr, n);
		ENSURE(m_AllocationCount > 0);
		--m_AllocationCount;
	}

private:
	LinearAllocator& m_Allocator;
	std::uint32_t m_AllocationCount{0u};
};

} // namespace PS::Memory

#endif // INCLUDED_PS_LINEARALLOCATOR
