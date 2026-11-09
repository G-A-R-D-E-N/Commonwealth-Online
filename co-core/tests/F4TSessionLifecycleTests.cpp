#include "F4TSessionLifecycle.h"

#include <cassert>
#include <string_view>

int main()
{
	using namespace F4T::Session;

	assert(ToWireName(LifecycleState::Connecting) == "connecting");
	assert(ToWireName(LifecycleState::Connected) == "connected");
	assert(ToWireName(LifecycleState::Ready) == "ready");
	assert(ToWireName(LifecycleState::CharacterLoaded) == "characterLoaded");
	assert(ToWireName(LifecycleState::SpaceChange) == "spaceChange");
	assert(ToWireName(LifecycleState::Disconnected) == "disconnected");

	assert(!IsSpawnable(LifecycleState::Connecting));
	assert(!IsSpawnable(LifecycleState::Connected));
	assert(IsSpawnable(LifecycleState::Ready));
	assert(IsSpawnable(LifecycleState::CharacterLoaded));
	assert(IsSpawnable(LifecycleState::SpaceChange));
	assert(!IsSpawnable(LifecycleState::Disconnected));

	assert(IsTerminal(LifecycleState::Disconnected));
	assert(!IsTerminal(LifecycleState::SpaceChange));

	assert(CanTransition(LifecycleState::Connecting, LifecycleState::Connected));
	assert(CanTransition(LifecycleState::Connecting, LifecycleState::Disconnected));
	assert(!CanTransition(LifecycleState::Connecting, LifecycleState::Ready));
	assert(!CanTransition(LifecycleState::Connecting, LifecycleState::CharacterLoaded));

	assert(CanTransition(LifecycleState::Connected, LifecycleState::Ready));
	assert(!CanTransition(LifecycleState::Connected, LifecycleState::CharacterLoaded));
	assert(CanTransition(LifecycleState::Connected, LifecycleState::Disconnected));

	assert(CanTransition(LifecycleState::Ready, LifecycleState::CharacterLoaded));
	assert(CanTransition(LifecycleState::Ready, LifecycleState::SpaceChange));
	assert(!CanTransition(LifecycleState::Ready, LifecycleState::Connected));
	assert(CanTransition(LifecycleState::CharacterLoaded, LifecycleState::SpaceChange));
	assert(!CanTransition(LifecycleState::CharacterLoaded, LifecycleState::Ready));
	assert(CanTransition(LifecycleState::SpaceChange, LifecycleState::CharacterLoaded));
	assert(!CanTransition(LifecycleState::SpaceChange, LifecycleState::Ready));
	assert(CanTransition(LifecycleState::Disconnected, LifecycleState::Connecting));
	assert(!CanTransition(LifecycleState::Disconnected, LifecycleState::Ready));

	assert(CanTransition(LifecycleState::Ready, LifecycleState::Ready));

	const auto illegal = Transition(LifecycleState::Connecting, LifecycleState::CharacterLoaded);
	assert(!illegal.accepted);
	assert(illegal.state == LifecycleState::Connecting);
	assert(!illegal.reason.empty());

	const auto legal = Transition(LifecycleState::Connected, LifecycleState::Ready);
	assert(legal.accepted);
	assert(legal.state == LifecycleState::Ready);
	assert(legal.reason.empty());

	LifecycleTracker tracker;
	assert(tracker.State() == LifecycleState::Connecting);
	assert(!tracker.IsSpawnable());
	assert(tracker.MoveTo(LifecycleState::Ready).accepted == false);
	assert(tracker.State() == LifecycleState::Connecting);

	assert(tracker.MoveTo(LifecycleState::Connected).accepted);
	assert(tracker.MoveTo(LifecycleState::Ready).accepted);
	assert(tracker.IsSpawnable());
	assert(tracker.MoveTo(LifecycleState::CharacterLoaded).accepted);
	assert(tracker.MoveTo(LifecycleState::SpaceChange).accepted);
	assert(tracker.MoveTo(LifecycleState::CharacterLoaded).accepted);
	assert(tracker.MoveTo(LifecycleState::Disconnected).accepted);
	assert(!tracker.IsSpawnable());

	tracker.Reset();
	assert(tracker.State() == LifecycleState::Connecting);

	LifecycleTracker reconnect{ LifecycleState::Disconnected };
	assert(reconnect.State() == LifecycleState::Connecting);

	return 0;
}
