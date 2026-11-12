#include "F4TLauncherEventProvenance.h"

#include <cassert>
#include <cstdint>

namespace
{
	using F4T::MainMenuLauncher::EventSource;
	using F4T::MainMenuLauncher::EventSourceState;
	using F4T::MainMenuLauncher::IsCurrentEventSource;

	EventSourceState State(
		std::uint64_t a_view,
		std::uint64_t a_generation,
		bool a_desiredVisible = true,
		bool a_handleValid = true,
		bool a_runtimeReady = true)
	{
		return EventSourceState{ a_view, a_generation, a_handleValid, a_desiredVisible, a_runtimeReady };
	}
}

int main()
{
	const EventSource viewA{ 11, 1 };
	const EventSource viewB{ 22, 2 };

	// View A is current, then recreated and replaced by view B.
	assert(IsCurrentEventSource(viewA, State(11, 1)));
	assert(IsCurrentEventSource(viewB, State(22, 2)));

	// A delayed event from A cannot act once B is bound.
	assert(!IsCurrentEventSource(viewA, State(22, 2)));

	// A stale generation never passes, even when the handle token matches.
	assert(!IsCurrentEventSource(EventSource{ 22, 1 }, State(22, 2)));
	assert(!IsCurrentEventSource(EventSource{ 11, 2 }, State(22, 2)));

	// Hide / not-wanted rejects both the current and a stale source.
	assert(!IsCurrentEventSource(viewB, State(22, 2, /*desiredVisible=*/false)));
	assert(!IsCurrentEventSource(viewA, State(22, 2, /*desiredVisible=*/false)));

	// Invalid handle or a launcher that is not runtime-ready rejects.
	assert(!IsCurrentEventSource(viewB, State(22, 2, true, /*handleValid=*/false)));
	assert(!IsCurrentEventSource(viewB, State(22, 2, true, true, /*runtimeReady=*/false)));

	// Zero view or zero generation never validates.
	assert(!IsCurrentEventSource(EventSource{ 0, 2 }, State(22, 2)));
	assert(!IsCurrentEventSource(EventSource{ 22, 0 }, State(22, 2)));

	return 0;
}
