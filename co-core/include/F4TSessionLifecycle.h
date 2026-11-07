#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::Session
{
	enum class LifecycleState : std::uint8_t
	{
		Connecting = 0,
		Connected = 1,
		Ready = 2,
		CharacterLoaded = 3,
		SpaceChange = 4,
		Disconnected = 5
	};

	[[nodiscard]] std::string_view ToWireName(LifecycleState a_state) noexcept;
	[[nodiscard]] bool IsSpawnable(LifecycleState a_state) noexcept;
	[[nodiscard]] bool IsTerminal(LifecycleState a_state) noexcept;
	[[nodiscard]] bool CanTransition(LifecycleState a_from, LifecycleState a_to) noexcept;

	struct TransitionResult
	{
		bool accepted{};
		LifecycleState state{ LifecycleState::Connecting };
		std::string reason;
	};

	[[nodiscard]] TransitionResult Transition(LifecycleState a_from, LifecycleState a_to);

	class LifecycleTracker
	{
	public:
		explicit LifecycleTracker(LifecycleState a_initial = LifecycleState::Connecting) noexcept;

		[[nodiscard]] LifecycleState State() const noexcept;
		[[nodiscard]] bool IsSpawnable() const noexcept;
		[[nodiscard]] TransitionResult MoveTo(LifecycleState a_next);
		void Reset() noexcept;

	private:
		LifecycleState _state{ LifecycleState::Connecting };
	};
}
