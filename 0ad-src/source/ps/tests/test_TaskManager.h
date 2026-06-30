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

#include "lib/types.h"
#include "ps/Future.h"
#include "ps/TaskManager.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

class TestTaskManager : public CxxTest::TestSuite
{
public:
	void test_basic()
	{
		// There is a minimum of 3.
		TS_ASSERT(g_TaskManager.GetNumberOfWorkers() >= 3);

		std::atomic<int> tasks_run = 0;
		auto increment_run = [&tasks_run]() { tasks_run++; };
		Future future{g_TaskManager, increment_run};
		future.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 1);

		// Test Execute.
		std::condition_variable cv;
		std::mutex mutex;
		std::atomic<bool> go = false;
		future = {g_TaskManager, [&]{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [&go]() -> bool { return go; });
			lock.unlock();
			increment_run();
			lock.lock();
			go = false;
			lock.unlock();
			cv.notify_all();
		}};
		TS_ASSERT_EQUALS(tasks_run.load(), 1);
		std::unique_lock<std::mutex> lock(mutex);
		go = true;
		lock.unlock();
		cv.notify_all();
		lock.lock();
		cv.wait(lock, [&go]() -> bool { return !go; });
		TS_ASSERT_EQUALS(tasks_run.load(), 2);
		// Wait on the future before the mutex/cv go out of scope.
		future.Wait();
	}

	void test_Priority()
	{
		std::atomic<int> tasks_run = 0;
		// Push general tasks
		auto increment_run = [&tasks_run]() { tasks_run++; };
		Future future = {g_TaskManager, increment_run};
		Future futureLow = {g_TaskManager, increment_run, Threading::TaskPriority::LOW};
		future.Wait();
		futureLow.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 2);
		// Also check with no waiting expected.
		Future{g_TaskManager, increment_run}.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 3);
		Future{g_TaskManager, increment_run, Threading::TaskPriority::LOW}.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 4);
	}

	void test_Load()
	{
#define ITERATIONS 100000
		std::vector<Future<int>> futures;
		futures.resize(ITERATIONS);
		std::vector<u32> values(ITERATIONS);

		Future f1{g_TaskManager, [&futures]{
			for (u32 i = 0; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }};
		}};

		Future f2{g_TaskManager, [&futures]{
			for (u32 i = 1; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }, Threading::TaskPriority::LOW};
		}};

		Future f3{g_TaskManager, [&futures]{
			for (u32 i = 2; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }};
		}};

		f1.Wait();
		f2.Wait();
		f3.Wait();

		for (size_t i = 0; i < ITERATIONS; ++i)
			TS_ASSERT_EQUALS(futures[i].Get(), 5);
#undef ITERATIONS
	}
};
