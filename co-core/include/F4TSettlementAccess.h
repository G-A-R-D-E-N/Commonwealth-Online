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
		PersistenceUnavailable
	};

	struct Settlement
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string ownerProfileId;
		std::vector<std::string> allowedProfileIds;
	};

	[[nodiscard]] bool ValidProfileId(std::string_view a_profileId) noexcept;

	class Table
	{
	public:
		[[nodiscard]] bool CanBuild(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId) const;
		[[nodiscard]] std::optional<Settlement> Find(Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::vector<Settlement> Snapshot() const;
		[[nodiscard]] bool Restore(const std::vector<Settlement>& a_settlements);
		ApplyStatus SetOwner(Workshop::BuildZoneId a_buildZoneId, std::string_view a_ownerProfileId);
		ApplyStatus Grant(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId);
		ApplyStatus Revoke(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId);

	private:
		struct Record
		{
			std::string ownerProfileId;
			std::set<std::string> allowedProfileIds;
		};

		[[nodiscard]] static Settlement View(Workshop::BuildZoneId a_buildZoneId, const Record& a_record);

		std::map<Workshop::BuildZoneId, Record> m_records;
	};
}
