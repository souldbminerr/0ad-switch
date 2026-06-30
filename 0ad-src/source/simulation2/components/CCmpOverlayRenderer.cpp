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

#include "ICmpOverlayRenderer.h"

#include "graphics/Color.h"
#include "graphics/HeightMipmap.h"
#include "graphics/Overlay.h"
#include "graphics/TextureManager.h"
#include "maths/FixedVector2D.h"
#include "maths/Matrix3D.h"
#include "maths/Vector3D.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Profile.h"
#include "renderer/Renderer.h"
#include "renderer/Scene.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Message.h"

#include <cstddef>
#include <string>
#include <vector>

class CCmpOverlayRenderer final : public ICmpOverlayRenderer
{
public:
	static void ClassInit(CComponentManager &componentManager)
	{
		componentManager.SubscribeToMessageType(MT_VisibilityChanged);
	}

	DEFAULT_COMPONENT_ALLOCATOR(OverlayRenderer)

	// Currently-enabled set of sprites
	std::vector<SOverlaySprite> m_Sprites;

	// For each entry in m_Sprites, store the offset of the sprite from the unit's position
	// (so we can recompute the sprite position after the unit moves)
	std::vector<CVector3D> m_SpriteOffsets;

	// Whether the sprites should be drawn (only valid between Interpolate and RenderSubmit)
	bool m_Enabled;

	// NOT SAFE TO SERIALIZE - used to hide status bars/auras of entities invisible to the player.
	// We also can't just deserialize it because annoyingly the range manager computes LOS data on deserialize...
	LosVisibility m_VisibilityToCurrentPlayer = LosVisibility::VISIBLE;
	// So to work around that, just store which player we saved this for last.
	player_id_t m_LastStoredPlayerVisibility = INVALID_PLAYER;

	static std::string GetSchema()
	{
		return "<empty/>";
	}

	void Init(const CParamNode&) override
	{
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer&) override
	{
		// TODO: should we do anything here?
		// or should we expect other components to reinitialise us
		// after deserialization?
	}

	void Deserialize(const CParamNode &paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void HandleMessage(const CMessage &msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_Interpolate:
		{
			PROFILE("OverlayRenderer::Interpolate");
			const CMessageInterpolate &msgData = static_cast<const CMessageInterpolate &>(msg);
			Interpolate(msgData.deltaSimTime, msgData.offset);
			break;
		}
		case MT_RenderSubmit:
		{
			PROFILE("OverlayRenderer::RenderSubmit");
			const CMessageRenderSubmit &msgData = static_cast<const CMessageRenderSubmit &>(msg);
			RenderSubmit(msgData.collector);
			break;
		}
		case MT_VisibilityChanged:
		{
			const CMessageVisibilityChanged &msgData = static_cast<const CMessageVisibilityChanged &>(msg);
			if (msgData.player == GetSimContext().GetCurrentDisplayedPlayer())
				m_VisibilityToCurrentPlayer = static_cast<LosVisibility>(msgData.newVisibility);
			break;
		}
		}
	}

	/*
	 * Must be called whenever the size of m_Sprites changes,
	 * to determine whether we need to respond to rendering messages.
	 */
	void UpdateMessageSubscriptions()
	{
		bool needRender = !m_Sprites.empty();

		GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_Interpolate, this, needRender);
		GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_RenderSubmit, this, needRender);
	}

	void Reset() override
	{
		m_Sprites.clear();
		m_SpriteOffsets.clear();

		UpdateMessageSubscriptions();
	}

	void AddSprite(const VfsPath &textureName, const CFixedVector2D &corner0, const CFixedVector2D &corner1, const CFixedVector3D &position, const std::string &color) override
	{
		CColor colorObj(1.0f, 1.0f, 1.0f, 1.0f);
		if (!colorObj.ParseString(color, 1))
			LOGERROR("OverlayRenderer: Error parsing '%s'", color);

		CTextureProperties textureProps(textureName);

		SOverlaySprite sprite;
		sprite.m_Texture = g_Renderer.GetTextureManager().CreateTexture(textureProps);
		sprite.m_X0 = corner0.X.ToFloat();
		sprite.m_Y0 = corner0.Y.ToFloat();
		sprite.m_X1 = corner1.X.ToFloat();
		sprite.m_Y1 = corner1.Y.ToFloat();
		sprite.m_Color = colorObj;

		m_Sprites.push_back(sprite);
		m_SpriteOffsets.push_back(CVector3D(position));

		UpdateMessageSubscriptions();
	}

	void Interpolate(float /*frameTime*/, float frameOffset)
	{
		// Recompute our visibility if needed.
		if (m_LastStoredPlayerVisibility != GetSimContext().GetCurrentDisplayedPlayer())
		{
			CmpPtr<ICmpRangeManager> cmpRangeManager(GetSystemEntity());
			m_VisibilityToCurrentPlayer = static_cast<LosVisibility>(cmpRangeManager->GetLosVisibility(GetEntityHandle(), GetSimContext().GetCurrentDisplayedPlayer()));
			m_LastStoredPlayerVisibility = GetSimContext().GetCurrentDisplayedPlayer();
		}

		// Skip all the following computations if we have no sprites
		if (m_Sprites.empty() || m_VisibilityToCurrentPlayer == LosVisibility::HIDDEN)
		{
			m_Enabled = false;
			return;
		}

		// Disable rendering of the unit if it has no position
		CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
		if (!cmpPosition || !cmpPosition->IsInWorld())
		{
			m_Enabled = false;
			return;
		}

		// Find the precise position of the unit
		CMatrix3D transform(cmpPosition->GetInterpolatedTransform(frameOffset));
		CVector3D position(transform.GetTranslation());

		// Move all the sprites to the desired offset relative to the unit
		for (size_t i = 0; i < m_Sprites.size(); ++i)
			m_Sprites[i].m_Position = position + m_SpriteOffsets[i];

		m_Enabled = true;
	}

	void RenderSubmit(SceneCollector &collector)
	{
		if (!m_Enabled || !ICmpOverlayRenderer::m_OverrideVisible)
			return;

		for (size_t i = 0; i < m_Sprites.size(); ++i)
			collector.Submit(&m_Sprites[i]);
	}
};

REGISTER_COMPONENT_TYPE(OverlayRenderer)
