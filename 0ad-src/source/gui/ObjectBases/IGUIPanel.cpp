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

#include "IGUIPanel.h"

#include "gui/CGUI.h"
#include "gui/CGUISetting.h"
#include "ps/CStr.h"

#include <map>
#include <string>
#include <vector>

IGUIPanel::IGUIPanel(CGUI& pGUI)
	: IGUIObject(pGUI)
{
}

IGUIPanel::~IGUIPanel()
{
}

bool IGUIPanel::IsMouseOver() const
{
	return m_CachedLayoutActualSize.PointInside(m_pGUI.GetMousePos());
}

void IGUIPanel::UpdateCachedSize()
{
	IGUIObject::UpdateCachedSize();
	m_CachedLayoutActualSize = m_CachedActualSize;
}

CRect IGUIPanel::GetComputedSize()
{
	// Ensure the size is up to date before we use it.
	m_Settings.at("size")->DispatchDelayedSettingChange();
	UpdateCachedSize();
	return m_CachedLayoutActualSize;
}

const std::vector<IGUIObject*>& IGUIPanel::GetVisibleChildren() const
{
	if (m_Drawing)
		return m_Children;

	static std::vector<IGUIObject*> emptyVector;
	return emptyVector;
}
