#include "F4TJoinState.h"

#include <cassert>
#include <cstddef>
#include <string_view>

namespace
{
	using F4T::JoinState::Event;
	using F4T::JoinState::Machine;
	using F4T::JoinState::State;

	constexpr State kStates[] = { State::Connecting,
		State::Authenticating,
		State::LoadingWorld,
		State::FlyOver,
		State::Spawning,
		State::Ready,
		State::Failed,
		State::Cancelled };

	constexpr std::string_view kStateIds[] = { "Connecting",
		"Authenticating",
		"LoadingWorld",
		"FlyOver",
		"Spawning",
		"Ready",
		"Failed",
		"Cancelled" };

	constexpr Event kEvents[] = { Event::Connected,
		Event::Authenticated,
		Event::WorldLoaded,
		Event::ReadyGateSatisfied,
		Event::FlyOverTimeout,
		Event::SpawnSettled,
		Event::Failed,
		Event::Cancelled };

	constexpr std::size_t kStateCount = sizeof(kStates) / sizeof(kStates[0]);
	constexpr std::size_t kEventCount = sizeof(kEvents) / sizeof(kEvents[0]);
	static_assert(sizeof(kStateIds) / sizeof(kStateIds[0]) == kStateCount);

	constexpr State kEligible[kStateCount][kEventCount] = {
		{ State::Authenticating, State::Connecting, State::Connecting, State::Connecting, State::Connecting, State::Connecting, State::Failed, State::Cancelled },
		{ State::Authenticating, State::LoadingWorld, State::Authenticating, State::Authenticating, State::Authenticating, State::Authenticating, State::Failed, State::Cancelled },
		{ State::LoadingWorld, State::LoadingWorld, State::FlyOver, State::LoadingWorld, State::LoadingWorld, State::LoadingWorld, State::Failed, State::Cancelled },
		{ State::FlyOver, State::FlyOver, State::FlyOver, State::Spawning, State::Spawning, State::FlyOver, State::Failed, State::Cancelled },
		{ State::Spawning, State::Spawning, State::Spawning, State::Spawning, State::Spawning, State::Ready, State::Failed, State::Cancelled },
		{ State::Ready, State::Ready, State::Ready, State::Ready, State::Ready, State::Ready, State::Failed, State::Cancelled },
		{ State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed },
		{ State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled }
	};

	constexpr State kIneligible[kStateCount][kEventCount] = {
		{ State::Authenticating, State::Connecting, State::Connecting, State::Connecting, State::Connecting, State::Connecting, State::Failed, State::Cancelled },
		{ State::Authenticating, State::LoadingWorld, State::Authenticating, State::Authenticating, State::Authenticating, State::Authenticating, State::Failed, State::Cancelled },
		{ State::LoadingWorld, State::LoadingWorld, State::Spawning, State::LoadingWorld, State::LoadingWorld, State::LoadingWorld, State::Failed, State::Cancelled },
		{ State::FlyOver, State::FlyOver, State::FlyOver, State::Spawning, State::Spawning, State::FlyOver, State::Failed, State::Cancelled },
		{ State::Spawning, State::Spawning, State::Spawning, State::Spawning, State::Spawning, State::Ready, State::Failed, State::Cancelled },
		{ State::Ready, State::Ready, State::Ready, State::Ready, State::Ready, State::Ready, State::Failed, State::Cancelled },
		{ State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed, State::Failed },
		{ State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled, State::Cancelled }
	};

	constexpr State kPreReady[] = { State::Connecting, State::Authenticating, State::LoadingWorld, State::FlyOver };
	constexpr std::size_t kPreReadyCount = sizeof(kPreReady) / sizeof(kPreReady[0]);

	constexpr Event kFlyOverStayingEvents[] = { Event::Connected, Event::Authenticated, Event::WorldLoaded, Event::SpawnSettled };
	constexpr std::size_t kFlyOverStayingCount = sizeof(kFlyOverStayingEvents) / sizeof(kFlyOverStayingEvents[0]);

	constexpr State kTerminals[] = { State::Failed, State::Cancelled };
	constexpr Event kTerminalEntries[] = { Event::Failed, Event::Cancelled };

	void CheckMatrix(const State (&a_expected)[kStateCount][kEventCount], bool a_flyOverEligible)
	{
		for (std::size_t stateIndex = 0; stateIndex < kStateCount; ++stateIndex) {
			for (std::size_t eventIndex = 0; eventIndex < kEventCount; ++eventIndex) {
				const auto from = kStates[stateIndex];
				const auto expected = a_expected[stateIndex][eventIndex];
				Machine    machine(from);
				const auto transition = machine.Advance(kEvents[eventIndex], a_flyOverEligible);
				assert(transition.state == expected);
				assert(machine.Current() == expected);
				assert(transition.changed == (expected != from));
				assert(transition.restoreCamera == (from == State::FlyOver && expected != State::FlyOver));
			}
		}
	}
}

int main()
{
	const Machine initial;
	assert(initial.Current() == State::Connecting);
	assert(Machine(State::Ready).Current() == State::Ready);

	Machine eligible;
	auto    transition = eligible.Advance(Event::Connected, true);
	assert(transition.changed);
	assert(transition.state == State::Authenticating);
	assert(!transition.restoreCamera);
	assert(eligible.Current() == State::Authenticating);

	transition = eligible.Advance(Event::Authenticated, true);
	assert(transition.changed);
	assert(transition.state == State::LoadingWorld);
	assert(!transition.restoreCamera);
	assert(eligible.Current() == State::LoadingWorld);

	transition = eligible.Advance(Event::WorldLoaded, true);
	assert(transition.changed);
	assert(transition.state == State::FlyOver);
	assert(!transition.restoreCamera);
	assert(eligible.Current() == State::FlyOver);

	transition = eligible.Advance(Event::ReadyGateSatisfied, true);
	assert(transition.changed);
	assert(transition.state == State::Spawning);
	assert(transition.restoreCamera);
	assert(eligible.Current() == State::Spawning);

	transition = eligible.Advance(Event::SpawnSettled, true);
	assert(transition.changed);
	assert(transition.state == State::Ready);
	assert(!transition.restoreCamera);
	assert(eligible.Current() == State::Ready);
	assert(F4T::JoinState::AllowsGameplayPublish(eligible.Current()));
	assert(F4T::JoinState::AllowsHud(eligible.Current()));

	Machine ineligible;
	ineligible.Advance(Event::Connected, false);
	ineligible.Advance(Event::Authenticated, false);
	transition = ineligible.Advance(Event::WorldLoaded, false);
	assert(transition.changed);
	assert(transition.state == State::Spawning);
	assert(!transition.restoreCamera);
	assert(ineligible.Current() == State::Spawning);
	transition = ineligible.Advance(Event::SpawnSettled, false);
	assert(transition.changed);
	assert(transition.state == State::Ready);
	assert(ineligible.Current() == State::Ready);

	Machine timeout;
	timeout.Advance(Event::Connected, true);
	timeout.Advance(Event::Authenticated, true);
	timeout.Advance(Event::WorldLoaded, true);
	assert(timeout.Current() == State::FlyOver);
	transition = timeout.Advance(Event::FlyOverTimeout, true);
	assert(transition.changed);
	assert(transition.state == State::Spawning);
	assert(transition.restoreCamera);
	transition = timeout.Advance(Event::SpawnSettled, true);
	assert(transition.changed);
	assert(transition.state == State::Ready);
	assert(timeout.Current() == State::Ready);

	CheckMatrix(kEligible, true);
	CheckMatrix(kIneligible, false);

	for (std::size_t stateIndex = 0; stateIndex < kPreReadyCount; ++stateIndex) {
		for (std::size_t eventIndex = 0; eventIndex < kEventCount; ++eventIndex) {
			assert(Machine(kPreReady[stateIndex]).Advance(kEvents[eventIndex], true).state != State::Ready);
			assert(Machine(kPreReady[stateIndex]).Advance(kEvents[eventIndex], false).state != State::Ready);
		}
	}
	for (std::size_t stateIndex = 0; stateIndex < kStateCount; ++stateIndex) {
		const auto from = kStates[stateIndex];
		const auto enteredEligible = Machine(from).Advance(Event::SpawnSettled, true);
		const auto enteredIneligible = Machine(from).Advance(Event::SpawnSettled, false);
		assert((from == State::Spawning) == (enteredEligible.changed && enteredEligible.state == State::Ready));
		assert((from == State::Spawning) == (enteredIneligible.changed && enteredIneligible.state == State::Ready));
	}

	for (std::size_t stateIndex = 0; stateIndex < kStateCount; ++stateIndex) {
		const auto state = kStates[stateIndex];
		assert(F4T::JoinState::StateId(state) == kStateIds[stateIndex]);
		assert(F4T::JoinState::AllowsGameplayPublish(state) == (state == State::Ready));
		assert(F4T::JoinState::AllowsHud(state) == (state == State::Ready));
		assert(F4T::JoinState::RequiresCameraTakeover(state) == (state == State::FlyOver));
		assert(F4T::JoinState::IsTerminal(state) == (state == State::Failed || state == State::Cancelled));
		const bool allowsOverlay = state != State::Ready && state != State::Failed && state != State::Cancelled;
		assert(F4T::JoinState::AllowsOverlay(state) == allowsOverlay);
	}

	{
		Machine machine(State::FlyOver);
		const auto restore = machine.Advance(Event::ReadyGateSatisfied, true);
		assert(restore.changed);
		assert(restore.restoreCamera);
		assert(restore.state == State::Spawning);
	}
	{
		Machine machine(State::FlyOver);
		const auto restore = machine.Advance(Event::FlyOverTimeout, true);
		assert(restore.changed);
		assert(restore.restoreCamera);
		assert(restore.state == State::Spawning);
	}
	{
		Machine machine(State::FlyOver);
		const auto restore = machine.Advance(Event::Failed, true);
		assert(restore.changed);
		assert(restore.restoreCamera);
		assert(restore.state == State::Failed);
	}
	{
		Machine machine(State::FlyOver);
		const auto restore = machine.Advance(Event::Cancelled, false);
		assert(restore.changed);
		assert(restore.restoreCamera);
		assert(restore.state == State::Cancelled);
	}
	for (std::size_t eventIndex = 0; eventIndex < kFlyOverStayingCount; ++eventIndex) {
		Machine    machine(State::FlyOver);
		const auto staying = machine.Advance(kFlyOverStayingEvents[eventIndex], true);
		assert(!staying.changed);
		assert(!staying.restoreCamera);
		assert(staying.state == State::FlyOver);
		assert(machine.Current() == State::FlyOver);
	}
	for (std::size_t stateIndex = 0; stateIndex < kStateCount; ++stateIndex) {
		const auto from = kStates[stateIndex];
		if (from == State::FlyOver) {
			continue;
		}
		for (std::size_t eventIndex = 0; eventIndex < kEventCount; ++eventIndex) {
			Machine machine(from);
			assert(!machine.Advance(kEvents[eventIndex], true).restoreCamera);
		}
	}

	for (std::size_t terminalIndex = 0; terminalIndex < 2; ++terminalIndex) {
		const auto terminal = kTerminals[terminalIndex];
		Machine    machine(State::Spawning);
		const auto entry = machine.Advance(kTerminalEntries[terminalIndex], true);
		assert(entry.changed);
		assert(entry.state == terminal);
		assert(!entry.restoreCamera);
		for (std::size_t eventIndex = 0; eventIndex < kEventCount; ++eventIndex) {
			const auto stuck = machine.Advance(kEvents[eventIndex], true);
			assert(!stuck.changed);
			assert(!stuck.restoreCamera);
			assert(stuck.state == terminal);
			assert(machine.Current() == terminal);
		}
		const auto stuckAgain = machine.Advance(kTerminalEntries[terminalIndex], true);
		assert(!stuckAgain.changed);
		assert(!stuckAgain.restoreCamera);
		assert(stuckAgain.state == terminal);
		assert(machine.Current() == terminal);
	}

	return 0;
}
