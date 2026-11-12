#include "F4TPipboyPauseLatch.h"

#include <cassert>

int main()
{
	using namespace F4T::PipboyPauseLatch;

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(state.cleared);
		assert(!OnTick(state, true, true));
		assert(state.cleared);
		assert(OnTick(state, true, false));
		assert(!state.cleared);
		assert(!OnTick(state, true, false));
	}

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(OnTick(state, true, false));
		assert(OnShow(state, false, true));
		assert(state.cleared);
		assert(OnTick(state, true, false));
		assert(!state.cleared);
	}

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(!OnShow(state, true, true));
		assert(state.cleared);
		assert(OnTick(state, true, false));
		assert(!state.cleared);
	}

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(!OnTick(state, false, false));
		assert(!state.cleared);
	}

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(!OnTick(state, false, false));
		assert(!state.cleared);
		assert(OnShow(state, false, true));
		assert(state.cleared);
		assert(OnTick(state, true, false));
		assert(!state.cleared);
		assert(!OnTick(state, true, false));
	}

	{
		State state{};
		assert(OnShow(state, false, true));
		assert(!OnTick(state, true, true));
		assert(state.cleared);
		assert(!OnTick(state, false, true));
		assert(!state.cleared);
		assert(!OnTick(state, true, false));
	}

	{
		State state{};
		assert(!OnShow(state, false, false));
		assert(!state.cleared);
		assert(!OnTick(state, true, false));
	}
}
