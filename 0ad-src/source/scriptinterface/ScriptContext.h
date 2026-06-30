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

#ifndef INCLUDED_SCRIPTCONTEXT
#define INCLUDED_SCRIPTCONTEXT

#include <cstdint>
#include <list>
#include <memory>

namespace JS { class Realm; }
struct JSContext;

// Those are minimal defaults. The runtime for the main game is larger and GCs upon a larger growth.
constexpr int DEFAULT_CONTEXT_SIZE = 16 * 1024 * 1024;
constexpr uint32_t DEFAULT_HEAP_GROWTH_BYTES_GCTRIGGER = 2 * 1024 * 1024;

namespace Script
{
class JobQueue;
}

/**
 * Abstraction around a SpiderMonkey JSContext.
 *
 * A single ScriptContext, with the associated context,
 * should only be used on a single thread.
 *
 * (One means to share data between threads and contexts is to create
 * a Script::StructuredClone.)
 */

class ScriptContext
{
public:
	ScriptContext(int contextSize, uint32_t heapGrowthBytesGCTrigger);
	~ScriptContext();

	/**
	 * Returns a context, in which any number of ScriptInterfaces compartments can live.
	 * Each context should only ever be used on a single thread.
	 * @param parentContext Parent context from the parent thread, with which we share some thread-safe data
	 * @param contextSize Maximum size in bytes of the new context
	 * @param heapGrowthBytesGCTrigger Size in bytes of cumulated allocations after which a GC will be triggered
	 */
	static std::shared_ptr<ScriptContext> CreateContext(
		int contextSize = DEFAULT_CONTEXT_SIZE,
		uint32_t heapGrowthBytesGCTrigger = DEFAULT_HEAP_GROWTH_BYTES_GCTRIGGER);


	/**
	 * MaybeIncrementalGC checks if running a GC is worth the time that will take.
	 * The logic is custom as Spidermonkey tends to assume 'idle time' will exist,
	 * which is a thing in websites but not really in 0 A.D.
	 * This can have a few behaviours:
	 *  - doing nothing
	 *  - starting a new incremental GC
	 *  - running a GC slice
	 *  - finishing the incremental GC
	 *  For details, check the SM doc in e.g. GC.cpp and GCapi.cpp
	 */
	void MaybeIncrementalGC();

	/**
	 * Does a non-incremental, shrinking GC.
	 * A shrinking GC dumps JIT code and tries to defragment memory.
	 */
	void ShrinkingGC();

	/**
	 * This is used to keep track of realms which should be prepared for a GC.
	 */
	void RegisterRealm(JS::Realm* realm);
	void UnRegisterRealm(JS::Realm* realm);

	/**
	 * Runs the promise continuation.
	 * On contexts where promises can be used this function has to be
	 * called.
	 * This function has to be called frequently.
	 */
	void RunJobs();

	/**
	 * GetGeneralJSContext returns the context without starting a GC request and without
	 * entering any compartment. It should only be used in specific situations, such as
	 * creating a new compartment, or when initializing a persistent rooted.
	 * If you need the compartmented context of a ScriptInterface, you should create a
	 * ScriptRequest and use the context from that.
	 */
	JSContext* GetGeneralJSContext() const { return m_cx; }

private:

	JSContext* m_cx;
	const std::unique_ptr<Script::JobQueue> m_JobQueue;

	void PrepareZonesForIncrementalGC() const;
	std::list<JS::Realm*> m_Realms;

	int m_ContextSize;
	uint32_t m_HeapGrowthBytesGCTrigger;
	uint32_t m_LastGCBytes{0};
};

// Using a global object for the context is a workaround until Simulation, AI, etc,
// use their own threads and also their own contexts.
extern thread_local std::shared_ptr<ScriptContext> g_ScriptContext;

#endif // INCLUDED_SCRIPTCONTEXT
