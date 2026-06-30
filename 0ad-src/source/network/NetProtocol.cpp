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

#include "NetProtocol.h"

#include "ps/CStr.h"

#include <algorithm>
#include <cstdint>

namespace
{
template <typename ModType>
std::string GetModName(const ModType& mod)
{
	return mod.m_Name + "-" + mod.m_Version;
}
}

std::optional<HandshakeError> CheckHandshake(const CSrvHandshakeMessage& serverMessage, const CCliHandshakeMessage& clientMessage)
{
	if (serverMessage.m_EngineVersion != clientMessage.m_EngineVersion)
		return HandshakeError{"engine", clientMessage.m_EngineVersion, serverMessage.m_EngineVersion};

	const auto& serverMods = serverMessage.m_EnabledMods;
	const auto& clientMods = clientMessage.m_EnabledMods;

	for (uint32_t i = 0; i < std::max(clientMods.size(), serverMods.size()); ++i)
	{
		if (i >= serverMods.size())
			return HandshakeError{"mod", GetModName(clientMods[i]), ""};

		if (i >= clientMods.size())
			return HandshakeError{"mod", "", GetModName(serverMods[i])};

		const std::string serverMod = GetModName(serverMods[i]);
		const std::string clientMod = GetModName(clientMods[i]);

		// Client and server have different mods enabled, or mods enabled in a different order
		if (clientMod != serverMod)
			return HandshakeError{"mod", clientMod, serverMod};
	}

	return {};
}
