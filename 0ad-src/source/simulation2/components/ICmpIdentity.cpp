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

#include "ICmpIdentity.h"

#include "simulation2/system/InterfaceScripted.h"
#include "simulation2/scripting/ScriptComponent.h"
#include <optional>


BEGIN_INTERFACE_WRAPPER(Identity)
END_INTERFACE_WRAPPER(Identity)

class CCmpIdentityScripted : public ICmpIdentity
{
public:
	DEFAULT_SCRIPT_WRAPPER(IdentityScripted)

	std::string GetSelectionGroupName() override
	{
		return m_Script.Call<std::string>("GetSelectionGroupName");
	}

	std::wstring GetPhenotype() override
	{
		return m_Script.Call<std::wstring>("GetPhenotype");
	}

	std::wstring GetCiv() override
	{
		std::optional<std::wstring> civ{m_Script.Call<std::optional<std::wstring>>("GetCiv")};
		return !civ || !civ.has_value() ? L"" : *civ;
	}
};

REGISTER_COMPONENT_SCRIPT_WRAPPER(IdentityScripted)
