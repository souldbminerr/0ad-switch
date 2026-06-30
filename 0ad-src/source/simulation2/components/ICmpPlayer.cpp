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

#include "ICmpPlayer.h"

#include "graphics/Color.h"
#include "maths/FixedVector3D.h"
#include "simulation2/scripting/ScriptComponent.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/InterfaceScripted.h"
#include "simulation2/system/Message.h"

BEGIN_INTERFACE_WRAPPER(Player)
END_INTERFACE_WRAPPER(Player)

class CCmpPlayerScripted : public ICmpPlayer
{
public:
	DEFAULT_SCRIPT_WRAPPER_BASIC(PlayerScripted)

	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_PlayerWon);
		componentManager.SubscribeToMessageType(MT_PlayerDefeated);
	}

	void Serialize(ISerializer& serialize) final
	{
		serialize.Bool("isActive", m_IsActive);
		m_Script.Serialize(serialize);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) final
	{
		deserialize.Bool("isActive", m_IsActive);
		m_Script.Deserialize(GetSimContext().GetComponentManager(), paramNode, deserialize, GetEntityId());
	}

	void HandleMessage(const CMessage& msg, bool global) final
	{
		const int msgType{msg.GetType()};

		// Handle messages that were subscribed to in ClassInit.
		if (msgType == MT_PlayerWon || msgType == MT_PlayerDefeated)
		{
			m_IsActive = false;
			if (!m_Script.HasMessageHandler(msg, global))
				return;
		}

		// Handle messages that were subscribed to within the JS implementation of the interface.
		m_Script.HandleMessage(msg, global);
	}

	CColor GetDisplayedColor() override
	{
		return m_Script.Call<CColor>("GetDisplayedColor");
	}

	CFixedVector3D GetStartingCameraPos() override
	{
		return m_Script.Call<CFixedVector3D>("GetStartingCameraPos");
	}

	CFixedVector3D GetStartingCameraRot() override
	{
		return m_Script.Call<CFixedVector3D>("GetStartingCameraRot");
	}

	bool HasStartingCamera() override
	{
		return m_Script.Call<bool>("HasStartingCamera");
	}

	std::string GetState() override
	{
		return m_Script.Call<std::string>("GetState");
	}

	bool IsRemoved() override
	{
		return m_Script.Call<bool>("IsRemoved");
	}

	bool IsActive() final
	{
		return m_IsActive;
	}

private:
	// Serialize this player state variable in C++ so that mods can't manipulate this value in order to
	// reveal the map locally.
	// Once it's set to `false` it's never set to true again. To prevent mods from temporarily changing
	// it.
	bool m_IsActive{true};
};

REGISTER_COMPONENT_SCRIPT_WRAPPER(PlayerScripted)
