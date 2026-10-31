#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::JoinState
{
	enum class State : std::uint8_t
	{
		Connecting,
		Authenticating,
		LoadingWorld,
		FlyOver,
		Spawning,
		Ready,
		Failed,
		Cancelled
	};

	enum class Event : std::uint8_t
	{
		Connected,
		Authenticated,
		WorldLoaded,
		ReadyGateSatisfied,
		FlyOverTimeout,
		SpawnSettled,
		Failed,
		Cancelled
	};

	struct Transition
	{
		State state{ State::Connecting };
		bool  changed{ false };
		bool  restoreCamera{ false };
	};

	[[nodiscard]] constexpr bool IsTerminal(State a_state)
	{
		return a_state == State::Failed || a_state == State::Cancelled;
	}

	[[nodiscard]] constexpr bool RequiresCameraTakeover(State a_state)
	{
		return a_state == State::FlyOver;
	}

	[[nodiscard]] constexpr bool AllowsOverlay(State a_state)
	{
		switch (a_state) {
		case State::Connecting:
		case State::Authenticating:
		case State::LoadingWorld:
		case State::FlyOver:
		case State::Spawning:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool AllowsGameplayPublish(State a_state)
	{
		return a_state == State::Ready;
	}

	[[nodiscard]] constexpr bool AllowsHud(State a_state)
	{
		return a_state == State::Ready;
	}

	[[nodiscard]] constexpr std::string_view StateId(State a_state)
	{
		switch (a_state) {
		case State::Connecting:
			return "Connecting";
		case State::Authenticating:
			return "Authenticating";
		case State::LoadingWorld:
			return "LoadingWorld";
		case State::FlyOver:
			return "FlyOver";
		case State::Spawning:
			return "Spawning";
		case State::Ready:
			return "Ready";
		case State::Failed:
			return "Failed";
		case State::Cancelled:
			return "Cancelled";
		}
		return "Unknown";
	}

	class Machine
	{
	public:
		explicit constexpr Machine(State a_initial = State::Connecting) :
			state_(a_initial)
		{}

		[[nodiscard]] constexpr State Current() const { return state_; }

		[[nodiscard]] Transition Advance(Event a_event, bool a_flyOverEligible)
		{
			Transition transition{ state_, false, false };
			if (IsTerminal(state_)) {
				return transition;
			}

			State next = state_;
			switch (a_event) {
			case Event::Failed:
				next = State::Failed;
				break;
			case Event::Cancelled:
				next = State::Cancelled;
				break;
			case Event::Connected:
				if (state_ == State::Connecting) {
					next = State::Authenticating;
				}
				break;
			case Event::Authenticated:
				if (state_ == State::Authenticating) {
					next = State::LoadingWorld;
				}
				break;
			case Event::WorldLoaded:
				if (state_ == State::LoadingWorld) {
					next = a_flyOverEligible ? State::FlyOver : State::Spawning;
				}
				break;
			case Event::ReadyGateSatisfied:
				if (state_ == State::FlyOver) {
					next = State::Spawning;
				}
				break;
			case Event::FlyOverTimeout:
				if (state_ == State::FlyOver) {
					next = State::Spawning;
				}
				break;
			case Event::SpawnSettled:
				if (state_ == State::Spawning) {
					next = State::Ready;
				}
				break;
			}

			transition.restoreCamera = RequiresCameraTakeover(state_) && next != State::FlyOver;
			transition.changed = next != state_;
			transition.state = next;
			state_ = next;
			return transition;
		}

	private:
		State state_;
	};
}
