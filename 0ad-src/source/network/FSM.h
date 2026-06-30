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

#ifndef FSM_H
#define FSM_H

#include <cstddef>
#include <limits>
#include <unordered_map>

constexpr unsigned int FSM_INVALID_STATE{std::numeric_limits<unsigned int>::max()};

/**
 * Represents a signal in the state machine that a change has occurred.
 * The CFsmEvent objects are under the control of CFsm so
 * they are created and deleted via CFsm.
 */
template <typename MessageType>
class CFsmEvent
{
public:
	CFsmEvent(unsigned int type, MessageType param) :
		m_Type{type},
		m_Param{param}
	{}

	unsigned int GetType() const
	{
		return m_Type;
	}

	MessageType GetParamRef()
	{
		return m_Param;
	}

private:
	unsigned int m_Type; // Event type
	MessageType m_Param; // Event paramater
};

/**
 * Manages states, events, actions and transitions
 * between states. It provides an interface for advertising
 * events and track the current state. The implementation is
 * a Mealy state machine, so the system respond to events
 * and execute some action.
 *
 * A Mealy state machine has behaviour associated with state
 * transitions; Mealy machines are event driven where an
 * event triggers a state transition.
 */
template <typename Context, typename MessageType>
class CFsm
{
	using Action = bool(Context* pContext, CFsmEvent<MessageType>* pEvent);

	struct CallbackFunction
	{
		Action* pFunction{nullptr};
		Context* pContext{nullptr};

		bool operator()(CFsmEvent<MessageType>& event) const
		{
			return !pFunction || pFunction(pContext, &event);
		}
	};
public:
	/**
	 * Adds a new transistion to the state machine.
	 */
	void AddTransition(unsigned int state, unsigned int eventType, unsigned int nextState,
		Action* pAction = nullptr, Context* pContext = nullptr)
	{
		m_Transitions.insert({TransitionKey{state, eventType},
			Transition{{pAction, pContext}, nextState}});
	}

	/**
	 * Sets the initial state for FSM.
	 */
	void SetFirstState(unsigned int firstState)
	{
		m_FirstState = firstState;
	}

	/**
	 * Sets the current state and update the last state to the current state.
	 */
	void SetCurrState(unsigned int state)
	{
		m_CurrState = state;
	}
	unsigned int GetCurrState() const
	{
		return m_CurrState;
	}

	void SetNextState(unsigned int nextState)
	{
		m_NextState = nextState;
	}

	unsigned int GetNextState() const
	{
		return m_NextState;
	}

	/**
	 * Updates the FSM and retrieves next state.
	 * @return whether the state was changed.
	 */
	bool Update(unsigned int eventType, MessageType eventData)
	{
		if (IsFirstTime())
			m_CurrState = m_FirstState;

		// Lookup transition
		auto transitionIterator = m_Transitions.find({m_CurrState, eventType});
		if (transitionIterator == m_Transitions.end())
			return false;

		CFsmEvent<MessageType> event{eventType, eventData};

		// Save the default state transition (actions might call SetNextState
		// to override this)
		SetNextState(transitionIterator->second.nextState);

		if (!transitionIterator->second.action(event))
			return false;

		SetCurrState(GetNextState());

		// Reset the next state since it's no longer valid
		SetNextState(FSM_INVALID_STATE);

		return true;
	}

	/**
	 * Tests whether the state machine has finished its work.
	 */
	bool IsDone() const
	{
		return m_Done;
	}

private:
	struct TransitionKey
	{
		using UnderlyingType = unsigned int;
		UnderlyingType state;
		UnderlyingType eventType;

		struct Hash
		{
			size_t operator()(const TransitionKey& key) const noexcept
			{
				constexpr size_t count{std::numeric_limits<size_t>::digits / 2};
				const size_t wideState{static_cast<size_t>(key.state)};
				const size_t rotatedState{(wideState << count) | (wideState >> count)};
				return static_cast<size_t>(key.eventType) ^ rotatedState;
			}
		};

		friend bool operator==(const TransitionKey& lhs, const TransitionKey& rhs) noexcept
		{
			return lhs.state == rhs.state && lhs.eventType == rhs.eventType;
		}
	};

	struct Transition
	{
		CallbackFunction action;
		unsigned int nextState;
	};

	using TransitionMap = std::unordered_map<TransitionKey, const Transition,
		typename TransitionKey::Hash>;

	/**
	 * Verifies whether state machine has already been updated.
	 */
	bool IsFirstTime() const
	{
		return m_CurrState == FSM_INVALID_STATE;
	}

	bool m_Done{false};
	unsigned int m_FirstState{FSM_INVALID_STATE};
	unsigned int m_CurrState{FSM_INVALID_STATE};
	unsigned int m_NextState{FSM_INVALID_STATE};
	TransitionMap m_Transitions;
};

#endif // FSM_H
