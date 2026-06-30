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

#include "CScrollPanel.h"

#include "gui/CGUIScrollBarHorizontal.h"
#include "gui/CGUIScrollBarVertical.h"
#include "gui/GUIObjectEventBroadcaster.h"
#include "gui/IGUIScrollBar.h"
#include "gui/SGUIMessage.h"
#include "maths/Rect.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

class CGUI;

CScrollPanel::CScrollPanel(CGUI& pGUI)
	: IGUIPanel(pGUI),
	IGUIScrollBarOwner(*static_cast<IGUIObject*>(this)),
	m_Orientation(this, "orientation", EScrollOrientation::VERTICAL),
	m_ScrollBarStyle(this, "scrollbar_style"),
	m_MinWidth(this, "min_width", 0),
	m_MinHeight(this, "min_height", 0)
{
	auto vbar = std::make_unique<CGUIScrollBarVertical>(pGUI);
	vbar->SetRightAligned(true);
	AddScrollBar(std::move(vbar));

	auto hbar = std::make_unique<CGUIScrollBarHorizontal>(pGUI);
	hbar->SetBottomAligned(true);
	AddScrollBar(std::move(hbar));
}

void CScrollPanel::ResetStates()
{
	IGUIPanel::ResetStates();
	IGUIScrollBarOwner::ResetStates();
}

void CScrollPanel::UpdateCachedSize()
{
	IGUIPanel::UpdateCachedSize();
	Setup();
}

void CScrollPanel::HandleMessage(SGUIMessage& Message)
{
	IGUIScrollBar& scrollbar0 = GetScrollBar(0);
	IGUIScrollBar& scrollbar1 = GetScrollBar(1);

	IGUIPanel::HandleMessage(Message);

	float vscroll = scrollbar0.GetPos();
	float hscroll = scrollbar1.GetPos();
	bool updateScrollPosition = false;

	IGUIScrollBarOwner::HandleMessage(Message);

	if (vscroll != scrollbar0.GetPos())
	{
		vscroll = scrollbar0.GetPos();
		updateScrollPosition = true;
	}

	if (hscroll != scrollbar1.GetPos())
	{
		hscroll = scrollbar1.GetPos();
		updateScrollPosition = true;
	}

	if (updateScrollPosition)
		UpdateScrollPosition(vscroll, hscroll);

	switch (Message.type)
	{
	case GUIM_SETTINGS_UPDATED:
		if (Message.value == "scrollbar_style")
		{
			scrollbar0.SetScrollBarStyle(m_ScrollBarStyle);
			scrollbar1.SetScrollBarStyle(m_ScrollBarStyle);
			Setup();
		}

		if (Message.value == "orientation" || Message.value == "size" || Message.value == "min_width" || Message.value == "min_height")
		{
			scrollbar0.SetPos(0);
			scrollbar1.SetPos(0);
			Setup();
		}
		break;

	case GUIM_CHILD_RESIZED:
	case GUIM_CHILD_TOGGLE_VISIBILITY:
		Setup();
		Message.Skip(false);
		break;

	case GUIM_LOAD:
		scrollbar0.SetScrollBarStyle(m_ScrollBarStyle);
		scrollbar1.SetScrollBarStyle(m_ScrollBarStyle);

		Setup();
		break;

	default:
		break;
	}
}

void CScrollPanel::Draw(CCanvas2D& canvas)
{
	IGUIScrollBar& scrollbar0 = GetScrollBar(0);
	IGUIScrollBar& scrollbar1 = GetScrollBar(1);

	m_Drawing = true;
	IGUIScrollBarOwner::Draw(canvas);

	CRect cliparea(m_CachedLayoutActualSize);

	// substract scrollbar from cliparea
	if (scrollbar0.IsVisible())
		cliparea.right -= scrollbar0.GetOuterRect().GetWidth();
	if (scrollbar1.IsVisible())
		cliparea.bottom -= scrollbar1.GetOuterRect().GetHeight();

	CGUIObjectEventBroadcaster::RecurseVisibleObject(this, &IGUIObject::DrawInArea, canvas, cliparea);
	m_Drawing = false;
}

void CScrollPanel::Setup()
{
	IGUIScrollBar& scrollbar0 = GetScrollBar(0);
	IGUIScrollBar& scrollbar1 = GetScrollBar(1);

	m_CachedActualSize = m_CachedLayoutActualSize;

	if (HasVerticalScrollBar() && m_CachedLayoutActualSize.GetHeight() < m_MinHeight)
		m_CachedActualSize.bottom = m_CachedLayoutActualSize.top + m_MinHeight;

	if (HasHorizontalScrollBar() && m_CachedLayoutActualSize.GetWidth() < m_MinWidth)
		m_CachedActualSize.right = m_CachedLayoutActualSize.left + m_MinWidth;

	for (IGUIObject* child : m_Children)
		child->RecurseObject(&IGUIObject::IsHiddenOrGhost, &IGUIObject::UpdateCachedSize);

	float vscroll = scrollbar0.GetPos();
	float hscroll = scrollbar1.GetPos();
	float maxVRange = 0;
	float maxHRange = 0;

	for (IGUIObject* child : m_Children)
	{
		if (child->IsHiddenOrGhost())
			continue;
		CRect childSize = child->GetComputedSize();
		maxVRange = std::max(maxVRange, childSize.bottom);
		maxHRange = std::max(maxHRange, childSize.right);
	}

	maxVRange -= m_CachedLayoutActualSize.top;
	maxHRange -= m_CachedLayoutActualSize.left;

	scrollbar0.SetScrollRange(HasVerticalScrollBar() ? maxVRange : m_CachedLayoutActualSize.GetHeight());
	scrollbar0.SetScrollSpace(m_CachedLayoutActualSize.GetHeight());
	scrollbar0.SetX(m_CachedLayoutActualSize.right);
	scrollbar0.SetY(m_CachedLayoutActualSize.top);
	scrollbar0.SetZ(GetBufferedZ());
	scrollbar0.SetLength(m_CachedLayoutActualSize.GetHeight());

	scrollbar1.SetScrollRange(HasHorizontalScrollBar() ? maxHRange : m_CachedLayoutActualSize.GetWidth());
	scrollbar1.SetScrollSpace(m_CachedLayoutActualSize.GetWidth());
	scrollbar1.SetX(m_CachedLayoutActualSize.left);
	scrollbar1.SetY(m_CachedLayoutActualSize.bottom);
	scrollbar1.SetZ(GetBufferedZ());
	scrollbar1.SetLength(m_CachedLayoutActualSize.GetWidth());

	if (HasVerticalScrollBar() && HasHorizontalScrollBar() && scrollbar0.IsVisible())
	{
		scrollbar1.SetLength(m_CachedLayoutActualSize.GetWidth() - scrollbar0.GetOuterRect().GetWidth());
		scrollbar1.SetScrollSpace(m_CachedLayoutActualSize.GetWidth() + scrollbar0.GetOuterRect().GetWidth());
	}

	if (HasHorizontalScrollBar() && HasVerticalScrollBar() && scrollbar1.IsVisible())
	{
		scrollbar0.SetLength(m_CachedLayoutActualSize.GetHeight() - scrollbar1.GetOuterRect().GetHeight());
		scrollbar0.SetScrollSpace(m_CachedLayoutActualSize.GetHeight() - scrollbar1.GetOuterRect().GetHeight());
	}

	if (HasVerticalScrollBar() && maxVRange < vscroll)
	{
		vscroll = maxVRange;
		scrollbar0.SetPos(vscroll);
	}

	if (HasHorizontalScrollBar() && maxHRange < hscroll)
	{
		hscroll = maxHRange;
		scrollbar1.SetPos(hscroll);
	}

	UpdateScrollPosition(vscroll, hscroll);
}

void CScrollPanel::UpdateScrollPosition(float scroll, float hscroll)
{
	IGUIScrollBar& scrollbar0 = GetScrollBar(0);
	IGUIScrollBar& scrollbar1 = GetScrollBar(1);

	m_CachedActualSize = m_CachedLayoutActualSize;

	if (HasVerticalScrollBar() && m_CachedLayoutActualSize.GetHeight() < m_MinHeight)
		m_CachedActualSize.bottom = m_CachedLayoutActualSize.top + m_MinHeight;

	if (HasHorizontalScrollBar() && m_CachedLayoutActualSize.GetWidth() < m_MinWidth)
		m_CachedActualSize.right = m_CachedLayoutActualSize.left + m_MinWidth;

	m_CachedActualSize.top -= scroll;
	m_CachedActualSize.bottom -= scroll;

	m_CachedActualSize.left -= hscroll;
	m_CachedActualSize.right -= hscroll;

	// upddate scroll bars size base on m_Width
	if (scrollbar0.IsVisible())
		m_CachedActualSize.right -= scrollbar0.GetOuterRect().GetWidth();
	if (scrollbar1.IsVisible())
		m_CachedActualSize.bottom -= scrollbar1.GetOuterRect().GetHeight();

	for (IGUIObject* child : m_Children)
		child->RecurseObject(&IGUIObject::IsHiddenOrGhost, &IGUIObject::UpdateCachedSize);
}

void CScrollPanel::ResetScrollPosition(EScrollOrientation orientation)
{
	IGUIScrollBar& scrollbar0 = GetScrollBar(0);
	IGUIScrollBar& scrollbar1 = GetScrollBar(1);

	if (orientation == EScrollOrientation::BOTH || orientation == EScrollOrientation::VERTICAL)
		scrollbar0.SetPos(0);

	if (orientation == EScrollOrientation::BOTH || orientation == EScrollOrientation::HORIZONTAL)
		scrollbar1.SetPos(0);
}
