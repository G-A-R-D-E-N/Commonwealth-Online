#pragma once

#include <cstddef>
#include <cstdint>

namespace F4T::WorkshopScale
{
	inline constexpr std::uint32_t SessionPlayers = 100;
	inline constexpr std::uint32_t MaxObjectsPerSettlement = 2000;
	inline constexpr std::uint32_t MaxObjectsPerServer = 200000;
	inline constexpr std::uint32_t MaxVanillaSettlementsPerProfile = 2;
	inline constexpr std::uint32_t MaxVanillaSettlementsPerFaction = 4;
	inline constexpr std::uint32_t MaxCampsPerServer = 300;
	inline constexpr float InterestExtraMeters = 150.0F;
	inline constexpr std::uint32_t FullDefenseSimZones = 8;
	inline constexpr float OpsPerPlayerPerSecond = 4.0F;
	inline constexpr float OpsBurstPerPlayer = 8.0F;
	inline constexpr float OpsPerZonePerSecond = 40.0F;
}
