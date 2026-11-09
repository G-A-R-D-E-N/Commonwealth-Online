#pragma once

namespace F4T::PipboyPauseLatch
{
	struct State
	{
		bool cleared{ false };
	};

	bool OnShow(State& a_state, bool a_onStack, bool a_multiplayer);
	bool OnTick(State& a_state, bool a_menuPresent, bool a_onStack);
}
