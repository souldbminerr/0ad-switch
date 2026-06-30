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

#ifndef INCLUDED_FUTURE
#define INCLUDED_FUTURE

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

template<typename Callback>
class PackagedTask;

class StopToken
{
public:
	explicit StopToken(const std::atomic<bool>& request) noexcept :
		m_Request{request}
	{}

	bool IsStopRequested() const noexcept
	{
		return m_Request.load();
	}
private:
	const std::atomic<bool>& m_Request;
};

template<typename Callback>
using CallbackResult = typename std::conditional_t<std::is_invocable_v<Callback, StopToken>,
	std::invoke_result<Callback, StopToken>, std::invoke_result<Callback>>::type;

namespace FutureSharedStateDetail
{
struct VoidSubstitution {};
template<typename ResultType>
using NonVoid = std::conditional_t<std::is_void_v<ResultType>, VoidSubstitution, ResultType>;

/**
 * Responsible for syncronization between the task and the receiving thread.
 */
template<typename ResultType>
class Receiver
{
public:
	Receiver() = default;
	~Receiver()
	{
		ENSURE(IsDone());
	}

	Receiver(const Receiver&) = delete;
	Receiver(Receiver&&) = delete;

	bool IsDone() const noexcept
	{
		return m_Done.load();
	}

	void Wait()
	{
		// Fast path: we're already done.
		if (IsDone())
			return;
		// Slow path: we aren't done when we run the above check. Lock and wait until we are.
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_ConditionVariable.wait(lock, [this]{ return IsDone(); });
	}

	/**
	 * Request the executing thread to stop as fast as possible. This is only
	 * a request the execution therad might ignore it.
	 * @see GetResult must not be called after a call to @p RequestStop.
	 */
	void RequestStop() noexcept
	{
		m_StopRequest.store(true);
	}

	/**
	 * Move the result away from the shared state, mark the future invalid.
	 */
	ResultType GetResult()
	{
		// The caller must ensure that this is only called if there is a result.
		ENSURE(IsDone());
		ENSURE(std::holds_alternative<NonVoid<ResultType>>(m_Outcome) ||
			std::get<std::exception_ptr>(m_Outcome));

		if (std::holds_alternative<std::exception_ptr>(m_Outcome))
			std::rethrow_exception(std::exchange(std::get<std::exception_ptr>(m_Outcome), {}));

		[[maybe_unused]] auto ret = std::move(std::get<NonVoid<ResultType>>(m_Outcome));
		m_Outcome.template emplace<std::exception_ptr>();

		if constexpr (std::is_void_v<ResultType>)
			return;
		else
			return ret;
	}

	// This is only set by the executing thread and read by the receiving thread. It is never reset.
	std::atomic<bool> m_Done{false};
	// This is only set by the receiving thread and read by the executing thread. It is never reset.
	std::atomic<bool> m_StopRequest{false};
	std::mutex m_Mutex;
	std::condition_variable m_ConditionVariable;

	std::variant<std::exception_ptr, NonVoid<ResultType>> m_Outcome;
};

/**
 * The shared state between futures and packaged state.
 */
template<typename Callback>
struct SharedState
{
	SharedState(Callback&& callbackFunc) :
		callback{std::forward<Callback>(callbackFunc)}
	{}

	Callback callback;
	Receiver<CallbackResult<Callback>> receiver;
};

} // namespace FutureSharedStateDetail

/**
 * Corresponds somewhat to std::packaged_task.
 * Like packaged_task, this holds a function acting as a promise.
 * This type is mostly just the shared state and the call operator,
 * handling the promise & continuation logic.
 */
template<typename Callback>
class PackagedTask
{
public:
	PackagedTask() = delete;
	PackagedTask(std::shared_ptr<FutureSharedStateDetail::SharedState<Callback>> ss) :
		m_SharedState(std::move(ss))
	{}

	void operator()()
	{
		if (!m_SharedState->receiver.m_StopRequest.load())
		{
			auto& outcome = m_SharedState->receiver.m_Outcome;
			try
			{
				using ResultType = CallbackResult<Callback>;
				if constexpr (std::is_void_v<ResultType>)
				{
					Invoke();
					outcome.template emplace<FutureSharedStateDetail::VoidSubstitution>();
				}
				else
					outcome.template emplace<ResultType>(Invoke());
			}
			catch(...)
			{
				outcome.template emplace<std::exception_ptr>(std::current_exception());
			}
		}

		// Because we might have threads waiting on us, we need to make sure that they either:
		// - don't wait on our condition variable
		// - receive the notification when we're done.
		// This requires locking the mutex (@see Wait).
		{
			std::lock_guard<std::mutex> lock(m_SharedState->receiver.m_Mutex);
			m_SharedState->receiver.m_Done.store(true);
		}

		m_SharedState->receiver.m_ConditionVariable.notify_all();

		// We no longer need the shared state, drop it immediately.
		m_SharedState.reset();
	}

private:
	CallbackResult<Callback> Invoke()
	{
		if constexpr (std::is_invocable_v<Callback, StopToken>)
			return m_SharedState->callback(StopToken{m_SharedState->receiver.m_StopRequest});
		else
			return m_SharedState->callback();
	}

	std::shared_ptr<FutureSharedStateDetail::SharedState<Callback>> m_SharedState;
};

/**
 * Corresponds to std::future.
 * Unlike std::future, Future can request the cancellation of the task that would produce the result.
 * This makes it more similar to Java's CancellableTask or C#'s Task.
 * The name Future was kept over Task so it would be more familiar to C++ users,
 * but this all should be revised once Concurrency TS wraps up.
 *
 * Future is _not_ thread-safe. Call it from a single thread or ensure synchronization externally.
 *
 * The callback never runs after the @p Future is destroyed.
 */
template<typename ResultType>
class Future
{
	template<typename T>
	friend class PackagedTask;

public:
	Future() = default;
	Future(const Future& o) = delete;
	Future(Future&&) = default;
	Future& operator=(Future&& other)
	{
		CancelOrWait();
		m_Receiver = std::move(other.m_Receiver);
		return *this;
	}

	/**
	 * Make the future wait for the result of @a callback.
	 */
	template<typename Callback, typename... Args>
	Future(auto& taskManager, Callback&& callback, Args&&... args)
	{
		static_assert(std::is_same_v<CallbackResult<Callback>, ResultType>,
		"The return type of the wrapped function is not the same as the type the Future expects.");
		static_assert(std::is_invocable_v<Callback, StopToken> || !std::is_invocable_v<Callback, StopToken&>,
			"Consider taking the `StopToken` by value");

		auto temp = std::make_shared<FutureSharedStateDetail::SharedState<Callback>>(
			std::forward<Callback>(callback));
		m_Receiver = {temp, &temp->receiver};

		taskManager.PushTask(PackagedTask<Callback>(std::move(temp)), std::forward<Args>(args)...);
	}

	~Future()
	{
		CancelOrWait();
	}

	/**
	 * Move the result out of the future, and invalidate the future.
	 * If the future is not complete, calls Wait().
	 * If the future is invalid, asserts.
	 */
	ResultType Get()
	{
		ENSURE(!!m_Receiver);

		Wait();
		// This mark the state invalid - can't call Get again.
		return std::exchange(m_Receiver, nullptr)->GetResult();
	}

	/**
	 * @return true if the shared state is valid and has a result (i.e. Get can be called).
	 */
	bool IsDone() const
	{
		return !!m_Receiver && m_Receiver->IsDone();
	}

	/**
	 * @return true if the future has a shared state and it's not been invalidated, ie. pending, started or done.
	 */
	bool Valid() const
	{
		return !!m_Receiver;
	}

	void Wait()
	{
		if (Valid())
			m_Receiver->Wait();
	}

	void CancelOrWait()
	{
		if (!Valid())
			return;
		m_Receiver->RequestStop();
		m_Receiver->Wait();
		m_Receiver.reset();
	}

protected:
	std::shared_ptr<FutureSharedStateDetail::Receiver<ResultType>> m_Receiver;
};

template<typename Callback, typename... Args>
Future(auto& taskManager, Callback&& callback, Args&&... args) -> Future<CallbackResult<Callback>>;

#endif // INCLUDED_FUTURE
