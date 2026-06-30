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

#include "LocalTurnManager.h"

#include "lib/debug.h"
#include "simulation2/system/TurnManager.h"

#include <js/RootingAPI.h>

class CSimulation2;
class IReplayLogger;

CLocalTurnManager::CLocalTurnManager(CSimulation2& simulation, IReplayLogger& replay)
	: CTurnManager(simulation, DEFAULT_TURN_LENGTH, COMMAND_DELAY_SP, 0, replay)
{
}

void CLocalTurnManager::PostCommand(player_id_t playerid, JS::HandleValue data)
{
	AddCommand(m_ClientId, playerid, data, m_CurrentTurn + m_CommandDelay);
}

void CLocalTurnManager::PostCommand(JS::HandleValue data)
{
	AddCommand(m_ClientId, m_PlayerId, data, m_CurrentTurn + m_CommandDelay);
}

void CLocalTurnManager::NotifyFinishedOwnCommands(u32 turn)
{
	FinishedAllCommands(turn, m_TurnLength);
}

void CLocalTurnManager::NotifyFinishedUpdate(u32 /*turn*/)
{
#if 0 // this hurts performance and is only useful for verifying log replays
	std::string hash;
	{
		PROFILE3("state hash check");
		ENSURE(m_Simulation2.ComputeStateHash(hash));
	}
	m_Replay.Hash(hash);
#endif
}

void CLocalTurnManager::OnSimulationMessage(CSimulationMessage*)
{
	debug_warn(L"This should never be called");
}
