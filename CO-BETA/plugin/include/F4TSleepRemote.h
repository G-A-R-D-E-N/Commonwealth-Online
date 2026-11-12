#pragma once

#include "F4TSleepBedIdentity.h"
#include "RE/B/BSPointerHandle.h"

namespace F4T::SleepRemote
{
	enum class LookupStatus { Absent, Pending, Ready };
	struct SleepLookup
	{
		LookupStatus status{ LookupStatus::Absent };
		RE::ObjectRefHandle bed;
		std::uint32_t marker{};
	};
	void HandlePacket(const nlohmann::json& a_packet);
	void Forget(std::uint32_t a_playerId);
	void Clear();
	[[nodiscard]] SleepLookup Find(std::uint32_t a_playerId);
}
