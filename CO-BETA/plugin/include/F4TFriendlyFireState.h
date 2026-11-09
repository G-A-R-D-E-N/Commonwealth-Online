#pragma once

#include <nlohmann/json.hpp>

#include <atomic>

namespace F4T::FriendlyFireState
{
	// Written on the network thread, read on the game thread.
	inline std::atomic_bool& Storage() noexcept
	{
		static std::atomic_bool enabled{ true };
		return enabled;
	}

	inline void NoteWelcome(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("friendlyFire");
		if (found == a_packet.end() || !found->is_boolean()) {
			return;
		}
		Storage().store(found->get<bool>(), std::memory_order_relaxed);
	}

	inline bool Enabled() noexcept
	{
		return Storage().load(std::memory_order_relaxed);
	}
}
