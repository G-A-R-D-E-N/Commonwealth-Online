#include "F4TWorkshopRaids.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace F4T::WorkshopRaids
{
	float ExplosiveDamage(ExplosiveKind a_kind) noexcept
	{
		switch (a_kind) {
		case ExplosiveKind::GrenadeOrMine:
			return 60.0F;
		case ExplosiveKind::Missile:
			return 150.0F;
		case ExplosiveKind::MiniNuke:
			return 600.0F;
		}
		return 0.0F;
	}

	float WeaponHitDamage(WorkshopMaterials::WeaponKind a_weaponKind) noexcept
	{
		switch (a_weaponKind) {
		case WorkshopMaterials::WeaponKind::Melee:
			return 25.0F;
		case WorkshopMaterials::WeaponKind::Light:
			return 15.0F;
		case WorkshopMaterials::WeaponKind::Rifle:
			return 30.0F;
		case WorkshopMaterials::WeaponKind::Heavy:
			return 60.0F;
		case WorkshopMaterials::WeaponKind::Explosive:
			return 0.0F;
		}
		return 0.0F;
	}

	float OwnerLocalHour(std::uint64_t a_unixSeconds, float a_timezoneOffsetHours) noexcept
	{
		const double localSeconds = static_cast<double>(a_unixSeconds) + static_cast<double>(a_timezoneOffsetHours) * 3600.0;
		const double secondsIntoDay = std::fmod(std::fmod(localSeconds, 86400.0) + 86400.0, 86400.0);
		return static_cast<float>(secondsIntoDay / 3600.0);
	}

	std::uint64_t UtcDayIndex(std::uint64_t a_unixSeconds) noexcept
	{
		return a_unixSeconds / 86400;
	}

	bool RaidDeclarationLive(const RaidDeclaration& a_declaration, double a_nowMonotonic) noexcept
	{
		return a_nowMonotonic <= a_declaration.expiresMonotonic;
	}

	bool ValidWindowSet(float a_startHour, float a_durationHours, float a_timezoneOffsetHours) noexcept
	{
		return a_startHour >= 0.0F && a_startHour < 24.0F && a_durationHours >= 1.0F && a_durationHours <= 24.0F &&
			a_timezoneOffsetHours >= -12.0F && a_timezoneOffsetHours <= 14.0F;
	}

	bool OutsiderDamageAllowed(const RaidConfig& a_config, float a_ownerLocalHour, bool a_ownerSide) noexcept
	{
		return WindowedOutsiderRaidAllowed(a_config, a_ownerLocalHour, a_ownerSide, true);
	}

	bool WindowedOutsiderRaidAllowed(
		const RaidConfig& a_config,
		float a_ownerLocalHour,
		bool a_ownerSide,
		bool a_declarationReady) noexcept
	{
		if (a_ownerSide) {
			return true;
		}
		if (a_config.mode == RaidMode::Off) {
			return false;
		}
		if (a_config.mode == RaidMode::Open) {
			return true;
		}
		if (!a_declarationReady) {
			return false;
		}
		if (!std::isfinite(a_ownerLocalHour) || !std::isfinite(a_config.windowStartHour) ||
			!std::isfinite(a_config.windowDurationHours) || a_config.windowDurationHours <= 0.0F) {
			return false;
		}
		if (a_config.windowDurationHours >= 24.0F) {
			return true;
		}
		const float hour = std::fmod(a_ownerLocalHour + 24.0F, 24.0F);
		const float start = std::fmod(a_config.windowStartHour + 24.0F, 24.0F);
		const float end = std::fmod(start + a_config.windowDurationHours, 24.0F);
		if (start <= end) {
			return hour >= start && hour < end;
		}
		return hour >= start || hour < end;
	}

	bool WindowOverlapsInterval(
		const RaidConfig& a_config,
		float a_timezoneOffsetHours,
		double a_startUnix,
		double a_endUnix) noexcept
	{
		if (!std::isfinite(a_config.windowStartHour) || !std::isfinite(a_timezoneOffsetHours) ||
			!std::isfinite(a_config.windowDurationHours) || a_config.windowDurationHours <= 0.0F) {
			return false;
		}
		if (a_config.windowDurationHours >= 24.0F) {
			return true;
		}
		const double offset = static_cast<double>(a_timezoneOffsetHours) * 3600.0;
		const double duration = static_cast<double>(a_config.windowDurationHours) * 3600.0;
		const double startOfWindow = static_cast<double>(a_config.windowStartHour) * 3600.0;
		const auto firstDay = static_cast<long long>(std::floor((a_startUnix + offset - duration) / 86400.0)) - 1;
		const auto lastDay = static_cast<long long>(std::floor((a_endUnix + offset) / 86400.0));
		for (auto day = firstDay; day <= lastDay; ++day) {
			const double windowStart = static_cast<double>(day) * 86400.0 + startOfWindow - offset;
			if (windowStart < a_endUnix && windowStart + duration > a_startUnix) {
				return true;
			}
		}
		return false;
	}

	float UtcHourOfDay(std::uint64_t a_unixSeconds) noexcept
	{
		return static_cast<float>(a_unixSeconds % 86400U) / 3600.0F;
	}

	std::uint8_t DamageStage(float a_health, float a_maxHealth) noexcept
	{
		if (!std::isfinite(a_health) || !std::isfinite(a_maxHealth) || !(a_maxHealth > 0.0F)) {
			return 0;
		}
		const float ratio = a_health / a_maxHealth;
		if (ratio <= 0.25F) {
			return 2;
		}
		if (ratio <= 0.50F) {
			return 1;
		}
		return 0;
	}

	AttackState RefreshAttack(const AttackState& a_current, double a_nowMonotonic, bool a_hostileHit) noexcept
	{
		AttackState next = a_current;
		if (a_hostileHit) {
			if (!next.underAttack) {
				next.underAttack = true;
				next.sinceMonotonic = a_nowMonotonic;
				next.attackerCount = 1;
			} else {
				next.sinceMonotonic = a_nowMonotonic;
				next.attackerCount = std::max<std::uint32_t>(1, next.attackerCount);
			}
			return next;
		}
		if (next.underAttack && a_nowMonotonic - next.sinceMonotonic >= AttackWindowSeconds) {
			next = {};
		}
		return next;
	}

	float ClaimHoldSeconds(float a_defenseRating, bool a_upkeepUnpaid) noexcept
	{
		if (a_upkeepUnpaid) {
			return 30.0F;
		}
		const float rating = std::clamp(a_defenseRating, 0.0F, 100.0F);
		return 60.0F + (rating / 100.0F) * 240.0F;
	}

	bool FactionLeaveBlocksRaid(std::uint64_t a_blockedUntilUnix, std::uint64_t a_nowUnix) noexcept
	{
		return a_blockedUntilUnix != 0 && a_nowUnix < a_blockedUntilUnix;
	}

	std::string_view RaidModeName(RaidMode a_mode) noexcept
	{
		switch (a_mode) {
		case RaidMode::Open:
			return "open";
		case RaidMode::Windowed:
			return "windowed";
		case RaidMode::Off:
			return "off";
		}
		return "open";
	}

	std::optional<RaidMode> ParseRaidMode(std::string_view a_name) noexcept
	{
		if (a_name == "open") {
			return RaidMode::Open;
		}
		if (a_name == "windowed") {
			return RaidMode::Windowed;
		}
		if (a_name == "off") {
			return RaidMode::Off;
		}
		return std::nullopt;
	}

	std::optional<ExplosiveKind> ParseExplosiveKind(std::string_view a_name) noexcept
	{
		if (a_name == "grenade" || a_name == "mine") {
			return ExplosiveKind::GrenadeOrMine;
		}
		if (a_name == "missile") {
			return ExplosiveKind::Missile;
		}
		if (a_name == "mininuke") {
			return ExplosiveKind::MiniNuke;
		}
		return std::nullopt;
	}

	std::string_view ExplosiveKindName(ExplosiveKind a_kind) noexcept
	{
		switch (a_kind) {
		case ExplosiveKind::GrenadeOrMine:
			return "grenade";
		case ExplosiveKind::Missile:
			return "missile";
		case ExplosiveKind::MiniNuke:
			return "mininuke";
		}
		return "grenade";
	}

	namespace
	{
		struct ExplosiveSource
		{
			std::uint32_t sourceId;
			ExplosiveKind kind;
		};

		constexpr std::array kExplosiveSources{
			ExplosiveSource{ 0x000EEBED, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000E5750, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x00107BD6, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000FF21D, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000FF21F, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010A33D, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000E56C2, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010771F, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010A340, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010A342, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010A344, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0010C669, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000EECE9, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x000E5752, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x00245F21, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0017A57F, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0017B3B3, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0017B3B2, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0015B07E, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0023D535, ExplosiveKind::GrenadeOrMine },
			ExplosiveSource{ 0x0003F6F8, ExplosiveKind::Missile },
			ExplosiveSource{ 0x000BD56F, ExplosiveKind::MiniNuke },
			ExplosiveSource{ 0x00065DF2, ExplosiveKind::MiniNuke },
			ExplosiveSource{ 0x001A7FF2, ExplosiveKind::MiniNuke },
			ExplosiveSource{ 0x001C6BFF, ExplosiveKind::MiniNuke },
			ExplosiveSource{ 0x00246741, ExplosiveKind::MiniNuke },
		};
	}

	std::optional<ExplosiveKind> ExplosiveKindForSource(std::uint32_t a_formId) noexcept
	{
		for (const auto& source : kExplosiveSources) {
			if (source.sourceId == a_formId) {
				return source.kind;
			}
		}
		return std::nullopt;
	}

	SettlementStanding ClassifyStanding(bool a_ownsSettlement, bool a_allowlisted, bool a_factionMate) noexcept
	{
		if (a_ownsSettlement) {
			return SettlementStanding::Owner;
		}
		if (a_factionMate) {
			return SettlementStanding::FactionMate;
		}
		if (a_allowlisted) {
			return SettlementStanding::Allowlisted;
		}
		return SettlementStanding::Raider;
	}
}
