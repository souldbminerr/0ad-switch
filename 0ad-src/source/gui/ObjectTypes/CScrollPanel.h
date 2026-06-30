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

#ifndef INCLUDED_CSCROLLPANEL
#define INCLUDED_CSCROLLPANEL

#include "gui/CGUISetting.h"
#include "gui/ObjectBases/IGUIObject.h"
#include "gui/ObjectBases/IGUIPanel.h"
#include "gui/ObjectBases/IGUIScrollBarOwner.h"
#include "gui/SettingTypes/EScrollOrientation.h"
#include "ps/CStr.h"

#include <vector>

class CGUI;

class CScrollPanel : public IGUIPanel, public IGUIScrollBarOwner
{
	GUI_OBJECT(CScrollPanel)
public:
	CScrollPanel(CGUI& pGUI);

	virtual void UpdateCachedSize();
	virtual void ResetStates();

	void Setup();

	void ResetScrollPosition(EScrollOrientation orientation = EScrollOrientation::BOTH);

protected:
	/**
	 * @see IGUIObject#HandleMessage()
	 */
	virtual void HandleMessage(SGUIMessage& Message);

	void UpdateScrollPosition(float vscroll, float hscroll);

	bool HasHorizontalScrollBar() const { return *m_Orientation == EScrollOrientation::HORIZONTAL || *m_Orientation == EScrollOrientation::BOTH; };
	bool HasVerticalScrollBar() const { return *m_Orientation == EScrollOrientation::VERTICAL || *m_Orientation == EScrollOrientation::BOTH; };

	virtual void Draw(CCanvas2D& canvas);

	virtual void CreateJSObject();

	CGUISimpleSetting<EScrollOrientation> m_Orientation;
	CGUISimpleSetting<CStr> m_ScrollBarStyle;
	CGUISimpleSetting<int> m_MinWidth;
	CGUISimpleSetting<int> m_MinHeight;
};

#endif // INCLUDED_CSCROLLPANEL
