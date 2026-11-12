#pragma once

#include "F4TWorkshopScale.h"
#include "F4TWorkshopTransactions.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::WorkshopDefenses
{
	inline constexpr float RatingCap = 100.0F;
	inline constexpr float SettlerGuardBaseRating = 3.0F;
	inline constexpr float UnpaidUpkeepRatingScale = 0.5F;
	inline constexpr float MaximumStructureResistance = 0.30F;
	inline constexpr float DefaultTurretRating = 4.0F;
	inline constexpr float DefaultTurretDamage = 25.0F;
	inline constexpr float DefaultTurretRangeMeters = 40.0F;
	inline constexpr float DefaultTurretFireIntervalSeconds = 1.0F;
	inline constexpr float TargetStickinessSeconds = 2.0F;

	struct TurretStats
	{
		Workshop::ObjectId objectId{};
		float ratingValue{ DefaultTurretRating };
		bool needsPower{};
		bool alive{ true };
		bool powered{};
		float rangeMeters{ DefaultTurretRangeMeters };
		float damage{ DefaultTurretDamage };
		float fireIntervalSeconds{ DefaultTurretFireIntervalSeconds };
		double x{};
		double y{};
		double z{};
	};

	struct SettlerStats
	{
		Workshop::ObjectId settlerId{};
		bool alive{ true };
		bool onGuard{};
		float weaponTierBonus{};
	};

	struct HostilePresence
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::string profileId;
		double x{};
		double y{};
		double z{};
	};

	[[nodiscard]] float ComputeDefenseRating(
		const std::vector<TurretStats>& a_turrets,
		const std::vector<SettlerStats>& a_settlers,
		bool a_upkeepUnpaid) noexcept;

	[[nodiscard]] float StructureResistanceFromRating(float a_defenseRating) noexcept;

	[[nodiscard]] float ApplyStructureResistance(float a_damage, float a_defenseRating) noexcept;

	[[nodiscard]] std::vector<Workshop::BuildZoneId> SelectFullSimZones(
		const std::vector<std::pair<Workshop::BuildZoneId, std::uint32_t>>& a_underAttackByCount,
		std::uint32_t a_limit = WorkshopScale::FullDefenseSimZones);

	[[nodiscard]] std::optional<HostilePresence> NearestHostileInRange(
		const TurretStats& a_turret,
		const std::vector<HostilePresence>& a_hostiles,
		std::optional<std::uint32_t> a_stickyConnectionId,
		double a_nowMonotonic,
		double a_stickyUntilMonotonic) noexcept;
}
