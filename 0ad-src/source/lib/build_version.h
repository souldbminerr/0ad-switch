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

#ifndef INCLUDED_BUILDVERSION
#define INCLUDED_BUILDVERSION

/*
 * The version of the game is built as MAJOR.MINOR.PATCH, where
 * - MAJOR is 0
 * - MINOR is incremented for each main release
 * - PATCH is incremented whenever we release a bugfix patch release
 *
 * TODO: This does not respect semver.
 */
#define PS_VERSION_MAJOR 0
#define PS_VERSION_MINOR 28
#define PS_VERSION_PATCH 0

/*
 * When a patch version is released, it should stay simulation-compatible with
 * all the previous patch releases of the same main release. This allows players
 * to keep replaying their games and playing online against other versions of the
 * same main release.
 *
 * The "compatible patch version" is the earliest patch version with which the
 * current version is compatible. It should ideally stay at 0 all the time.
 * If the compatible patch version is bumped:
 * - a new lobby room must be opened
 * - the version of the public '0ad' mod must be bumped
 * - incompatible replays and savegames will be rotated as new folders are created
 *
 * This does not describe compatibility with the modding API. Modders are advised
 * to rely on the actual engine version.
 */
#define PS_SERIALIZATION_COMPATIBLE_PATCH 0

#define STR(ver) #ver
#define DOTCONCAT(v1, v2, v3) STR(v1) "." STR(v2) "." STR(v3)

// See definition of PS_VERSION_* for details
#define PS_VERSION DOTCONCAT(PS_VERSION_MAJOR, PS_VERSION_MINOR, PS_VERSION_PATCH)
// Used in Windows .rc file
#define PS_VERSION_WORD PS_VERSION_MAJOR,PS_VERSION_MINOR,PS_VERSION_PATCH,0
// See definition of PS_SERIALIZATION_COMPATIBLE_PATCH for details
#define PS_SERIALIZATION_VERSION DOTCONCAT(PS_VERSION_MAJOR, PS_VERSION_MINOR, PS_SERIALIZATION_COMPATIBLE_PATCH)

extern wchar_t build_version[];

#endif // INCLUDED_BUILDVERSION
