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

#include "lib/self_test.h"

#include "network/FSM.h"

#include <array>
#include <cstddef>
#include <utility>
#include <variant>

class TestFSM : public CxxTest::TestSuite
{
	struct FSMGlobalState
	{
		std::array<size_t, 3> occurCount{0, 0, 0};
	};

	template<size_t N>
	static bool IncrementGlobal(FSMGlobalState* state, CFsmEvent<std::monostate>*)
	{
		++std::get<N>(state->occurCount);
		return true;
	}

	static bool IncrementParam(FSMGlobalState*, CFsmEvent<std::size_t&>* event)
	{
		++event->GetParamRef();
		return true;
	}

	enum class State : unsigned int
	{
		ZERO,
		ONE,
		TWO
	};

	enum class Instruction : unsigned int
	{
		TO_ZERO,
		TO_ONE,
		TO_TWO
	};

public:

	void test_global()
	{
		FSMGlobalState globalState;
		CFsm<FSMGlobalState, std::monostate> FSMObject;

		/*
		Corresponding pseudocode

		while (true)
		{
			// state zero
			await nextInstruction();

			// state one
			const auto cond = await nextInstruction();

			if (cond == instruction::TO_ONE)
			{
				//state two
				await nextInstruction();
			}
		}
		*/

		FSMObject.AddTransition(static_cast<unsigned int>(State::ZERO),
			static_cast<unsigned int>(Instruction::TO_ONE), static_cast<unsigned int>(State::ONE),
			&IncrementGlobal<1>, &globalState);
		FSMObject.AddTransition(static_cast<unsigned int>(State::ONE),
			static_cast<unsigned int>(Instruction::TO_TWO), static_cast<unsigned int>(State::TWO),
			&IncrementGlobal<2>, &globalState);
		FSMObject.AddTransition(static_cast<unsigned int>(State::ONE),
			static_cast<unsigned int>(Instruction::TO_ZERO), static_cast<unsigned int>(State::ZERO),
			&IncrementGlobal<0>, &globalState);
		FSMObject.AddTransition(static_cast<unsigned int>(State::TWO),
			static_cast<unsigned int>(Instruction::TO_ZERO), static_cast<unsigned int>(State::ZERO),
			&IncrementGlobal<0>, &globalState);

		FSMObject.SetFirstState(static_cast<unsigned int>(State::ZERO));

		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE), {}));
		TS_ASSERT_EQUALS(std::get<1>(globalState.occurCount), 1);
		TS_ASSERT_EQUALS(FSMObject.GetCurrState(), static_cast<unsigned int>(State::ONE));

		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ZERO), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ZERO), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ZERO), {}));
		TS_ASSERT_EQUALS(std::get<0>(globalState.occurCount), 3);
		TS_ASSERT_EQUALS(std::get<1>(globalState.occurCount), 3);
		TS_ASSERT_EQUALS(std::get<2>(globalState.occurCount), 1);

		// Some transitions do not exist.
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ZERO), {}));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE), {}));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE), {}));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO), {}));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO), {}));
		TS_ASSERT_EQUALS(std::get<0>(globalState.occurCount), 3);
		TS_ASSERT_EQUALS(std::get<1>(globalState.occurCount), 4);
		TS_ASSERT_EQUALS(std::get<2>(globalState.occurCount), 2);
	}

	void test_param()
	{
		FSMGlobalState globalState;
		CFsm<FSMGlobalState, std::size_t&> FSMObject;

		// Equal to the FSM in test_global.
		FSMObject.AddTransition(static_cast<unsigned int>(State::ZERO),
			static_cast<unsigned int>(Instruction::TO_ONE), static_cast<unsigned int>(State::ONE),
			&IncrementParam, nullptr);
		FSMObject.AddTransition(static_cast<unsigned int>(State::ONE),
			static_cast<unsigned int>(Instruction::TO_TWO), static_cast<unsigned int>(State::TWO),
			&IncrementParam, nullptr);
		FSMObject.AddTransition(static_cast<unsigned int>(State::ONE),
			static_cast<unsigned int>(Instruction::TO_ZERO), static_cast<unsigned int>(State::ZERO),
			&IncrementParam, nullptr);
		FSMObject.AddTransition(static_cast<unsigned int>(State::TWO),
			static_cast<unsigned int>(Instruction::TO_ZERO), static_cast<unsigned int>(State::ZERO),
			&IncrementParam, nullptr);

		FSMObject.SetFirstState(static_cast<unsigned int>(State::ZERO));

		// Some transitions do not exist.
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ZERO),
			std::get<0>(globalState.occurCount)));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO),
			std::get<2>(globalState.occurCount)));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE),
			std::get<1>(globalState.occurCount)));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_ONE),
			std::get<1>(globalState.occurCount)));
		TS_ASSERT(FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO),
			std::get<2>(globalState.occurCount)));
		TS_ASSERT(!FSMObject.Update(static_cast<unsigned int>(Instruction::TO_TWO),
			std::get<2>(globalState.occurCount)));
		TS_ASSERT_EQUALS(std::get<0>(globalState.occurCount), 0);
		TS_ASSERT_EQUALS(std::get<1>(globalState.occurCount), 1);
		TS_ASSERT_EQUALS(std::get<2>(globalState.occurCount), 1);
	}
};
