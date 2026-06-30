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

#ifndef INCLUDED_MAPGENERATOR
#define INCLUDED_MAPGENERATOR

#include "lib/file/vfs/vfs_path.h"
#include "lib/types.h"
#include "scriptinterface/StructuredClone.h"

#include <atomic>
#include <js/PropertyDescriptor.h>
#include <string>
#include <string_view>

class ScriptInterface;
class StopToken;

constexpr std::wstring_view RANDOM_MAP_PREFIX{L"maps/random/"};

/**
 * Generate the map. This does take a long time.
 *
 * @param st request to fastly stop the function. The returned value is
 *	unspecified.
 * @param progress Destination to write the function progress to. You must not
 *	write to it while `RunMapGenerationScript` is running.
 * @param script The VFS path for the script, e.g. "maps/random/latium.js".
 * @param settings JSON string containing settings for the map generator.
 * @param flags With thous flags the engine functions get registered
 *	`g_MapSettings` also respects this flags.
 * @return If there is an error `nullptr` is returned. Otherwise random map
 *	data, according to this format:
 *	https://gitea.wildfiregames.com/0ad/0ad/wiki/Random_Map_Generator_Internals#Dataformat
 */
Script::StructuredClone RunMapGenerationScript(const StopToken stopToken, std::atomic<int>& progress,
	ScriptInterface& scriptInterface, const VfsPath& script, const std::string& settings,
	const u16 flags = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT);

#endif	//INCLUDED_MAPGENERATOR
