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
 * type definitions needed for wposix.
 */

#ifndef INCLUDED_WPOSIX_TYPES
#define INCLUDED_WPOSIX_TYPES

#include <cstdint>

//
// <sys/types.h>
//

typedef intptr_t ssize_t;
// prevent wxWidgets from (incompatibly) redefining it
#define HAVE_SSIZE_T

// VC9 defines off_t as long, but we need 64-bit file offsets even in
// 32-bit builds. to avoid conflicts, we have to define _OFF_T_DEFINED,
// which promises _off_t has also been defined. since that's used by
// CRT headers, we have to define it too, but must use the original
// long type to avoid breaking struct stat et al.
typedef __int64 off_t;
typedef long _off_t;
#define _OFF_T_DEFINED


//
// <limits.h>
//

// Win32 MAX_PATH is 260; our number may be a bit more efficient.
#define PATH_MAX 256u

#endif	// #ifndef INCLUDED_WPOSIX_TYPES
