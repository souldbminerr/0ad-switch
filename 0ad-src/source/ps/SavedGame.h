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

#ifndef INCLUDED_SAVEDGAME
#define INCLUDED_SAVEDGAME

#include "lib/status.h"
#include "scriptinterface/StructuredClone.h"

#include <js/Value.h>
#include <optional>
#include <string>

class CSimulation2;
class CStrW;
class ScriptInterface;

/**
 * @file
 * Contains functions for managing saved game archives.
 *
 * A saved game is simply a zip archive with the extension '0adsave'
 * and containing two files:
 * <ul>
 *  <li>metadata.json - JSON data file containing the game metadata</li>
 *	<li>simulation.dat - the serialized simulation state data</li>
 * </ul>
 */

namespace SavedGames
{
	/**
	 * Create new saved game archive with given name and simulation data
	 *
	 * @param name Name to save the game with
	 * @param description A user-given description of the save
	 * @param simulation
	 * @param guiMetadataClone if not NULL, store some UI-related data with the saved game
	 * @return INFO::OK if successfully saved, else an error Status
	 */
	Status Save(const CStrW& name, const CStrW& description, CSimulation2& simulation, const Script::StructuredClone& guiMetadataClone);

	/**
	 * Create new saved game archive with given prefix and simulation data
	 *
	 * @param prefix Create new numbered file starting with this prefix
	 * @param description A user-given description of the save
	 * @param simulation
	 * @param guiMetadataClone if not NULL, store some UI-related data with the saved game
	 * @return INFO::OK if successfully saved, else an error Status
	 */
	Status SavePrefix(const CStrW& prefix, const CStrW& description, CSimulation2& simulation, const Script::StructuredClone& guiMetadataClone);

	struct LoadResult
	{
		// Object containing metadata associated with saved game,
		// parsed from metadata.json inside the archive.
		JS::Value metadata;
		// Serialized simulation state stored as string of bytes,
		// loaded from simulation.dat inside the archive.
		std::string savedState;
	};

	/**
	 * Load saved game archive with the given name
	 *
	 * @param name filename of saved game (without path or extension)
	 * @param scriptInterface
	 * @return An empty `std::optional` if an error ocoured.
	 */
	std::optional<LoadResult> Load(const ScriptInterface& scriptInterface, const std::wstring& name);

	/**
	 * Get list of saved games for GUI script usage
	 *
	 * @param scriptInterface the ScriptInterface in which to create the return data.
	 * @return array of objects containing saved game data
	 */
	JS::Value GetSavedGames(const ScriptInterface& scriptInterface);

	/**
	 * Permanently deletes the saved game archive with the given name
	 *
	 * @param name filename of saved game (without path or extension)
	 * @return true if deletion was successful, or false on error
	 */
	bool DeleteSavedGame(const std::wstring& name);
}

#endif // INCLUDED_SAVEDGAME
