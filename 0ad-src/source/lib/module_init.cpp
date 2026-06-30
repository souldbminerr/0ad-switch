/* Copyright (C) 2025 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * helpers for module initialization/shutdown.
 */

#include "precompiled.h"

#include "module_init.h"

#include "lib/debug.h"
#include "lib/sysdep/cpu.h"

// not yet initialized, or already shutdown
static const Status UNINITIALIZED = 0;	// value documented in header
// running user callback - concurrent ModuleInit callers must spin
static const Status BUSY = ERR::AGAIN;	// never returned
// init succeeded; allow shutdown
static const Status INITIALIZED = INFO::SKIPPED;


Status ModuleInit(volatile ModuleInitState* initState, Status (*init)())
{
	for(;;)
	{
		Status expected{ UNINITIALIZED };
		if(initState->compare_exchange_strong(expected, BUSY))
		{
			Status ret = init();
			initState->store(ret == INFO::OK ? INITIALIZED : ret);
			return ret;
		}

		const ModuleInitState latchedInitState = *initState;
		if(latchedInitState == UNINITIALIZED || latchedInitState == BUSY)
		{
			cpu_Pause();
			continue;
		}

		ENSURE(latchedInitState == INITIALIZED || latchedInitState < 0);
		return (Status)latchedInitState;
	}
}


Status ModuleShutdown(volatile ModuleInitState* initState, void (*shutdown)())
{
	for(;;)
	{
		Status expected{ INITIALIZED };
		if(initState->compare_exchange_strong(expected, BUSY))
		{
			shutdown();
			initState->store(UNINITIALIZED);
			return INFO::OK;
		}

		const ModuleInitState latchedInitState = *initState;
		if(latchedInitState == INITIALIZED || latchedInitState == BUSY)
		{
			cpu_Pause();
			continue;
		}

		if(latchedInitState == UNINITIALIZED)
			return INFO::SKIPPED;

		ENSURE(latchedInitState < 0);
		return (Status)latchedInitState;
	}
}
