#include "F4TPipboyPauseLatch.h"

namespace F4T::PipboyPauseLatch
{
	bool OnShow(State& a_state, bool a_onStack, bool a_multiplayer)
	{
		if (a_onStack || !a_multiplayer) {
			return false;
		}
		a_state.cleared = true;
		return true;
	}

	bool OnTick(State& a_state, bool a_menuPresent, bool a_onStack)
	{
		if (!a_state.cleared) {
			return false;
		}
		if (!a_menuPresent || !a_onStack) {
			a_state.cleared = false;
		}
		return a_menuPresent && !a_onStack;
	}
}
