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

#include "ps/Future.h"

#include <cstring>
#include <exception>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class TestFuture : public CxxTest::TestSuite
{
public:
	struct TestTaskManager
	{
		std::vector<std::function<void()>> tasks;
		void PushTask(std::function<void()> task)
		{
			tasks.push_back(std::move(task));
		}
	};

	void test_future_basic()
	{
		bool executed{false};
		TestTaskManager ttm;
		Future noret{ttm, [&]{ executed = true; }};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(executed);
	}

	void test_future_return()
	{
		TestTaskManager ttm;
		{
			Future future{ttm, []{ return 1; }};
			TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
			std::exchange(ttm.tasks, {})[0]();
			TS_ASSERT_EQUALS(future.Get(), 1);
		}

		static int destroyed = 0;
		// No trivial constructor or destructor. Also not copiable.
		struct NonDef
		{
			NonDef() = delete;
			NonDef(int input) : value(input) {};
			NonDef(const NonDef&) = delete;
			NonDef(NonDef&& o)
			{
				value = o.value;
				o.value = 0;
			}
			~NonDef() { if (value != 0) destroyed++; }
			int value = 0;
		};
		TS_ASSERT_EQUALS(destroyed, 0);
		{
			Future future{ttm, []{ return NonDef{1}; }};
			TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
			std::exchange(ttm.tasks, {})[0]();
			TS_ASSERT_EQUALS(future.Get().value, 1);
		}
		TS_ASSERT_EQUALS(destroyed, 1);
		/**
		 * TODO: find a way to test this
		{
			Future<NonDef> future;
			std::function<void()> task = future.Wrap([]() { return NonDef{1}; });
			future.CancelOrWait();
			TS_ASSERT_THROWS(future.Get(), const Future<NonDef>::BadFutureAccess&);
		}
		 */
		TS_ASSERT_EQUALS(destroyed, 1);
	}

	void test_future_moving()
	{
		Future<int> future;
		std::function<int()> function;
		TestTaskManager ttm;

		// Set things up so all temporaries passed into the futures will be reset to obviously invalid memory.
		std::aligned_storage_t<sizeof(Future<int>), alignof(Future<int>)> futureStorage;
		std::aligned_storage_t<sizeof(std::function<int()>), alignof(std::function<int()>)> functionStorage;
		Future<int>* f = &future; // CppCheck doesn't read placement new correctly, do this to silence errors.
		std::function<int()>* c = &function;
		f = new (&futureStorage) Future<int>{};
		c = new (&functionStorage) std::function<int()>{};

		*c = []() { return 7; };
		*f = {ttm, std::move(*c)};

		future = std::move(*f);
		function = std::move(*c);

		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();

		TS_ASSERT_EQUALS(future.Get(), 7);

		// Destroy and clear the memory
		f->~Future();
		c->~function();
		memset(&futureStorage, 0xFF, sizeof(decltype(futureStorage)));
		memset(&functionStorage, 0xFF, sizeof(decltype(functionStorage)));
	}

	void test_move_only_function()
	{
		class MoveOnlyType
		{
		public:
			MoveOnlyType() = default;
			MoveOnlyType(MoveOnlyType&) = delete;
			MoveOnlyType& operator=(MoveOnlyType&) = delete;
			MoveOnlyType(MoveOnlyType&&) = default;
			MoveOnlyType& operator=(MoveOnlyType&&) = default;
			int fn() const { return 7; }
		};

		TestTaskManager ttm;

		Future future{ttm, [t = MoveOnlyType{}]{ return t.fn(); }};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();

		TS_ASSERT_EQUALS(future.Get(), 7);
	}

	struct TestException : std::exception
	{
		using std::exception::exception;
	};

	void test_exception()
	{
		TestTaskManager ttm;
		Future<int> future{ttm, []() -> int
		{
			throw TestException{};
		}};

		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(future.IsDone());
		TS_ASSERT_THROWS(future.Get(), const TestException&);
	}

	void test_voidException()
	{
		TestTaskManager ttm;
		Future<void> future{ttm, []
		{
			throw TestException{};
		}};

		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(future.IsDone());
		TS_ASSERT_THROWS(future.Get(), const TestException&);
	}

	void test_implicitException()
	{
		// If the function does not throw but it's the cause something is thrown the exception should
		// also be reported to the code receiving the result.

		class ThrowsOnMove
		{
		public:
			ThrowsOnMove() = default;
			ThrowsOnMove(ThrowsOnMove&&)
			{
				throw TestException{};
			}
		};

		TestTaskManager ttm;

		Future<ThrowsOnMove> future{ttm, []
		{
			return ThrowsOnMove{};
		}};

		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(future.IsDone());
		TS_ASSERT_THROWS(future.Get(), const TestException&);
	}

	void test_stop_token_overload()
	{
		TestTaskManager ttm;
		{
			class DifferentValues
			{
			public:
				bool operator()()
				{
					return false;
				}
				bool operator()(StopToken)
				{
					return true;
				}
			};

			Future<bool> future{ttm, DifferentValues{}};
			TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
			std::exchange(ttm.tasks, {})[0]();
			TS_ASSERT_EQUALS(future.Get(), true);
		}
		{
			class DifferentTypes
			{
			public:
				void operator()()
				{}
				bool operator()(StopToken)
				{
					return true;
				}
			};

			Future<bool> future{ttm, DifferentTypes{}};
			TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
			std::exchange(ttm.tasks, {})[0]();
		}
	}
};
