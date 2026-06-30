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

#include "precompiled.h"

#include "ScriptContext.h"

#include "js/Modules.h"
#include "js/friend/PerformanceHint.h"
#include "lib/alignment.h"
#include "lib/debug.h"
#include "ps/CLogger.h"
#include "ps/Profile.h"
#include "ps/Profiler2.h"
#include "ps/ThreadUtil.h"
#include "scriptinterface/ModuleLoader.h"
#include "scriptinterface/Promises.h"
#include "scriptinterface/ScriptEngine.h"

#include <js/Context.h>
#include <js/GCAPI.h>
#include <js/Initialization.h>
#include <js/Promise.h>
#include <js/SliceBudget.h>
#include <js/Stack.h>
#include <jsapi.h>
#include <jsfriendapi.h>

namespace JS { class Realm; }
struct JSContext;
struct JSRuntime;

void GCSliceCallbackHook(JSContext*, JS::GCProgress progress, const JS::GCDescription&)
{
	/**
	 * From the GCAPI.h file:
	 * > During GC, the GC is bracketed by GC_CYCLE_BEGIN/END callbacks. Each
	 * > slice between those (whether an incremental or the sole non-incremental
	 * > slice) is bracketed by GC_SLICE_BEGIN/GC_SLICE_END.
	 * Thus, to safely monitor GCs, we need to profile SLICE_X calls.
	 */


	if (progress == JS::GC_SLICE_BEGIN)
	{
		if (CProfileManager::IsInitialised() && Threading::IsMainThread())
			g_Profiler.Start("GCSlice");
		g_Profiler2.RecordRegionEnter("GCSlice");
	}
	else if (progress == JS::GC_SLICE_END)
	{
		if (CProfileManager::IsInitialised() && Threading::IsMainThread())
			g_Profiler.Stop();
		g_Profiler2.RecordRegionLeave();
	}

	// The following code can be used to print some information aobut garbage collection
	// Search for "Nonincremental reason" if there are problems running GC incrementally.
	#if 0
	if (progress == JS::GCProgress::GC_CYCLE_BEGIN)
		printf("starting cycle ===========================================\n");

	//const char16_t* str = desc.formatMessage(cx);
	const char16_t* str = desc.formatSliceMessage(cx);
	int len = 0;

	for(int i = 0; i < 10000; i++)
	{
		len++;
		if(!str[i])
			break;
	}

	wchar_t outstring[len];

	for(int i = 0; i < len; i++)
	{
		outstring[i] = (wchar_t)str[i];
	}

	printf("---------------------------------------\n: %ls \n---------------------------------------\n", outstring);

	const uint32_t gcBytes = JS_GetGCParameter(cx, JSGC_BYTES);

	printf("gcBytes: %i KB\n", gcBytes / 1024);

	if (progress == JS::GCProgress::GC_SLICE_END)
		printf("ending cycle ===========================================\n");
	#endif
}

std::shared_ptr<ScriptContext> ScriptContext::CreateContext(int contextSize, uint32_t heapGrowthBytesGCTrigger)
{
	return std::make_shared<ScriptContext>(contextSize, heapGrowthBytesGCTrigger);
}

ScriptContext::ScriptContext(int contextSize, uint32_t heapGrowthBytesGCTrigger):
	m_JobQueue{std::make_unique<Script::JobQueue>()},
	m_ContextSize{contextSize},
	m_HeapGrowthBytesGCTrigger{heapGrowthBytesGCTrigger}
{
	ENSURE(ScriptEngine::IsInitialised() && "The ScriptEngine must be initialized before constructing any ScriptContexts!");

	m_cx = JS_NewContext(contextSize);
	ENSURE(m_cx); // TODO: error handling

	// Set stack quota limits - JS scripts will stop with a "too much recursion" exception.
	// This seems to refer to the program's actual stack size, so it should be lower than the lowest common denominator
	// of the various stack sizes of supported OS.
	// From SM78's jsapi.h:
	// - "The stack quotas for each kind of code should be monotonically descending"
	// - "This function may only be called immediately after the runtime is initialized
	//   and before any code is executed and/or interrupts requested"
	JS_SetNativeStackQuota(m_cx, 950 * KiB, 900 * KiB, 850 * KiB);

	ENSURE(JS::InitSelfHostedCode(m_cx));

	JS::SetGCSliceCallback(m_cx, GCSliceCallbackHook);

	JS_SetGCParameter(m_cx, JSGC_MAX_BYTES, m_ContextSize);
#if OS_SWITCH
	JS_SetGCParameter(m_cx, JSGC_COMPACTING_ENABLED, false);
#else
	JS_SetGCParameter(m_cx, JSGC_INCREMENTAL_GC_ENABLED, true);
#endif
	JS_SetGCParameter(m_cx, JSGC_PER_ZONE_GC_ENABLED, false);

	// Set a low time budget to avoid lag spikes, but allow any number of last ditch GCs
	// to avoid OOM errors.
	JS_SetGCParameter(m_cx, JSGC_SLICE_TIME_BUDGET_MS, 6);
	JS_SetGCParameter(m_cx, JSGC_MIN_LAST_DITCH_GC_PERIOD, 0);


#if OS_SWITCH
	JS_SetOffthreadIonCompilationEnabled(m_cx, false);
#else
	JS_SetOffthreadIonCompilationEnabled(m_cx, true);
#endif

	// For GC debugging:
	// JS_SetGCZeal(m_cx, 2, JS_DEFAULT_ZEAL_FREQ);

	JS_SetContextPrivate(m_cx, nullptr);

#if OS_SWITCH
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_ION_ENABLE, 0);
	LOGMESSAGE("SWITCHGC jit mode: Ion=0 Baseline=1 (bisection step 1)");
#else
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_ION_ENABLE, 1);
#endif
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_BASELINE_ENABLE, 1);

	// Turn off Spectre mitigations - this is a huge speedup on JS code, particularly JS -> C++ calls.
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_SPECTRE_JIT_TO_CXX_CALLS, 0);
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_SPECTRE_INDEX_MASKING, 0);
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_SPECTRE_VALUE_MASKING, 0);
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_SPECTRE_STRING_MITIGATIONS, 0);
	JS_SetGlobalJitCompilerOption(m_cx, JSJITCOMPILER_SPECTRE_OBJECT_MITIGATIONS, 0);

	// Workaround to turn off nursery size heuristic.
	// See https://gitea.wildfiregames.com/0ad/0ad/issues/7714 for details.
	js::gc::SetPerformanceHint(m_cx, js::gc::PerformanceHint::InPageLoad);

	ScriptEngine::GetSingleton().RegisterContext(m_cx);

	JS::SetJobQueue(m_cx, m_JobQueue.get());
	JS::SetPromiseRejectionTrackerCallback(m_cx, &Script::UnhandledRejectedPromise);

	JSRuntime* runtime{JS_GetRuntime(m_cx)};
	JS::SetModuleMetadataHook(runtime, &Script::ModuleLoader::MetadataHook);
	JS::SetModuleResolveHook(runtime, &Script::ModuleLoader::ResolveHook);
	JS::SetModuleDynamicImportHook(runtime, &Script::ModuleLoader::DynamicImportHook);
}

ScriptContext::~ScriptContext()
{
	ENSURE(ScriptEngine::IsInitialised() && "The ScriptEngine must be active (initialized and not yet shut down) when destroying a ScriptContext!");

	JSRuntime* runtime{JS_GetRuntime(m_cx)};
	JS::SetModuleDynamicImportHook(runtime, nullptr);
	JS::SetModuleResolveHook(runtime, nullptr);
	JS::SetModuleMetadataHook(runtime, nullptr);

	// Switch back to normal performance mode to avoid assertion in debug mode.
	js::gc::SetPerformanceHint(m_cx, js::gc::PerformanceHint::Normal);

	JS_DestroyContext(m_cx);
	ScriptEngine::GetSingleton().UnRegisterContext(m_cx);
}

void ScriptContext::RegisterRealm(JS::Realm* realm)
{
	ENSURE(realm);
	m_Realms.push_back(realm);
}

void ScriptContext::UnRegisterRealm(JS::Realm* realm)
{
	// Schedule the zone for GC, which will destroy the realm.
	if (JS::IsIncrementalGCInProgress(m_cx))
		JS::FinishIncrementalGC(m_cx, JS::GCReason::API);
	JS::PrepareZoneForGC(m_cx, js::GetRealmZone(realm));
	m_Realms.remove(realm);
}

#define GC_DEBUG_PRINT 0
void ScriptContext::MaybeIncrementalGC()
{
	PROFILE2("MaybeIncrementalGC");

	if (!JS::IsIncrementalGCEnabled(m_cx))
		return;

	// The idea is to get the heap size after a completed GC and trigger the next GC
	// when the heap size has reached m_LastGCBytes + X.
	// Spidermonkey allocates memory arenas of 4KB for JS heap data.
	// At the end of a GC, any such arena that became empty is freed.
	// On shrinking GCs, spidermonkey further defragments the arenas, which effectively frees more memory but costs time.
	// In practice, shrinking GCs also dump JITted code and the defragmentation is not worth it for 0 A.D.
	// The regular GCs also free quite a bit of memory anyways, and non-full arenas get used for new objects.

	const uint32_t gcBytes = JS_GetGCParameter(m_cx, JSGC_BYTES);

#if GC_DEBUG_PRINT
	printf("gcBytes: %i KB, last of %i KB\n", gcBytes / 1024, m_LastGCBytes / 1024);
#endif

	// The memory freeing happens mostly in the background, so we can't rely on the value on the last incremental slice.
	// To fix that, just remember a 'minimum' value.
	if (m_LastGCBytes > gcBytes || m_LastGCBytes == 0)
	{
#if GC_DEBUG_PRINT
		printf("Setting m_LastGCBytes: %d KB \n", gcBytes / 1024);
#endif
		m_LastGCBytes = gcBytes;
	}

	// Run an additional incremental GC slice if the currently running incremental GC isn't over yet
	// ... or
	// start a new incremental GC if the JS heap size has grown enough for a GC to make sense
	if (JS::IsIncrementalGCInProgress(m_cx) || (gcBytes - m_LastGCBytes > m_HeapGrowthBytesGCTrigger))
	{
#if GC_DEBUG_PRINT
		if (JS::IsIncrementalGCInProgress(m_cx))
			printf("An incremental GC cycle is in progress. \n");
		else
			printf("GC needed because JSGC_BYTES - m_LastGCBytes > m_HeapGrowthBytesGCTrigger \n"
				"    JSGC_BYTES: %d KB \n    m_LastGCBytes: %d KB \n    m_HeapGrowthBytesGCTrigger: %d KB \n",
				gcBytes / 1024,
				m_LastGCBytes / 1024,
				m_HeapGrowthBytesGCTrigger / 1024);
#endif

#if GC_DEBUG_PRINT
		if (!JS::IsIncrementalGCInProgress(m_cx))
			printf("Starting incremental GC \n");
		else
			printf("Running incremental GC slice \n");
#endif

		// There is a tradeoff between this time and the number of frames we must run GCs on, but overall we should prioritize smooth framerates.
		const js::SliceBudget GCSliceTimeBudget = js::SliceBudget(js::TimeBudget(6)); // Milliseconds an incremental slice is allowed to run. SM respects this fairly well.

		PrepareZonesForIncrementalGC();
		if (!JS::IsIncrementalGCInProgress(m_cx))
			JS::StartIncrementalGC(m_cx, JS::GCOptions::Normal, JS::GCReason::API, GCSliceTimeBudget);
		else
			JS::IncrementalGCSlice(m_cx, JS::GCReason::API, GCSliceTimeBudget);

		// Reset this here so that the minimum gets cleared.
		m_LastGCBytes = gcBytes;
	}
}

void ScriptContext::ShrinkingGC()
{
	JS_SetGCParameter(m_cx, JSGC_INCREMENTAL_GC_ENABLED, false);
	JS_SetGCParameter(m_cx, JSGC_PER_ZONE_GC_ENABLED, true);
	JS::PrepareForFullGC(m_cx);
#if OS_SWITCH
	JS::NonIncrementalGC(m_cx, JS::GCOptions::Normal, JS::GCReason::API);
#else
	JS::NonIncrementalGC(m_cx, JS::GCOptions::Shrink, JS::GCReason::API);
#endif
#if OS_SWITCH
	JS_SetGCParameter(m_cx, JSGC_INCREMENTAL_GC_ENABLED, false);
#else
	JS_SetGCParameter(m_cx, JSGC_INCREMENTAL_GC_ENABLED, true);
#endif
	JS_SetGCParameter(m_cx, JSGC_PER_ZONE_GC_ENABLED, false);
}

void ScriptContext::RunJobs()
{
	m_JobQueue->runJobs(m_cx);
}

void ScriptContext::PrepareZonesForIncrementalGC() const
{
	for (JS::Realm* const& realm : m_Realms)
		JS::PrepareZoneForGC(m_cx, js::GetRealmZone(realm));
}
