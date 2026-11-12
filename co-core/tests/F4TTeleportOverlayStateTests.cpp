#include "F4TTeleportOverlayState.h"

#include <cassert>

int main()
{
	using namespace F4T::TeleportOverlay;

	{
		State state{};
		assert(Tick(state, 0.0, false, true) == Action::None);
		assert(Cancel(state) == Action::None);
	}

	{
		State state{};
		assert(Begin(state, 10.0) == Action::Show);
		assert(state.active);
		assert(Begin(state, 10.5) == Action::None);
		assert(Tick(state, 10.5, false, true) == Action::None);
		assert(Tick(state, 10.9, false, true) == Action::None);
		assert(Tick(state, 11.0, false, true) == Action::None);
		assert(Tick(state, 14.0, false, true) == Action::Hide);
		assert(!state.active);
	}

	{
		State state{};
		assert(Begin(state, 0.0) == Action::Show);
		assert(Tick(state, 0.5, true, true) == Action::None);
		assert(state.sawLoadingMenu);
		assert(Tick(state, 2.0, false, false) == Action::None);
		assert(Tick(state, 2.0, false, true) == Action::Hide);
		assert(!state.active);
	}

	{
		State state{};
		assert(Begin(state, 0.0) == Action::Show);
		assert(Tick(state, 3.9, false, true) == Action::None);
		assert(Tick(state, 4.0, false, true) == Action::Hide);
	}

	{
		State state{};
		assert(Begin(state, 0.0) == Action::Show);
		assert(Tick(state, 19.9, true, false) == Action::None);
		assert(Tick(state, 20.0, true, false) == Action::Hide);
		assert(!state.active);
	}

	{
		State state{};
		assert(Begin(state, 0.0) == Action::Show);
		assert(Cancel(state) == Action::Hide);
		assert(!state.active);
		assert(Cancel(state) == Action::None);
		assert(Begin(state, 5.0) == Action::Show);
	}

	{
		FaderWatch watch{};
		assert(WatchFader(watch, 0.0, true, false, false) == FaderAction::None);
		ArmFaderWatch(watch, 10.0);
		assert(WatchFader(watch, 12.9, true, false, false) == FaderAction::None);
		assert(WatchFader(watch, 13.0, false, false, false) == FaderAction::None);
		assert(!watch.armed);
	}

	{
		FaderWatch watch{};
		ArmFaderWatch(watch, 0.0);
		assert(WatchFader(watch, 3.0, true, true, false) == FaderAction::None);
		assert(!watch.armed);
	}

	{
		FaderWatch watch{};
		ArmFaderWatch(watch, 0.0);
		assert(WatchFader(watch, 3.0, true, false, true) == FaderAction::None);
		assert(!watch.armed);
	}

	{
		FaderWatch watch{};
		ArmFaderWatch(watch, 0.0);
		assert(WatchFader(watch, 3.0, true, false, false) == FaderAction::RetryHide);
		assert(WatchFader(watch, 5.0, true, false, false) == FaderAction::None);
		assert(WatchFader(watch, 6.0, true, false, false) == FaderAction::RetryFadeIn);
		assert(WatchFader(watch, 9.0, true, false, false) == FaderAction::GiveUp);
		assert(!watch.armed);
	}

	return 0;
}
