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
 * CPU and memory detection.
 */

#ifndef INCLUDED_CPU
#define INCLUDED_CPU

#include "lib/status.h"
#include "lib/sysdep/arch.h"
#include "lib/sysdep/compiler.h"

namespace ERR
{
	const Status CPU_FEATURE_MISSING     = -130000;
	const Status CPU_UNKNOWN_OPCODE      = -130001;
	const Status CPU_UNKNOWN_VENDOR      = -130002;

}


//-----------------------------------------------------------------------------
// CPU detection

/**
 * @return string identifying the CPU (usually a cleaned-up version of the
 * brand string)
 **/
const char* cpu_IdentifierString();


/**
 * pause in spin-wait loops, as a performance optimisation.
 **/
inline void cpu_Pause()
{
#if MSC_VERSION && ARCH_X86_X64
	_mm_pause();
#elif GCC_VERSION && ARCH_X86_X64
	__asm__ __volatile__( "rep; nop" : : : "memory" );
#endif
}

#endif	// #ifndef INCLUDED_CPU
