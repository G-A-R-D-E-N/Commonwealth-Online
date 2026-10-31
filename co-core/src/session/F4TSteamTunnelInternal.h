#pragma once

#include "F4TSteamTunnel.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Transport::Internal
{
	[[nodiscard]] inline bool IsRelayableDatagram(std::span<const std::uint8_t> a_payload)
	{
		return !a_payload.empty() && a_payload.size() <= kMaximumRelayedDatagramBytes;
	}

	[[nodiscard]] inline std::string_view FailureText(const SteamConnectionEvent& a_event, std::string_view a_fallback)
	{
		return a_event.endDebug.empty() ? a_fallback : std::string_view{ a_event.endDebug };
	}

	inline void PushEvent(std::deque<TunnelEvent>& a_events, TunnelEvent a_event)
	{
		if (a_events.size() >= kMaximumTunnelEvents) a_events.pop_front();
		a_events.push_back(std::move(a_event));
	}

	[[nodiscard]] inline std::size_t PushPending(
		std::deque<std::vector<std::uint8_t>>& a_pending,
		std::size_t& a_bytes,
		const std::uint8_t* a_data,
		std::size_t a_size)
	{
		std::size_t evicted = 0;
		while (!a_pending.empty() &&
			(a_pending.size() >= kMaximumQueuedDatagrams || a_bytes + a_size > kMaximumQueuedBytes)) {
			a_bytes -= a_pending.front().size();
			a_pending.pop_front();
			++evicted;
		}
		a_pending.emplace_back(a_data, a_data + a_size);
		a_bytes += a_size;
		return evicted;
	}

	[[nodiscard]] inline std::vector<std::vector<std::uint8_t>> DrainPending(
		std::deque<std::vector<std::uint8_t>>& a_pending,
		std::size_t& a_bytes)
	{
		std::vector<std::vector<std::uint8_t>> drained;
		drained.reserve(a_pending.size());
		while (!a_pending.empty()) {
			drained.push_back(std::move(a_pending.front()));
			a_pending.pop_front();
		}
		a_bytes = 0;
		return drained;
	}
}
