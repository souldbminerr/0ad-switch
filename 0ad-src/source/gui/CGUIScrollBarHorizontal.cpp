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

#include "CGUIScrollBarHorizontal.h"

#include "gui/CGUI.h"
#include "gui/CGUISprite.h"
#include "gui/IGUIScrollBar.h"
#include "gui/SGUIMessage.h"
#include "maths/Rect.h"
#include "maths/Vector2D.h"
#include "ps/CLogger.h"

CGUIScrollBarHorizontal::CGUIScrollBarHorizontal(CGUI& pGUI)
	: IGUIScrollBar(pGUI)
{
}

CGUIScrollBarHorizontal::~CGUIScrollBarHorizontal()
{
}

void CGUIScrollBarHorizontal::SetPosFromMousePos(const CVector2D& mouse)
{
	if (!GetStyle())
		return;

	// Calculate the position for the top of the item being scrolled
	float emptyBackground = m_Length - m_BarSize;

	if (GetStyle()->m_UseEdgeButtons)
		emptyBackground -= GetStyle()->m_Width * 2;

	m_Pos = m_PosWhenPressed + GetMaxPos() * (mouse.X - m_BarPressedAtPos.X) / emptyBackground;
}

void CGUIScrollBarHorizontal::Draw(CCanvas2D& canvas)
{
	if (!IsVisible())
		return;

	if (!GetStyle())
	{
		LOGWARNING("Attempt to draw scrollbar without a style.");
		return;
	}

	CRect outline = GetOuterRect();

	m_pGUI.DrawSprite(
		GetStyle()->m_SpriteBackHorizontal,
		canvas,
		CRect(
			outline.left + (GetStyle()->m_UseEdgeButtons ? GetStyle()->m_Width : 0),
			outline.top,
			outline.right - (GetStyle()->m_UseEdgeButtons ? GetStyle()->m_Width : 0),
			outline.bottom));

	if (GetStyle()->m_UseEdgeButtons)
	{
		const CGUISpriteInstance* button_left;
		const CGUISpriteInstance* button_right;

		if (m_ButtonMinusHovered)
		{
			if (m_ButtonMinusPressed)
				button_left = &(GetStyle()->m_SpriteButtonLeftPressed ? GetStyle()->m_SpriteButtonLeftPressed : GetStyle()->m_SpriteButtonLeft);
			else
				button_left = &(GetStyle()->m_SpriteButtonLeftOver ? GetStyle()->m_SpriteButtonLeftOver : GetStyle()->m_SpriteButtonLeft);
		}
		else
			button_left = &GetStyle()->m_SpriteButtonLeft;

		if (m_ButtonPlusHovered)
		{
			if (m_ButtonPlusPressed)
				button_right = &(GetStyle()->m_SpriteButtonRightPressed ? GetStyle()->m_SpriteButtonRightPressed : GetStyle()->m_SpriteButtonRight);
			else
				button_right = &(GetStyle()->m_SpriteButtonRightOver ? GetStyle()->m_SpriteButtonRightOver : GetStyle()->m_SpriteButtonRight);
		}
		else
			button_right = &GetStyle()->m_SpriteButtonRight;

		m_pGUI.DrawSprite(
			*button_left,
			canvas,
			CRect(
				outline.left,
				outline.top,
				outline.left + GetStyle()->m_Width,
				outline.bottom));

		m_pGUI.DrawSprite(
			*button_right,
			canvas,
			CRect(
				outline.right - GetStyle()->m_Width,
				outline.top,
				outline.right,
				outline.bottom));
	}

	m_pGUI.DrawSprite(
		GetStyle()->m_SpriteSliderHorizontal,
		canvas,
		GetBarRect()
	);
}

void CGUIScrollBarHorizontal::HandleMessage(SGUIMessage& Message)
{
	switch (Message.type)
	{
	case GUIM_MOUSE_WHEEL_LEFT:
	{
		ScrollMinus();
		// Since the scroll was changed, let's simulate a mouse movement
		//  to check if scrollbar now is hovered
		SGUIMessage msg(GUIM_MOUSE_MOTION);
		HandleMessage(msg);
		Message.Skip(false);
		break;
	}

	case GUIM_MOUSE_WHEEL_RIGHT:
	{
		ScrollPlus();
		// Since the scroll was changed, let's simulate a mouse movement
		//  to check if scrollbar now is hovered
		SGUIMessage msg(GUIM_MOUSE_MOTION);
		HandleMessage(msg);
		Message.Skip(false);
		break;
	}

	default:
		IGUIScrollBar::HandleMessage(Message);
		break;
	}
}

CRect CGUIScrollBarHorizontal::GetBarRect() const
{
	CRect ret;
	if (!GetStyle())
		return ret;

	// Get from where the scroll area begins to where it ends
	float from = m_X;
	float to = m_X + m_Length - m_BarSize;

	if (GetStyle()->m_UseEdgeButtons)
	{
		from += GetStyle()->m_Width;
		to -= GetStyle()->m_Width;
	}

	ret.left = from + (to - from) * m_Pos / GetMaxPos();
	ret.right = ret.left + m_BarSize;
	ret.bottom = m_Y + (m_BottomAligned ? 0 : GetStyle()->m_Width);
	ret.top = ret.bottom - GetStyle()->m_Width;

	return ret;
}

CRect CGUIScrollBarHorizontal::GetOuterRect() const
{
	CRect ret;
	if (!GetStyle())
		return ret;

	ret.left = m_X;
	ret.right = m_X + m_Length;
	ret.bottom = m_Y + (m_BottomAligned ? 0 : GetStyle()->m_Width);
	ret.top = ret.bottom - GetStyle()->m_Width;

	return ret;
}

bool CGUIScrollBarHorizontal::HoveringButtonMinus(const CVector2D& mouse)
{
	if (!GetStyle())
		return false;

	float StartY = m_BottomAligned ? m_Y - GetStyle()->m_Width : m_Y;

	return mouse.Y >= StartY &&
		mouse.Y <= StartY + GetStyle()->m_Width &&
		mouse.X >= m_X &&
		mouse.X <= m_X + GetStyle()->m_Width;
}

bool CGUIScrollBarHorizontal::HoveringButtonPlus(const CVector2D& mouse)
{
	if (!GetStyle())
		return false;

	float StartY = m_BottomAligned ? m_Y - GetStyle()->m_Width : m_Y;

	return mouse.Y > StartY &&
		mouse.Y < StartY + GetStyle()->m_Width &&
		mouse.X > m_X + m_Length - GetStyle()->m_Width &&
		mouse.X < m_X + m_Length;
}

void CGUIScrollBarHorizontal::SetScrollPlentyFromMousePos(const CVector2D& mouse)
{
	// Scroll plus or minus a lot, this might change, it doesn't
	//  have to be fancy though.
	if (mouse.X < GetBarRect().left)
		ScrollMinusPlenty();
	else
		ScrollPlusPlenty();
}
