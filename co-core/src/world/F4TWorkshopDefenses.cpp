#include "F4TWorkshopDefenses.h"

#include <cmath>
#include <limits>

namespace F4T::WorkshopDefenses
{
	float ComputeDefenseRating(
		const std::vector<TurretStats>& a_turrets,
		const std::vector<SettlerStats>& a_settlers,
		bool a_upkeepUnpaid) noexcept
	{
		float rating = 0.0F;
		for (const auto& turret : a_turrets) {
			if (!turret.alive) {
				continue;
			}
			if (turret.needsPower && !turret.powered) {
				continue;
			}
			rating += turret.ratingValue;
		}
		for (const auto& settler : a_settlers) {
			if (!settler.alive || !settler.onGuard) {
				continue;
			}
			rating += SettlerGuardBaseRating + settler.weaponTierBonus;
		}
		if (a_upkeepUnpaid) {
			rating *= UnpaidUpkeepRatingScale;
		}
		return std::clamp(rating, 0.0F, RatingCap);
	}

	float StructureResistanceFromRating(float a_defenseRating) noexcept
	{
		const float rating = std::clamp(a_defenseRating, 0.0F, RatingCap);
		return (rating / RatingCap) * MaximumStructureResistance;
	}

	float ApplyStructureResistance(float a_damage, float a_defenseRating) noexcept
	{
		if (!std::isfinite(a_damage) || !(a_damage > 0.0F)) {
			return 0.0F;
		}
		const float resisted = a_damage * (1.0F - StructureResistanceFromRating(a_defenseRating));
		return resisted > 0.0F ? resisted : 0.0F;
	}

	std::vector<Workshop::BuildZoneId> SelectFullSimZones(
		const std::vector<std::pair<Workshop::BuildZoneId, std::uint32_t>>& a_underAttackByCount,
		std::uint32_t a_limit)
	{
		auto ranked = a_underAttackByCount;
		std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a_left, const auto& a_right) {
			if (a_left.second != a_right.second) {
				return a_left.second > a_right.second;
			}
			return a_left.first < a_right.first;
		});
		std::vector<Workshop::BuildZoneId> selected;
		selected.reserve((std::min)(ranked.size(), static_cast<std::size_t>(a_limit)));
		for (const auto& entry : ranked) {
			if (selected.size() >= a_limit) {
				break;
			}
			selected.push_back(entry.first);
		}
		return selected;
	}

	std::optional<HostilePresence> NearestHostileInRange(
		const TurretStats& a_turret,
		const std::vector<HostilePresence>& a_hostiles,
		std::optional<std::uint32_t> a_stickyConnectionId,
		double a_nowMonotonic,
		double a_stickyUntilMonotonic) noexcept
	{
		if (!std::isfinite(a_turret.rangeMeters) || !(a_turret.rangeMeters > 0.0F)) {
			return std::nullopt;
		}
		const double rangeSq = static_cast<double>(a_turret.rangeMeters) * static_cast<double>(a_turret.rangeMeters);
		if (a_stickyConnectionId && std::isfinite(a_stickyUntilMonotonic) && a_nowMonotonic <= a_stickyUntilMonotonic) {
			for (const auto& hostile : a_hostiles) {
				if (hostile.connectionId != *a_stickyConnectionId) {
					continue;
				}
				const double dx = hostile.x - a_turret.x;
				const double dy = hostile.y - a_turret.y;
				const double dz = hostile.z - a_turret.z;
				if (dx * dx + dy * dy + dz * dz <= rangeSq) {
					return hostile;
				}
				break;
			}
		}
		const HostilePresence* best{};
		double bestDist = (std::numeric_limits<double>::max)();
		for (const auto& hostile : a_hostiles) {
			const double dx = hostile.x - a_turret.x;
			const double dy = hostile.y - a_turret.y;
			const double dz = hostile.z - a_turret.z;
			const double dist = dx * dx + dy * dy + dz * dz;
			if (dist > rangeSq || dist >= bestDist) {
				continue;
			}
			best = &hostile;
			bestDist = dist;
		}
		if (!best) {
			return std::nullopt;
		}
		return *best;
	}
}
