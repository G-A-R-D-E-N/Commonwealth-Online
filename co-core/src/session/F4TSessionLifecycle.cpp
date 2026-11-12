#include "F4TSessionLifecycle.h"

#include <string>

namespace F4T::Session
{
	std::string_view ToWireName(LifecycleState a_state) noexcept
	{
		switch (a_state) {
		case LifecycleState::Connecting:
			return "connecting";
		case LifecycleState::Connected:
			return "connected";
		case LifecycleState::Ready:
			return "ready";
		case LifecycleState::CharacterLoaded:
			return "characterLoaded";
		case LifecycleState::SpaceChange:
			return "spaceChange";
		case LifecycleState::Disconnected:
			return "disconnected";
		}
		return "connecting";
	}

	bool IsSpawnable(LifecycleState a_state) noexcept
	{
		return a_state == LifecycleState::Ready ||
			a_state == LifecycleState::CharacterLoaded ||
			a_state == LifecycleState::SpaceChange;
	}

	bool IsTerminal(LifecycleState a_state) noexcept
	{
		return a_state == LifecycleState::Disconnected;
	}

	bool CanTransition(LifecycleState a_from, LifecycleState a_to) noexcept
	{
		if (a_from == a_to) {
			return true;
		}
		switch (a_from) {
		case LifecycleState::Connecting:
			return a_to == LifecycleState::Connected || a_to == LifecycleState::Disconnected;
		case LifecycleState::Connected:
			return a_to == LifecycleState::Ready || a_to == LifecycleState::Disconnected;
		case LifecycleState::Ready:
			return a_to == LifecycleState::CharacterLoaded ||
				a_to == LifecycleState::SpaceChange ||
				a_to == LifecycleState::Disconnected;
		case LifecycleState::CharacterLoaded:
			return a_to == LifecycleState::SpaceChange || a_to == LifecycleState::Disconnected;
		case LifecycleState::SpaceChange:
			return a_to == LifecycleState::CharacterLoaded || a_to == LifecycleState::Disconnected;
		case LifecycleState::Disconnected:
			return a_to == LifecycleState::Connecting;
		}
		return false;
	}

	TransitionResult Transition(LifecycleState a_from, LifecycleState a_to)
	{
		if (CanTransition(a_from, a_to)) {
			return TransitionResult{ true, a_to, {} };
		}
		std::string reason{ "illegal session lifecycle transition " };
		reason += ToWireName(a_from);
		reason += " -> ";
		reason += ToWireName(a_to);
		return TransitionResult{ false, a_from, std::move(reason) };
	}

	LifecycleTracker::LifecycleTracker(LifecycleState a_initial) noexcept :
		_state(IsTerminal(a_initial) ? LifecycleState::Connecting : a_initial)
	{}

	LifecycleState LifecycleTracker::State() const noexcept
	{
		return _state;
	}

	bool LifecycleTracker::IsSpawnable() const noexcept
	{
		return F4T::Session::IsSpawnable(_state);
	}

	TransitionResult LifecycleTracker::MoveTo(LifecycleState a_next)
	{
		auto result = Transition(_state, a_next);
		_state = result.state;
		return result;
	}

	void LifecycleTracker::Reset() noexcept
	{
		_state = LifecycleState::Connecting;
	}
}
