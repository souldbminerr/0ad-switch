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
 * platform-independent high resolution timer
 */

#ifndef INCLUDED_TIMER
#define INCLUDED_TIMER

#include "lib/code_annotation.h"
#include "lib/config2.h"	// CONFIG2_TIMER_ALLOW_RDTSC
#include "lib/debug.h"
#include "lib/sysdep/arch.h"

#include <cstdint>
#include <string>

#if ARCH_X86_X64 && CONFIG2_TIMER_ALLOW_RDTSC
# include "lib/sysdep/os_cpu.h"	// os_cpu_ClockFrequency
# include "lib/sysdep/arch/x86_x64/x86_x64.h"	// x86_x64::rdtsc
#endif

/**
 * timer_Time will subsequently return values relative to the current time.
 **/
void timer_Init();

/**
 * @return high resolution (> 1 us) timestamp [s].
 **/
double timer_Time();

/**
 * @return resolution [s] of the timer.
 **/
double timer_Resolution();


// (allow using XADD (faster than CMPXCHG) in 64-bit builds without casting)
#if ARCH_AMD64
typedef intptr_t Cycles;
#else
typedef i64 Cycles;
#endif

/**
 * internal helper functions for returning an easily readable
 * string (i.e. re-scaled to appropriate units)
 **/
std::string StringForSeconds(double seconds);
std::string StringForCycles(Cycles cycles);

//-----------------------------------------------------------------------------
// cumulative timer API

// this supplements in-game profiling by providing low-overhead,
// high resolution time accounting of specific areas.

// since TIMER_ACCRUE et al. are called so often, we try to keep
// overhead to an absolute minimum. storing raw tick counts (e.g. CPU cycles
// returned by x86_x64::rdtsc) instead of absolute time has two benefits:
// - no need to convert from raw->time on every call
//   (instead, it's only done once when displaying the totals)
// - possibly less overhead to querying the time itself
//   (timer_Time may be using slower time sources with ~3us overhead)
//
// however, the cycle count is not necessarily a measure of wall-clock time
// (see http://www.gamedev.net/reference/programming/features/timing).
// therefore, on systems with SpeedStep active, measurements of I/O or other
// non-CPU bound activity may be skewed. this is ok because the timer is
// only used for profiling; just be aware of the issue.
// if this is a problem, disable CONFIG2_TIMER_ALLOW_RDTSC.
//
// note that overflow isn't an issue either way (63 bit cycle counts
// at 10 GHz cover intervals of 29 years).

#if ARCH_X86_X64 && CONFIG2_TIMER_ALLOW_RDTSC

class TimerUnit
{
public:
	void SetToZero()
	{
		m_cycles = 0;
	}

	void SetFromTimer()
	{
		m_cycles = x86_x64::rdtsc();
	}

	void AddDifference(TimerUnit t0, TimerUnit t1)
	{
		m_cycles += t1.m_cycles - t0.m_cycles;
	}

	void Subtract(TimerUnit t)
	{
		m_cycles -= t.m_cycles;
	}

	std::string ToString() const
	{
		ENSURE(m_cycles >= 0);
		return StringForCycles(m_cycles);
	}

	double ToSeconds() const
	{
		return (double)m_cycles / os_cpu_ClockFrequency();
	}

private:
	Cycles m_cycles;
};

#else

class TimerUnit
{
public:
	void SetToZero()
	{
		m_seconds = 0.0;
	}

	void SetFromTimer()
	{
		m_seconds = timer_Time();
	}

	void AddDifference(TimerUnit t0, TimerUnit t1)
	{
		m_seconds += t1.m_seconds - t0.m_seconds;
	}

	void Subtract(TimerUnit t)
	{
		m_seconds -= t.m_seconds;
	}

	std::string ToString() const
	{
		ENSURE(m_seconds >= 0.0);
		return StringForSeconds(m_seconds);
	}

	double ToSeconds() const
	{
		return m_seconds;
	}

private:
	double m_seconds;
};

#endif

// opaque - do not access its fields!
// note: must be defined here because clients instantiate them;
// fields cannot be made private due to POD requirement.
struct TimerClient
{
	TimerUnit sum;	// total bill

	// only store a pointer for efficiency.
	const wchar_t* description;

	TimerClient* next;

	// how often the timer was billed (helps measure relative
	// performance of something that is done indeterminately often).
	intptr_t num_calls;
};

/**
 * make the given TimerClient (usually instantiated as static data)
 * ready for use. returns its address for TIMER_ADD_CLIENT's convenience.
 * this client's total (which is increased by a BillingPolicy) will be
 * displayed by timer_DisplayClientTotals.
 * notes:
 * - may be called at any time;
 * - always succeeds (there's no fixed limit);
 * - free() is not needed nor possible.
 * - description must remain valid until exit; a string literal is safest.
 **/
TimerClient* timer_AddClient(TimerClient* tc, const wchar_t* description);

/**
 * "allocate" a new TimerClient that will keep track of the total time
 * billed to it, along with a description string. These are displayed when
 * timer_DisplayClientTotals is called.
 * Invoke this at file or function scope; a (static) TimerClient pointer of
 * name \<id\> will be defined, which should be passed to TIMER_ACCRUE.
 **/
#define TIMER_ADD_CLIENT(id)\
	static TimerClient UID__;\
	static TimerClient* id = timer_AddClient(&UID__, WIDEN(#id))

/**
 * display all clients' totals; does not reset them.
 * typically called at exit.
 **/
void timer_DisplayClientTotals();


/// used by TIMER_ACCRUE
class ScopeTimerAccrue
{
	NONCOPYABLE(ScopeTimerAccrue);
public:
	ScopeTimerAccrue(TimerClient* tc)
		: m_tc(tc)
	{
		m_t0.SetFromTimer();
	}

	~ScopeTimerAccrue()
	{
		TimerUnit t1;
		t1.SetFromTimer();
		m_tc->sum.AddDifference(m_t0, t1);
		++m_tc->num_calls;
	}

private:
	TimerUnit m_t0;
	TimerClient* m_tc;
};

/**
 * Measure the time taken to execute code up until end of the current scope;
 * bill it to the given TimerClient object. Can safely be nested.
 * Useful for measuring total time spent in a function or basic block over the
 * entire program.
 * `client' is an identifier registered via TIMER_ADD_CLIENT.
 *
 * Example usage:
 * 	TIMER_ADD_CLIENT(client);
 *
 * 	void func()
 * 	{
 * 		TIMER_ACCRUE(client);
 * 		// code to be measured
 * 	}
 *
 * 	[later or at exit]
 * 	timer_DisplayClientTotals();
 **/
#define TIMER_ACCRUE(client) ScopeTimerAccrue UID__(client)

#endif	// #ifndef INCLUDED_TIMER
