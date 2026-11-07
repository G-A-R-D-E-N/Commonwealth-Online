#include "F4TSurvivalPolicyState.h"

#include <cassert>

int main()
{
	F4T::SurvivalPolicy::State state;
	assert(!state.AllowsGameplay(1, 1, true));
	state.Update(false);
	assert(state.AllowsGameplay(1, 1, true));
	assert(!state.AllowsGameplay(1, 1, false));
	state.Update(true);
	assert(!state.AllowsGameplay(1, 1, true));
	assert(!state.Prepare(0, 1, true));
	assert(!state.Prepare(1, 0, true));
	const auto first = state.Prepare(1, 1, true);
	assert(first);
	assert(!state.Complete(*first, 1, 1, true, false));
	assert(!state.AllowsGameplay(1, 1, true));
	state.Update(false);
	assert(!state.Current(*first, 1, 1, true));
	state.Update(true);
	assert(!state.Complete(*first, 1, 1, true, true));
	const auto next = state.Prepare(1, 1, true);
	assert(next && state.Complete(*next, 1, 1, true, true));
	assert(state.AllowsGameplay(1, 1, true));
	state.Update(true);
	assert(state.AllowsGameplay(1, 1, true));
	assert(!state.AllowsGameplay(1, 2, true));
	assert(!state.Current(*next, 2, 1, true));
	assert(!state.Complete(*next, 1, 2, true, true));
	state.Reset();
	assert(!state.Required());
	assert(!state.AllowsGameplay(1, 1, true));
	state.Update(true);
	assert(!state.Current(*next, 1, 1, true));
	assert(!state.AllowsGameplay(1, 1, true));
	return 0;
}
