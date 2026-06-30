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

#ifndef NETPROTOCOL_H
#define NETPROTOCOL_H

#include "network/NetMessage.h"
#include "ps/Mod.h"
#include "ps/Pyrogenesis.h"

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

struct HandshakeError
{
	std::string componentType;
	std::string clientComponent;
	std::string serverComponent;
};

template <typename Message>
Message CreateHandshake() {
	Message handshake;

	if constexpr (std::is_same_v<Message, CCliHandshakeMessage>)
		handshake.m_MagicResponse = PS_PROTOCOL_MAGIC_RESPONSE;
	else
		handshake.m_Magic = PS_PROTOCOL_MAGIC;

	handshake.m_ProtocolVersion = PS_PROTOCOL_VERSION;
	handshake.m_EngineVersion = PS_SERIALIZATION_VERSION;

	for (const Mod::ModData* mod : Mod::Instance().GetEnabledModsData())
	{
		if (mod->m_IgnoreInCompatibilityChecks)
			continue;

		typename Message::S_m_EnabledMods enabledMod;
		enabledMod.m_Name = mod->m_Name;
		enabledMod.m_Version = mod->m_Version;
		handshake.m_EnabledMods.push_back(enabledMod);
	}

	return handshake;
}

std::optional<HandshakeError> CheckHandshake(const CSrvHandshakeMessage& serverMessage, const CCliHandshakeMessage& clientMessage);

#endif
