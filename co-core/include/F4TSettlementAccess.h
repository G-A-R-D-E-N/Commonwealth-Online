#pragma once

#include "F4TWorkshopTransactions.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementAccess
{
	inline constexpr std::size_t MaximumAllowedProfiles = 15;
	inline constexpr std::size_t MaximumSettlements = 1024;

	enum class ApplyStatus : std::uint8_t
	{
		Committed,
		InvalidRequest,
		UnknownBuildZone,
		NotOwner,
		AlreadyAllowed,
		NotAllowed,
		Full,
		SettlementLimit,
		RateLimited,
		PersistenceUnavailable,
		UnderAttack,
		InsufficientMaterials,
		NothingToRepair,
		ChargeFailed,
		TooFar
	};

	enum class SettlementKind : std::uint8_t
	{
		Vanilla,
		Camp
	};

	struct Settlement
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string ownerProfileId;
		std::vector<std::string> allowedProfileIds;
		SettlementKind kind{ SettlementKind::Vanilla };
		float workbenchHp{ 2000.0F };
		bool workbenchBroken{};
		std::uint64_t workbenchBrokenUnix{};
		std::uint64_t protectedUntilUnix{};
		std::uint64_t upkeepPaidUntil{};
	};

	[[nodiscard]] bool ValidProfileId(std::string_view a_profileId) noexcept;
	[[nodiscard]] float WorkbenchMaxHpFor(SettlementKind a_kind) noexcept;
	[[nodiscard]] inline bool UnownedZoneOpenToBuild(bool a_namedZone) noexcept
	{
		return !a_namedZone;
	}

	class Table
	{
	public:
		[[nodiscard]] bool CanBuild(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId) const;
		[[nodiscard]] bool CanPlace(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId, bool a_namedZone) const;
		[[nodiscard]] std::optional<Settlement> Find(Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::vector<Settlement> Snapshot() const;
		[[nodiscard]] bool Restore(const std::vector<Settlement>& a_settlements);
		ApplyStatus SetOwner(
			Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::optional<SettlementKind> a_kind = std::nullopt);
		ApplyStatus Grant(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId);
		ApplyStatus Revoke(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId);
		ApplyStatus ApplyWorkbenchDamage(Workshop::BuildZoneId a_buildZoneId, float a_damage, std::uint64_t a_unixSeconds);
		ApplyStatus MarkProtectedUntil(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_untilUnix);
		ApplyStatus SetUpkeepPaidUntil(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_untilUnix);
		ApplyStatus RepairUnclaimedWorkbench(Workshop::BuildZoneId a_buildZoneId);
		ApplyStatus RepairWorkbench(Workshop::BuildZoneId a_buildZoneId, float a_amount);

	private:
		struct Record
		{
			std::string ownerProfileId;
			std::set<std::string> allowedProfileIds;
			SettlementKind kind{ SettlementKind::Vanilla };
			float workbenchHp{ 2000.0F };
			bool workbenchBroken{};
			std::uint64_t workbenchBrokenUnix{};
			std::uint64_t protectedUntilUnix{};
			std::uint64_t upkeepPaidUntil{};
		};

		[[nodiscard]] static Settlement View(Workshop::BuildZoneId a_buildZoneId, const Record& a_record);

		std::map<Workshop::BuildZoneId, Record> m_records;
	};
}
