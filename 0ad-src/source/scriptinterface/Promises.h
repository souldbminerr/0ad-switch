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

#ifndef INCLUDED_SCRIPTINTERFACE_JOBQUEUE
#define INCLUDED_SCRIPTINTERFACE_JOBQUEUE

#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/UniquePtr.h>
#include <queue>

class ScriptInterface;
struct JSContext;

namespace Script
{
void UnhandledRejectedPromise(JSContext* cx, bool, JS::HandleObject promise,
	JS::PromiseRejectionHandlingState state, void*);

class JobQueue final : public JS::JobQueue
{
public:
	~JobQueue() final = default;

	void runJobs(JSContext*) final;

private:
	JSObject* getIncumbentGlobal(JSContext* cx) final;

	bool enqueuePromiseJob(JSContext* cx, JS::HandleObject, JS::HandleObject job, JS::HandleObject,
		JS::HandleObject) final;

	bool empty() const final;
	bool isDrainingStopped() const final { return false; }

	js::UniquePtr<JS::JobQueue::SavedJobQueue> saveJobQueue(JSContext*) final;

	struct QueueElement
	{
		const ScriptInterface& scriptInterface;
		JS::PersistentRootedObject job;
	};
	using QueueType = std::queue<QueueElement>;
	QueueType m_Jobs;
};
}

#endif // INCLUDED_SCRIPTINTERFACE_JOBQUEUE
