#pragma once

#include "F4TNpcSync.h"

#include <cstdint>
#include <string>

namespace F4T::NpcCoop
{
	inline constexpr float kMinimumNpcHitDamage = 0.000001F;
	inline constexpr float kMaximumNpcHitDamage = 10000.0F;

	struct NpcHitIdentity
	{
		std::uint32_t npcId{};
		std::uint32_t sourceFormId{};
		std::string cellId;
		std::string worldspaceId;
		std::uint64_t authorityEpoch{};
	};

	struct NpcHitClaim
	{
		NpcHitIdentity identity;
		std::uint32_t senderPlayerId{};
		std::uint32_t sequence{};
		float damage{};
		std::uint32_t weaponFormId{};
	};
}

namespace F4T::NpcCoop
{
	inline constexpr float kMaximumNpcDamage = 1000.0F;

	struct NpcDamageClaim
	{
		NpcHitIdentity identity;
		std::uint32_t targetPlayerId{};
		std::uint32_t sequence{};
		float damage{};
		std::uint32_t weaponFormId{};
	};
}

namespace F4T::NpcSync::Health
{
	bool EnqueueNpcHit(F4T::NpcCoop::NpcHitClaim a_claim);
}
