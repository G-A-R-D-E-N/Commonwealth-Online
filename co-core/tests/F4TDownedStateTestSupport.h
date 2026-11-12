#pragma once

#include "F4TDownedState.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4TDownedStateTestSupport
{
	using F4T::DownedState::Config;
	using F4T::DownedState::Event;
	using F4T::DownedState::EventKind;
	using F4T::DownedState::RespawnVerdict;
	using F4T::DownedState::ReviveVerdict;
	using F4T::DownedState::State;
	using F4T::DownedState::Tracker;

	inline std::size_t CountKind(const std::vector<Event>& a_events, EventKind a_kind)
	{
		std::size_t count = 0;
		for (const auto& event : a_events) {
			if (event.kind == a_kind) {
				++count;
			}
		}
		return count;
	}

	inline const Event* FindKind(const std::vector<Event>& a_events, EventKind a_kind)
	{
		for (const auto& event : a_events) {
			if (event.kind == a_kind) {
				return &event;
			}
		}
		return nullptr;
	}

	inline State StateOf(const Tracker& a_tracker, std::uint32_t a_playerId)
	{
		const auto state = a_tracker.StateOf(a_playerId);
		assert(state.has_value());
		return *state;
	}

	// Constraint: blocks pinning the clock, grace or rate limit revive instantly.
	inline Config InstantConfig()
	{
		Config config;
		config.reviveChannelSeconds = 0.0;
		return config;
	}
}

