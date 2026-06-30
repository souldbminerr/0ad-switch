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

#include "ICmpDecay.h"

#include "lib/debug.h"
#include "maths/BoundingBoxAligned.h"
#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/Vector3D.h"
#include "ps/Profile.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpTerrain.h"
#include "simulation2/components/ICmpVisual.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <string>

/**
 * Fairly basic decay implementation, for units and buildings etc.
 * The decaying entity remains stationary for some time period, then falls downwards
 * with some initial speed and some acceleration, until it has fully sunk.
 * The sinking depth is determined from the actor's bounding box and the terrain.
 *
 * This currently doesn't work with entities whose ICmpPosition has an initial Y offset.
 *
 * This isn't very efficient (we'll store data and iterate every frame for every entity,
 * not just for corpse entities) - it could be designed more optimally if that's a real problem.
 *
 * Eventually we might want to adjust the decay rate based on user configuration (low-spec
 * machines could have fewer corpses), number of corpses, etc.
 *
 * Must not be used on network-synchronised entities, unless \<Inactive\> is present.
 */
class CCmpDecay final : public ICmpDecay
{
public:
	static void ClassInit(CComponentManager&)
	{
	}

	DEFAULT_COMPONENT_ALLOCATOR(Decay)

	bool m_Active;
	bool m_ShipSink;
	bool m_Stochastic;

	std::optional<std::minstd_rand> m_Generator;
	std::negative_binomial_distribution<> m_Distribution{ 6, 0.032 };
	std::geometric_distribution<>::result_type m_NextSink;

	float m_SinkProb;
	float m_DelayTime;
	float m_SinkRate;
	float m_SinkAccel;

	entity_pos_t m_InitialXRotation;
	entity_pos_t m_InitialZRotation;

	// Used to randomize ship-like sinking
	float m_SinkingAngleX;
	float m_SinkingAngleZ;

	float m_CurrentTime;
	float m_TotalSinkDepth; // distance we need to sink (derived from bounding box), or -1 if undetermined

	static std::string GetSchema()
	{
		return
			"<element name='Active' a:help='If false, the entity will not do any decaying'>"
				"<data type='boolean'/>"
			"</element>"
			"<element name='SinkingAnim' a:help='If true, the entity will decay in a ship-like manner'>"
				"<data type='boolean'/>"
			"</element>"
			"<element name='SinkProb' a:help='The entity decays according to the supplied probability each frame.'>"
				"<ref name='nonNegativeDecimal'/>"
			"</element>"
			"<element name='DelayTime' a:help='Time to wait before starting to sink, in seconds'>"
				"<ref name='nonNegativeDecimal'/>"
			"</element>"
			"<element name='SinkRate' a:help='Initial rate of sinking, in meters per second'>"
				"<ref name='nonNegativeDecimal'/>"
			"</element>"
			"<element name='SinkAccel' a:help='Acceleration rate of sinking, in meters per second per second'>"
				"<ref name='nonNegativeDecimal'/>"
			"</element>";
	}

	void Init(const CParamNode& paramNode) override
	{
		m_Active = paramNode.GetChild("Active").ToBool();
		m_ShipSink = paramNode.GetChild("SinkingAnim").ToBool();
		m_SinkProb = paramNode.GetChild("SinkProb").ToFixed().ToFloat();
		m_DelayTime = paramNode.GetChild("DelayTime").ToFixed().ToFloat();
		m_SinkRate = paramNode.GetChild("SinkRate").ToFixed().ToFloat();
		m_SinkAccel = paramNode.GetChild("SinkAccel").ToFixed().ToFloat();

		m_CurrentTime = 0.f;
		m_TotalSinkDepth = -1.f;

		m_Stochastic = m_SinkProb < 1.0f;

		std::negative_binomial_distribution<int>::param_type new_params(
			6, Clamp(m_SinkProb, 1e-3f, 1.0f));
		m_Distribution.param(new_params);

		// Detect unsafe misconfiguration
		if (m_Active && !ENTITY_IS_LOCAL(GetEntityId()))
		{
			debug_warn(L"CCmpDecay must not be used on non-local (network-synchronised) entities");
			m_Active = false;
		}

		if (m_Active)
			GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_Interpolate, this, true);
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer&) override
	{
		// This component isn't network-synchronised, so don't serialize anything
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_Interpolate:
		{
			PROFILE("Decay::Interpolate");

			if (!m_Active)
				break;

			const CMessageInterpolate& msgData = static_cast<const CMessageInterpolate&> (msg);

			CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
			if (!cmpPosition || !cmpPosition->IsInWorld())
			{
				// If there's no position (this usually shouldn't happen), destroy the unit immediately
				GetSimContext().GetComponentManager().DestroyComponentsSoon(GetEntityId());
				break;
			}

			// Compute the depth the first time this is called
			// (This is a bit of an ugly place to do it but at least we'll be sure
			// the actor component was loaded already)
			if (m_TotalSinkDepth < 0.f)
			{
				m_TotalSinkDepth = 1.f; // minimum so we always sink at least a little

				CmpPtr<ICmpVisual> cmpVisual(GetEntityHandle());
				if (cmpVisual)
				{
					CBoundingBoxAligned bound = cmpVisual->GetBounds();
					m_TotalSinkDepth = std::max(m_TotalSinkDepth, bound[1].Y - bound[0].Y);
				}

				// If this is a floating unit, we want it to sink all the way under the terrain,
				// so find the difference between its current position and the terrain

				CFixedVector3D pos = cmpPosition->GetPosition();

				//Use the entities position upon decay start to vary the pseudorandom seed.
				if (!m_Generator.has_value())
				{
					m_Generator.emplace(pos.X.ToInt_RoundToNearest() * pos.Z.ToInt_RoundToNearest());
					m_NextSink = m_Distribution(m_Generator.value());
				}

				CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
				if (cmpTerrain)
				{
					fixed ground = cmpTerrain->GetGroundLevel(pos.X, pos.Z);
					m_TotalSinkDepth += std::max(0.f, (pos.Y - ground).ToFloat());
				}

				// Sink it further down if it sinks like a ship, as it will rotate.
				if (m_ShipSink)
				{
					// lacking randomness we'll trick
					m_SinkingAngleX = (pos.X.ToInt_RoundToNearest() % 30 - 15) / 15.0;
					m_SinkingAngleZ = (pos.Z.ToInt_RoundToNearest() % 30) / 40.0;
					m_TotalSinkDepth += 10.f;
				}
				// probably 0 in both cases but we'll remember it anyway.
				m_InitialXRotation = cmpPosition->GetRotation().X;
				m_InitialZRotation = cmpPosition->GetRotation().Z;
			}

			m_CurrentTime += msgData.deltaSimTime;

			if (m_CurrentTime >= m_DelayTime)
			{
				//If the entity should sink stochastically, use the negative binomial distribution to see if it should sink each frame.
				if ((m_Stochastic && (m_NextSink-- == 0)) || !m_Stochastic)
				{
					m_NextSink = m_Distribution(m_Generator.value());
					float t = m_CurrentTime - m_DelayTime;
					float depth = (m_SinkRate * t) + (m_SinkAccel * t * t);

					if (m_ShipSink)
					{
						// exponential sinking with tilting
						float tilt_time = t > 5.f ? 5.f : t;
						float tiltSink = tilt_time * tilt_time / 5.f;
						entity_pos_t RotX = entity_pos_t::FromFloat(((m_InitialXRotation.ToFloat() * (5.f - tiltSink)) + (m_SinkingAngleX * tiltSink)) / 5.f);
						entity_pos_t RotZ = entity_pos_t::FromFloat(((m_InitialZRotation.ToFloat() * (3.f - tilt_time)) + (m_SinkingAngleZ * tilt_time)) / 3.f);
						cmpPosition->SetXZRotation(RotX,RotZ);

						depth = m_SinkRate * (exp(t - 1.f) - 0.54881163609f) + (m_SinkAccel * exp(t - 4.f) - 0.01831563888f);
						if (depth < 0.f)
							depth = 0.f;
					}

					cmpPosition->SetHeightOffset(entity_pos_t::FromFloat(-depth));

					if (depth > m_TotalSinkDepth)
						GetSimContext().GetComponentManager().DestroyComponentsSoon(GetEntityId());
				}
			}

			break;
		}
		}
	}
};

REGISTER_COMPONENT_TYPE(Decay)
