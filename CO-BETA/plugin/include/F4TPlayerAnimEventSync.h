#pragma once

#include <nlohmann/json.hpp>

#include <atomic>

namespace F4T::PlayerAnimEventSync
{
	void Tick();
	void HandleRelayedPacket(const nlohmann::json& a_packet);

	namespace InstallState
	{
		inline std::atomic<const void*> holder{ nullptr };
	}

	inline bool IsInstalled() noexcept
	{
		return InstallState::holder.load(std::memory_order_acquire) != nullptr;
	}
}
