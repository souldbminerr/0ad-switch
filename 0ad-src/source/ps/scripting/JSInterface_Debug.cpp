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

#include "JSInterface_Debug.h"

#include "i18n/L10n.h"
#include "lib/build_version.h"
#include "lib/debug.h"
#include "lib/utf8.h"
#include "scriptinterface/FunctionWrapper.h"

#include <ctime>
#include <jsapi.h>
#include <string>
#include <unicode/locid.h>
#include <unicode/smpdtfmt.h>
#include <unicode/utypes.h>

namespace JSI_Debug
{
/**
 * Microseconds since the epoch.
 */
double GetMicroseconds()
{
	return JS_Now();
}

// Deliberately cause the game to crash.
// Currently implemented via access violation (read of address 0).
// Useful for testing the crashlog/stack trace code.
int Crash()
{
	debug_printf("Crashing at user's request.\n");
	return *(volatile int*)0;
}

void DebugWarn()
{
	debug_warn(L"Warning at user's request.");
}

void DisplayErrorDialog(const std::wstring& msg)
{
	debug_DisplayError(msg.c_str(), DE_NO_DEBUG_INFO, NULL, NULL, NULL, 0, NULL, NULL);
}

// Return the date at which the current executable was compiled.
// - Displayed on main menu screen; tells non-programmers which auto-build
//   they are running. Could also be determined via .EXE file properties,
//   but that's a bit more trouble.
std::wstring GetBuildDate()
{
	UDate buildDate = g_L10n.ParseDateTime(__DATE__, "MMM d yyyy", icu::Locale::getUS());
	return wstring_from_utf8(g_L10n.LocalizeDateTime(buildDate, L10n::Date, icu::SimpleDateFormat::MEDIUM));
}

double GetBuildTimestamp()
{
	UDate buildDate = g_L10n.ParseDateTime(__DATE__ " " __TIME__, "MMM d yyyy HH:mm:ss", icu::Locale::getUS());
	if (buildDate)
		return buildDate / 1000.0;
	return std::time(nullptr);
}

// Return the build version of the current executable.
// - in nightly builds, build version is generated from the git HEAD branch and
//   hash and cached in lib/build_version.cpp. it is useful to know when attempting
//   to reproduce bugs (the main EXE and PDB should be temporarily reverted to
//   that commit so that they match user-submitted crashdumps).
// - this function is used by the JS GUI to display the version in a size-constrained
//   zone. when longerHash is false, the last 5 characters of the git hash are cut off.
std::wstring GetBuildVersion(bool longerHash = false)
{
	std::wstring buildVersion(build_version);
	if (buildVersion == L"custom build")
		return wstring_from_utf8(g_L10n.Translate("custom build"));

	// The hash is 10-char, which is a bit long for the GUI. Reduce it to 5-char by
	// default for the main menu display.
	if (!longerHash && buildVersion.length() > 5)
		return buildVersion.substr(0, buildVersion.length() - 5);

	return buildVersion;
}

void RegisterScriptFunctions(const ScriptRequest& rq)
{
	ScriptFunction::Register<&GetMicroseconds>(rq, "GetMicroseconds");
	ScriptFunction::Register<&Crash>(rq, "Crash");
	ScriptFunction::Register<&DebugWarn>(rq, "DebugWarn");
	ScriptFunction::Register<&DisplayErrorDialog>(rq, "DisplayErrorDialog");
	ScriptFunction::Register<&GetBuildDate>(rq, "GetBuildDate");
	ScriptFunction::Register<&GetBuildTimestamp>(rq, "GetBuildTimestamp");
	ScriptFunction::Register<&GetBuildVersion>(rq, "GetBuildVersion");
}
}
