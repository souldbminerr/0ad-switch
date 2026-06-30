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

#include "MapGenerator.h"

#include "graphics/MapIO.h"
#include "graphics/Patch.h"
#include "graphics/Terrain.h"
#include "lib/code_annotation.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/file/vfs/vfs_util.h"
#include "lib/path.h"
#include "lib/posix/posix_types.h"
#include "lib/status.h"
#include "lib/utf8.h"
#include "maths/MathUtil.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/FileIo.h"
#include "ps/Filesystem.h"
#include "ps/Future.h"
#include "ps/TemplateLoader.h"
#include "ps/scripting/JSInterface_VFS.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/ModuleLoader.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptContext.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "simulation2/helpers/MapEdgeTiles.h"
#include "simulation2/system/Component.h"

#include <boost/random/linear_congruential.hpp>
#include <cstddef>
#include <fmt/format.h>
#include <js/Interrupt.h>
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

struct JSContext;

namespace
{
constexpr const char* GENERATOR_NAME{"generateMap"};

bool MapGenerationInterruptCallback(JSContext* cx);

/**
 * Provides callback's for the JavaScript.
 */
class CMapGenerationCallbacks
{
public:
	// Only the constructor and the destructor are called by C++.

	CMapGenerationCallbacks(const StopToken stopToken, ScriptInterface& scriptInterface,
		const u16 flags) :
		m_StopToken{stopToken},
		m_ScriptInterface{scriptInterface}
	{
		m_ScriptInterface.SetCallbackData(static_cast<void*>(this));

		// Enable the script to be aborted
		JS_AddInterruptCallback(m_ScriptInterface.GetGeneralJSContext(),
			&MapGenerationInterruptCallback);

		// Set initial seed, callback data.
		// Expose functions, globals and classes relevant to the map scripts.
#define REGISTER_MAPGEN_FUNC(func) \
	ScriptFunction::Register<&CMapGenerationCallbacks::func, \
		ScriptInterface::ObjectFromCBData<CMapGenerationCallbacks>>(rq, #func, flags);

		// VFS
		JSI_VFS::RegisterScriptFunctions_ReadOnlySimulationMaps(m_ScriptInterface, flags);

		// Globalscripts may use VFS script functions
		m_ScriptInterface.LoadGlobalScripts();

		// File loading
		ScriptRequest rq(m_ScriptInterface);
		REGISTER_MAPGEN_FUNC(LoadLibrary);
		REGISTER_MAPGEN_FUNC(LoadHeightmapImage);
		REGISTER_MAPGEN_FUNC(LoadMapTerrain);

		// Template functions
		REGISTER_MAPGEN_FUNC(GetTemplate);
		REGISTER_MAPGEN_FUNC(TemplateExists);
		REGISTER_MAPGEN_FUNC(FindTemplates);
		REGISTER_MAPGEN_FUNC(FindActorTemplates);

		// Profiling
		REGISTER_MAPGEN_FUNC(GetMicroseconds);

		// Engine constants

		// Length of one tile of the terrain grid in metres.
		// Useful to transform footprint sizes to the tilegrid coordinate system.
		m_ScriptInterface.SetGlobal("TERRAIN_TILE_SIZE", static_cast<int>(TERRAIN_TILE_SIZE));

		// Number of impassable tiles at the map border
		m_ScriptInterface.SetGlobal("MAP_BORDER_WIDTH", static_cast<int>(MAP_EDGE_TILES));

#undef REGISTER_MAPGEN_FUNC
	}

	~CMapGenerationCallbacks()
	{
		JS_AddInterruptCallback(m_ScriptInterface.GetGeneralJSContext(), nullptr);
		m_ScriptInterface.SetCallbackData(nullptr);
	}

	StopToken m_StopToken;

private:

	// These functions are called by JS.

	/**
	 * Load all scripts of the given library
	 *
	 * @param libraryName VfsPath specifying name of the library (subfolder of ../maps/random/)
	 * @return true if all scripts ran successfully, false if there's an error
	 */
	bool LoadLibrary(const VfsPath& libraryName)
	{
		// Ignore libraries that are already loaded
		if (m_LoadedLibraries.find(libraryName) != m_LoadedLibraries.end())
			return true;

		// Mark this as loaded, to prevent it recursively loading itself
		m_LoadedLibraries.insert(libraryName);

		VfsPath path = VfsPath(L"maps/random/") / libraryName / VfsPath();
		VfsPaths pathnames;

		// Load all scripts in mapgen directory
		Status ret = vfs::GetPathnames(g_VFS, path, L"*.js", pathnames);
		if (ret == INFO::OK)
		{
			for (const VfsPath& p : pathnames)
			{
				LOGMESSAGE("Loading map generator script '%s'", p.string8());

				if (!m_ScriptInterface.LoadGlobalScriptFile(p))
				{
					LOGERROR("CMapGenerationCallbacks::LoadScripts: Failed to load script '%s'",
						p.string8());
					return false;
				}
			}
		}
		else
		{
			// Some error reading directory
			wchar_t error[200];
			LOGERROR(
				"CMapGenerationCallbacks::LoadScripts: Error reading scripts in directory '%s': %s",
				path.string8(),
				utf8_from_wstring(StatusDescription(ret, error, ARRAY_SIZE(error))));
			return false;
		}

		return true;
	}

	/**
	 * Load an image file and return it as a height array.
	 */
	JS::Value LoadHeightmapImage(const VfsPath& filename)
	{
		std::vector<u16> heightmap;
		if (LoadHeightmapImageVfs(filename, heightmap) != INFO::OK)
		{
			LOGERROR("Could not load heightmap file '%s'", filename.string8());
			return JS::UndefinedValue();
		}

		ScriptRequest rq(m_ScriptInterface);
		JS::RootedValue returnValue(rq.cx);
		Script::ToJSVal(rq, &returnValue, heightmap);
		return returnValue;
	}

	/**
	 * Load an Atlas terrain file (PMP) returning textures and heightmap.
	 *
	 * See CMapReader::UnpackTerrain, CMapReader::ParseTerrain for the reordering
	 */
	JS::Value LoadMapTerrain(const VfsPath& filename)
	{
		ScriptRequest rq(m_ScriptInterface);

		if (!VfsFileExists(filename))
		{
			throw std::runtime_error{fmt::format("Terrain file \"{}\" does not exist!",
				filename.string8().c_str())};
		}

		CFileUnpacker unpacker;
		unpacker.Read(filename, "PSMP");

		if (unpacker.GetVersion() < CMapIO::FILE_READ_VERSION)
		{
			throw std::runtime_error{fmt::format(
				"Could not load terrain file \"{}\" too old version!", filename.string8().c_str())};
		}

		// unpack size
		ssize_t patchesPerSide = (ssize_t)unpacker.UnpackSize();
		size_t verticesPerSide = patchesPerSide * PATCH_SIZE + 1;

		// unpack heightmap
		std::vector<u16> heightmap;
		heightmap.resize(SQR(verticesPerSide));
		unpacker.UnpackRaw(&heightmap[0], SQR(verticesPerSide) * sizeof(u16));

		// unpack texture names
		size_t textureCount = unpacker.UnpackSize();
		std::vector<std::string> textureNames;
		textureNames.reserve(textureCount);
		for (size_t i = 0; i < textureCount; ++i)
		{
			CStr texturename;
			unpacker.UnpackString(texturename);
			textureNames.push_back(texturename);
		}

		// unpack texture IDs per tile
		ssize_t tilesPerSide = patchesPerSide * PATCH_SIZE;
		std::vector<CMapIO::STileDesc> tiles;
		tiles.resize(size_t(SQR(tilesPerSide)));
		unpacker.UnpackRaw(&tiles[0], sizeof(CMapIO::STileDesc) * tiles.size());

		// reorder by patches and store and save texture IDs per tile
		std::vector<u16> textureIDs;
		for (ssize_t x = 0; x < tilesPerSide; ++x)
		{
			size_t patchX = x / PATCH_SIZE;
			size_t offX = x % PATCH_SIZE;
			for (ssize_t y = 0; y < tilesPerSide; ++y)
			{
				size_t patchY = y / PATCH_SIZE;
				size_t offY = y % PATCH_SIZE;
				// m_Priority and m_Tex2Index unused
				textureIDs.push_back(tiles[(patchY * patchesPerSide + patchX) * SQR(PATCH_SIZE) +
					(offY * PATCH_SIZE + offX)].m_Tex1Index);
			}
		}

		JS::RootedValue returnValue(rq.cx);

		Script::CreateObject(
			rq,
			&returnValue,
			"height", heightmap,
			"textureNames", textureNames,
			"textureIDs", textureIDs);

		return returnValue;
	}

	/**
	 * Microseconds since the epoch.
	 */
	double GetMicroseconds() const
	{
		return JS_Now();
	}

	/**
	 * Return the template data of the given template name.
	 */
	CParamNode GetTemplate(const std::string& templateName)
	{
		const CParamNode& templateRoot =
			m_TemplateLoader.GetTemplateFileData(templateName).GetOnlyChild();
		if (!templateRoot.IsOk())
			LOGERROR("Invalid template found for '%s'", templateName.c_str());

		return templateRoot;
	}

	/**
	 * Check whether the given template exists.
	 */
	bool TemplateExists(const std::string& templateName) const
	{
		return m_TemplateLoader.TemplateExists(templateName);
	}

	/**
	 * Returns all template names of simulation entity templates.
	 */
	std::vector<std::string> FindTemplates(const std::string& path, bool includeSubdirectories)
	{
		return m_TemplateLoader.FindTemplates(path, includeSubdirectories, SIMULATION_TEMPLATES);
	}

	/**
	 * Returns all template names of actors.
	 */
	std::vector<std::string> FindActorTemplates(const std::string& path, bool includeSubdirectories)
	{
		return m_TemplateLoader.FindTemplates(path, includeSubdirectories, ACTOR_TEMPLATES);
	}

	/**
	 * Provides the script context.
	 */
	ScriptInterface& m_ScriptInterface;

	/**
	 * Currently loaded script librarynames.
	 */
	std::set<VfsPath> m_LoadedLibraries;

	/**
	 * Backend to loading template data.
	 */
	CTemplateLoader m_TemplateLoader;
};

bool MapGenerationInterruptCallback(JSContext* cx)
{
	return !ScriptInterface::ObjectFromCBData<CMapGenerationCallbacks>(
		ScriptInterface::CmptPrivate::GetScriptInterface(cx))->m_StopToken.IsStopRequested();
}
} // anonymous namespace

Script::StructuredClone RunMapGenerationScript(const StopToken stopToken, std::atomic<int>& progress,
	ScriptInterface& scriptInterface, const VfsPath& script, const std::string& settings, const u16 flags)
{
	ScriptRequest rq(scriptInterface);

	// Parse settings
	JS::RootedValue settingsVal(rq.cx);
	if (!Script::ParseJSON(rq, settings, &settingsVal) && settingsVal.isUndefined())
	{
		LOGERROR("RunMapGenerationScript: Failed to parse settings");
		return nullptr;
	}

	// Prevent unintentional modifications to the settings object by random map scripts
	if (!Script::DeepFreezeObject(rq, settingsVal))
	{
		LOGERROR("RunMapGenerationScript: Failed to deepfreeze settings");
		return nullptr;
	}

	// Init RNG seed
	u32 seed = 0;
	if (!Script::HasProperty(rq, settingsVal, "Seed") ||
		!Script::GetProperty(rq, settingsVal, "Seed", seed))
		LOGWARNING("RunMapGenerationScript: No seed value specified - using 0");

	boost::rand48 mapGenRNG{seed};
	scriptInterface.ReplaceNondeterministicRNG(mapGenRNG);

	CMapGenerationCallbacks callbackData{stopToken, scriptInterface, flags};

	// Copy settings to global variable
	JS::RootedValue global(rq.cx, rq.globalValue());
	if (!Script::SetProperty(rq, global, "g_MapSettings", settingsVal, flags & JSPROP_READONLY,
		flags & JSPROP_ENUMERATE))
	{
		LOGERROR("RunMapGenerationScript: Failed to define g_MapSettings");
		return nullptr;
	}

	// Load RMS
	LOGMESSAGE("Loading RMS '%s'", script.string8());
	auto compileResult = scriptInterface.GetModuleLoader().LoadModule(rq, script);
	scriptInterface.GetContext().RunJobs();
	auto& future = *compileResult.begin();
	if (!future.IsDone())
		throw std::runtime_error{fmt::format("Loading module {:?} takes too long.", script.string8())};

	JS::RootedObject nsAsObject{rq.cx, future.Get()};

	{
		bool hasGenerator;
		if (!JS_HasProperty(rq.cx, nsAsObject, GENERATOR_NAME, &hasGenerator))
		{
			LOGERROR("RunMapGenerationScript: failed to search `%s` in the module-namespace.",
				GENERATOR_NAME);
			return nullptr;
		}

		if (!hasGenerator)
		{
			throw std::runtime_error{fmt::format(
				"The map generation script {:?} didn't export a {:?}.", script.string8(),
				GENERATOR_NAME)};
		}
	}

	LOGMESSAGE("Run RMS generator");
	JS::RootedValue ns{rq.cx, JS::ObjectValue(*nsAsObject)};
	JS::RootedValue map{rq.cx, ScriptFunction::RunGenerator(rq, ns, GENERATOR_NAME, settingsVal,
		[&](const JS::HandleValue value)
		{
			// When the task is started, `progress` is only mutated by this thread.
			const int currentProgress{progress.load()};
			int tempProgress;
			if (!Script::FromJSVal(rq, value, tempProgress))
				throw std::runtime_error{"Failed to convert the yielded value to an "
					"integer."};
			if (tempProgress < currentProgress)
			{
				LOGWARNING("The random map script tried to reduce the loading progress from "
					"%d to %d.", currentProgress, tempProgress);
				return;
			}
			progress.store(tempProgress);
		})};

	JS::RootedValue exportedMap{rq.cx};
	const bool exportSuccess{ScriptFunction::Call(rq, map, "MakeExportable", &exportedMap)};
	return Script::WriteStructuredClone(rq, exportSuccess ? exportedMap : map);
}
