#pragma once

#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopTransactions.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::WorkshopRaids
{
	enum class RaidMode : std::uint8_t
	{
		Open,
		Windowed,
		Off
	};

	enum class ExplosiveKind : std::uint8_t
	{
		GrenadeOrMine,
		Missile,
		MiniNuke
	};

	enum class HitRefusal : std::uint8_t
	{
		SettlementProtected,
		FactionLeaveBlocked,
		RaidWindowClosed,
		FactionMate,
		WeaponUnknown
	};

	enum class SettlementStanding : std::uint8_t
	{
		Raider,
		Allowlisted,
		FactionMate,
		Owner
	};

	inline constexpr float AttackWindowSeconds = 600.0F;
	inline constexpr float WorkbenchMaxHp = 2000.0F;
	inline constexpr float CampWorkbenchMaxHp = 1000.0F;
	inline constexpr Workshop::ObjectId WorkbenchObjectId = 0;
	inline constexpr float PostClaimProtectionHours = 12.0F;
	inline constexpr float UnclaimedWorkbenchRepairFraction = 0.25F;
	inline constexpr float UnclaimedRepairTimeoutSeconds = 1800.0F;
	inline constexpr float DeclarationWarningSeconds = 1800.0F;
	inline constexpr float FactionLeaveRaidCooldownHours = 48.0F;
	inline constexpr std::uint32_t MaxDeclarationsPerGroupPerDay = 3;
	[[nodiscard]] constexpr float StructureHitIntervalSeconds(WorkshopMaterials::WeaponKind a_weaponKind) noexcept
	{
		switch (a_weaponKind) {
		case WorkshopMaterials::WeaponKind::Melee:
			return 0.4F;
		case WorkshopMaterials::WeaponKind::Light:
			return 0.15F;
		case WorkshopMaterials::WeaponKind::Rifle:
			return 0.3F;
		case WorkshopMaterials::WeaponKind::Heavy:
			return 0.1F;
		case WorkshopMaterials::WeaponKind::Explosive:
			return 1.0F;
		}
		return 1.0F;
	}
	[[nodiscard]] constexpr std::string_view HitRefusalName(HitRefusal a_refusal) noexcept
	{
		switch (a_refusal) {
		case HitRefusal::SettlementProtected:
			return "settlementProtected";
		case HitRefusal::FactionLeaveBlocked:
			return "factionLeaveBlocked";
		case HitRefusal::RaidWindowClosed:
			return "raidWindowClosed";
		case HitRefusal::FactionMate:
			return "factionMate";
		case HitRefusal::WeaponUnknown:
			return "weaponUnknown";
		}
		return "raidWindowClosed";
	}
	inline constexpr float StructureRepairCooldownSeconds = 5.0F;
	inline constexpr std::size_t MaximumStructureHitCooldowns = 4096;
	inline constexpr float StructureDamageBudgetPerSecond = 600.0F;
	inline constexpr std::size_t MaximumStructureDamageBudgets = 4096;
	inline constexpr double RaidHitRefusedNoticeSeconds = 10.0;
	inline constexpr double ExplosiveEquipWindowSeconds = 30.0;
	inline constexpr std::uint32_t kMiniNukeSourceFormId{ 0x000BD56F };
	inline constexpr std::uint32_t kMissileSourceFormId{ 0x0003F6F8 };
	inline constexpr std::uint32_t kFragGrenadeSourceFormId{ 0x000EEBED };
	inline constexpr std::uint32_t kSidearmWeaponFormId{ 0x00004822 };

	struct AttackState
	{
		bool underAttack{};
		double sinceMonotonic{};
		std::uint32_t attackerCount{};
	};

	struct RaidConfig
	{
		RaidMode mode{ RaidMode::Open };
		float windowStartHour{};
		float windowDurationHours{ 2.0F };
	};

	struct RaidDeclaration
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string declarerProfileId;
		std::string factionId;
		double readyMonotonic{};
		double expiresMonotonic{};
		double readyUnix{};
		double expiresUnix{};
	};

	[[nodiscard]] float ExplosiveDamage(ExplosiveKind a_kind) noexcept;
	[[nodiscard]] float WeaponHitDamage(WorkshopMaterials::WeaponKind a_weaponKind) noexcept;
	[[nodiscard]] float OwnerLocalHour(std::uint64_t a_unixSeconds, float a_timezoneOffsetHours) noexcept;
	[[nodiscard]] std::uint64_t UtcDayIndex(std::uint64_t a_unixSeconds) noexcept;
	[[nodiscard]] bool RaidDeclarationLive(const RaidDeclaration& a_declaration, double a_nowMonotonic) noexcept;
	[[nodiscard]] bool ValidWindowSet(float a_startHour, float a_durationHours, float a_timezoneOffsetHours) noexcept;
	[[nodiscard]] bool OutsiderDamageAllowed(
		const RaidConfig& a_config,
		float a_ownerLocalHour,
		bool a_ownerSide) noexcept;
	[[nodiscard]] bool WindowedOutsiderRaidAllowed(
		const RaidConfig& a_config,
		float a_ownerLocalHour,
		bool a_ownerSide,
		bool a_declarationReady) noexcept;
	[[nodiscard]] bool WindowOverlapsInterval(const RaidConfig& a_config, float a_timezoneOffsetHours, double a_startUnix, double a_endUnix) noexcept;
	[[nodiscard]] float UtcHourOfDay(std::uint64_t a_unixSeconds) noexcept;
	[[nodiscard]] AttackState RefreshAttack(
		const AttackState& a_current,
		double a_nowMonotonic,
		bool a_hostileHit) noexcept;
	[[nodiscard]] float ClaimHoldSeconds(float a_defenseRating, bool a_upkeepUnpaid) noexcept;
	[[nodiscard]] std::uint8_t DamageStage(float a_health, float a_maxHealth) noexcept;
	[[nodiscard]] bool FactionLeaveBlocksRaid(std::uint64_t a_blockedUntilUnix, std::uint64_t a_nowUnix) noexcept;
	[[nodiscard]] std::string_view RaidModeName(RaidMode a_mode) noexcept;
	[[nodiscard]] std::optional<RaidMode> ParseRaidMode(std::string_view a_name) noexcept;
	[[nodiscard]] std::optional<ExplosiveKind> ParseExplosiveKind(std::string_view a_name) noexcept;
	[[nodiscard]] std::string_view ExplosiveKindName(ExplosiveKind a_kind) noexcept;
	[[nodiscard]] std::optional<ExplosiveKind> ExplosiveKindForSource(std::uint32_t a_formId) noexcept;
	[[nodiscard]] SettlementStanding ClassifyStanding(bool a_ownsSettlement, bool a_allowlisted, bool a_factionMate) noexcept;
}
