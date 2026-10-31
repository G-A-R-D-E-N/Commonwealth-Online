#pragma once

#include "F4TClientCompatibility.h"
#include "F4TObjectState.h"

#include <cstdint>
#include <nlohmann/json.hpp>

namespace F4T::ObjectSync
{
	inline constexpr std::size_t kMaximumQueue = 64;
	inline constexpr std::size_t kMaximumTrackedRefs = 32;
	inline constexpr std::uint32_t kMaximumWriteRetries = 30;

	inline void AddSessionCapabilities(F4T::ClientCompatibility::Metadata& a_metadata)
	{
		a_metadata.capabilities.emplace_back("object-state-v1");
	}

	void NoteWelcome(const nlohmann::json& a_packet);
	void NoteCapabilities(const nlohmann::json& a_packet);
	void EnqueuePacket(const nlohmann::json& a_packet);
	void Tick(std::uint64_t a_nowMs);
	void SetSharedDoors(bool a_sharedDoors);
	void RefreshTrackedLocks();
	void Reset();
}
