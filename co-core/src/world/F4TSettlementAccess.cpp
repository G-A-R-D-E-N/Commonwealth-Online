#include "F4TSettlementAccess.h"

#include "F4TIdentityProtocol.h"

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::SettlementAccess
{
	bool ValidProfileId(std::string_view a_profileId) noexcept
	{
		return !a_profileId.empty() &&
		       std::isspace(static_cast<unsigned char>(a_profileId.front())) == 0 &&
		       std::isspace(static_cast<unsigned char>(a_profileId.back())) == 0 &&
		       Identity::IsValidProfileId(a_profileId);
	}

	bool Table::CanBuild(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId) const
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return true;
		}
		return a_profileId == found->second.ownerProfileId ||
		       found->second.allowedProfileIds.contains(std::string{ a_profileId });
	}

	std::optional<Settlement> Table::Find(Workshop::BuildZoneId a_buildZoneId) const
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return std::nullopt;
		}
		return View(found->first, found->second);
	}

	std::vector<Settlement> Table::Snapshot() const
	{
		std::vector<Settlement> result;
		result.reserve(m_records.size());
		for (const auto& [buildZoneId, record] : m_records) {
			result.push_back(View(buildZoneId, record));
		}
		return result;
	}

	bool Table::Restore(const std::vector<Settlement>& a_settlements)
	{
		std::map<Workshop::BuildZoneId, Record> restored;
		for (const auto& settlement : a_settlements) {
			if (settlement.buildZoneId == 0 || restored.size() >= MaximumSettlements ||
				restored.contains(settlement.buildZoneId) || !ValidProfileId(settlement.ownerProfileId) ||
				settlement.allowedProfileIds.size() > MaximumAllowedProfiles) {
				return false;
			}
			Record record{ settlement.ownerProfileId, {} };
			for (const auto& profileId : settlement.allowedProfileIds) {
				if (!ValidProfileId(profileId) || profileId == settlement.ownerProfileId ||
					!record.allowedProfileIds.insert(profileId).second) {
					return false;
				}
			}
			restored.emplace(settlement.buildZoneId, std::move(record));
		}
		m_records = std::move(restored);
		return true;
	}

	ApplyStatus Table::SetOwner(Workshop::BuildZoneId a_buildZoneId, std::string_view a_ownerProfileId)
	{
		if (a_buildZoneId == 0) {
			return ApplyStatus::InvalidRequest;
		}
		if (a_ownerProfileId.empty()) {
			m_records.erase(a_buildZoneId);
			return ApplyStatus::Committed;
		}
		if (!ValidProfileId(a_ownerProfileId)) {
			return ApplyStatus::InvalidRequest;
		}
		const auto found = m_records.find(a_buildZoneId);
		if (found != m_records.end()) {
			if (a_ownerProfileId != found->second.ownerProfileId) {
				found->second = Record{ std::string{ a_ownerProfileId }, {} };
			}
			return ApplyStatus::Committed;
		}
		if (m_records.size() >= MaximumSettlements) {
			return ApplyStatus::SettlementLimit;
		}
		m_records.emplace(a_buildZoneId, Record{ std::string{ a_ownerProfileId }, {} });
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::Grant(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId)
	{
		if (!ValidProfileId(a_targetProfileId)) {
			return ApplyStatus::InvalidRequest;
		}
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end() || a_actorProfileId != found->second.ownerProfileId) {
			return ApplyStatus::NotOwner;
		}
		auto& record = found->second;
		if (a_targetProfileId == record.ownerProfileId) {
			return ApplyStatus::InvalidRequest;
		}
		if (record.allowedProfileIds.contains(std::string{ a_targetProfileId })) {
			return ApplyStatus::AlreadyAllowed;
		}
		if (record.allowedProfileIds.size() >= MaximumAllowedProfiles) {
			return ApplyStatus::Full;
		}
		record.allowedProfileIds.insert(std::string{ a_targetProfileId });
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::Revoke(Workshop::BuildZoneId a_buildZoneId, std::string_view a_actorProfileId, std::string_view a_targetProfileId)
	{
		if (!ValidProfileId(a_targetProfileId)) {
			return ApplyStatus::InvalidRequest;
		}
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end() || a_actorProfileId != found->second.ownerProfileId) {
			return ApplyStatus::NotOwner;
		}
		if (found->second.allowedProfileIds.erase(std::string{ a_targetProfileId }) == 0) {
			return ApplyStatus::NotAllowed;
		}
		return ApplyStatus::Committed;
	}

	Settlement Table::View(Workshop::BuildZoneId a_buildZoneId, const Record& a_record)
	{
		return Settlement{
			a_buildZoneId,
			a_record.ownerProfileId,
			std::vector<std::string>(a_record.allowedProfileIds.begin(), a_record.allowedProfileIds.end())
		};
	}
}
