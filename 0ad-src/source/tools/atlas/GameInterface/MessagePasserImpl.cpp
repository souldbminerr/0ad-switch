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

#include "MessagePasserImpl.h"

#include "lib/debug.h"
#include "lib/timer.h"
#include "ps/CLogger.h"
#include "tools/atlas/GameInterface/Handlers/MessageHandler.h"
#include "tools/atlas/GameInterface/Messages.h"
#include "tools/atlas/GameInterface/SharedMemory.h"

#include <map>
#include <string>
#include <utility>

using namespace AtlasMessage;

double last_user_activity = 0.0;

void MessagePasserImpl::Add(IMessage* msg)
{
	ENSURE(msg);
	ENSURE(msg->GetType() == IMessage::Message);

	if (m_Trace)
		debug_printf("%8.3f add message: %s\n", timer_Time(), msg->GetName());

	msgHandlers::const_iterator it = GetMsgHandlers().find(msg->GetName());
	if (it != GetMsgHandlers().end())
	{
		it->second(msg);
	}
	else
	{
		debug_warn(L"Unrecognized message");
		// CLogger might not be initialised, but this error will be sent
		// to the debug output window anyway so people can still see it
		LOGERROR("Unrecognized message (%s)", msg->GetName());
	}
	// Delete the object - we took ownership of it.
	AtlasMessage::ShareableDelete(msg);
}

void MessagePasserImpl::Query(QueryMessage* msg, void(* /*timeoutCallback*/ )())
{
	ENSURE(msg);
	ENSURE(msg->GetType() == IMessage::Query);

	if (m_Trace)
		debug_printf("%8.3f add query: %s\n", timer_Time(), msg->GetName());

	msgHandlers::const_iterator it = GetMsgHandlers().find(msg->GetName());
	if (it != GetMsgHandlers().end())
	{
		it->second(msg);
	}
	else
	{
		debug_warn(L"Unrecognized message");
		// CLogger might not be initialised, but this error will be sent
		// to the debug output window anyway so people can still see it
		LOGERROR("Unrecognized message (%s)", msg->GetName());
	}
}

void MessagePasserImpl::SetTrace(bool t)
{
	m_Trace = t;
}
