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

#ifndef INCLUDED_IPANEL
#define INCLUDED_IPANEL

#include "gui/ObjectBases/IGUIObject.h"
#include "maths/Rect.h"

class CGUI;

class IGUIPanel : public IGUIObject
{
public:
	IGUIPanel(CGUI& pGUI);
	virtual ~IGUIPanel();

	virtual void UpdateCachedSize();
	virtual CRect GetComputedSize();

	virtual bool IsMouseOver() const;

	virtual const std::vector<IGUIObject*>& GetVisibleChildren() const;

protected:
	CRect m_CachedLayoutActualSize;
	bool m_Drawing = false;
};

#endif // INCLUDED_IPANEL
