#pragma once

#include <cstdint>

namespace F4T::MainMenuLauncher
{
	struct EventSource
	{
		std::uint64_t view{ 0 };
		std::uint64_t generation{ 0 };
	};

	struct EventSourceState
	{
		std::uint64_t currentView{ 0 };
		std::uint64_t currentGeneration{ 0 };
		bool handleValid{ false };
		bool desiredVisible{ false };
		bool runtimeReady{ false };
	};

	[[nodiscard]] constexpr bool IsCurrentEventSource(
		const EventSource& a_source,
		const EventSourceState& a_state) noexcept
	{
		return a_state.runtimeReady &&
			a_source.view != 0 &&
			a_source.view == a_state.currentView &&
			a_source.generation != 0 &&
			a_source.generation == a_state.currentGeneration &&
			a_state.handleValid &&
			a_state.desiredVisible;
	}
}
