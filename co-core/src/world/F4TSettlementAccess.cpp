#include "F4TSettlementAccess.h"

#include "F4TIdentityProtocol.h"
#include "F4TWorkshopRaids.h"

#include <algorithm>
#include <cmath>
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

	float WorkbenchMaxHpFor(SettlementKind a_kind) noexcept
	{
		return a_kind == SettlementKind::Camp ? WorkshopRaids::CampWorkbenchMaxHp : WorkshopRaids::WorkbenchMaxHp;
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

	bool Table::CanPlace(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId, bool a_namedZone) const
	{
		if (m_records.contains(a_buildZoneId)) {
			return CanBuild(a_buildZoneId, a_profileId);
		}
		return UnownedZoneOpenToBuild(a_namedZone);
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
			if (!std::isfinite(settlement.workbenchHp) || settlement.workbenchHp < 0.0F) {
				return false;
			}
			if (!settlement.workbenchBroken && !(settlement.workbenchHp > 0.0F)) {
				return false;
			}
			Record record{
				settlement.ownerProfileId,
				{},
				settlement.kind,
				settlement.workbenchHp,
				settlement.workbenchBroken,
				settlement.workbenchBrokenUnix,
				settlement.protectedUntilUnix,
				settlement.upkeepPaidUntil
			};
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

	ApplyStatus Table::SetOwner(
		Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_ownerProfileId,
		std::optional<SettlementKind> a_kind)
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
			auto record = found->second;
			const auto kind = a_kind.value_or(record.kind);
			if (a_ownerProfileId != record.ownerProfileId) {
				record.ownerProfileId = std::string{ a_ownerProfileId };
				record.allowedProfileIds.clear();
				record.workbenchBroken = false;
				record.workbenchBrokenUnix = 0;
				record.workbenchHp = WorkbenchMaxHpFor(kind);
			}
			record.kind = kind;
			if (kind == SettlementKind::Camp && record.workbenchHp > WorkbenchMaxHpFor(SettlementKind::Camp)) {
				record.workbenchHp = WorkbenchMaxHpFor(SettlementKind::Camp);
			}
			found->second = std::move(record);
			return ApplyStatus::Committed;
		}
		if (m_records.size() >= MaximumSettlements) {
			return ApplyStatus::SettlementLimit;
		}
		const auto kind = a_kind.value_or(SettlementKind::Vanilla);
		Record record{ std::string{ a_ownerProfileId }, {}, kind };
		record.workbenchHp = WorkbenchMaxHpFor(kind);
		m_records.emplace(a_buildZoneId, std::move(record));
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::ApplyWorkbenchDamage(Workshop::BuildZoneId a_buildZoneId, float a_damage, std::uint64_t a_unixSeconds)
	{
		if (a_buildZoneId == 0 || !std::isfinite(a_damage) || !(a_damage > 0.0F)) {
			return ApplyStatus::InvalidRequest;
		}
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return ApplyStatus::UnknownBuildZone;
		}
		auto& record = found->second;
		if (record.workbenchBroken) {
			return ApplyStatus::Committed;
		}
		record.workbenchHp = (std::max)(0.0F, record.workbenchHp - a_damage);
		if (!(record.workbenchHp > 0.0F)) {
			record.workbenchHp = 0.0F;
			record.workbenchBroken = true;
			record.workbenchBrokenUnix = a_unixSeconds;
		}
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::MarkProtectedUntil(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_untilUnix)
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return ApplyStatus::UnknownBuildZone;
		}
		found->second.protectedUntilUnix = a_untilUnix;
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::SetUpkeepPaidUntil(Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_untilUnix)
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return ApplyStatus::UnknownBuildZone;
		}
		found->second.upkeepPaidUntil = a_untilUnix;
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::RepairUnclaimedWorkbench(Workshop::BuildZoneId a_buildZoneId)
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return ApplyStatus::UnknownBuildZone;
		}
		auto& record = found->second;
		if (!record.workbenchBroken) {
			return ApplyStatus::InvalidRequest;
		}
		record.workbenchBroken = false;
		record.workbenchBrokenUnix = 0;
		record.workbenchHp = WorkbenchMaxHpFor(record.kind) * WorkshopRaids::UnclaimedWorkbenchRepairFraction;
		if (!(record.workbenchHp > 0.0F)) {
			record.workbenchHp = 1.0F;
		}
		return ApplyStatus::Committed;
	}

	ApplyStatus Table::RepairWorkbench(Workshop::BuildZoneId a_buildZoneId, float a_amount)
	{
		const auto found = m_records.find(a_buildZoneId);
		if (found == m_records.end()) {
			return ApplyStatus::UnknownBuildZone;
		}
		auto& record = found->second;
		const float maxHp = WorkbenchMaxHpFor(record.kind);
		if (!(a_amount > 0.0F) || !(record.workbenchHp < maxHp || record.workbenchBroken)) {
			return ApplyStatus::NothingToRepair;
		}
		record.workbenchHp = (std::min)(maxHp, record.workbenchHp + a_amount);
		if (record.workbenchBroken && record.workbenchHp > 0.0F) {
			record.workbenchBroken = false;
			record.workbenchBrokenUnix = 0;
		}
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
			std::vector<std::string>(a_record.allowedProfileIds.begin(), a_record.allowedProfileIds.end()),
			a_record.kind,
			a_record.workbenchHp,
			a_record.workbenchBroken,
			a_record.workbenchBrokenUnix,
			a_record.protectedUntilUnix,
			a_record.upkeepPaidUntil
		};
	}
}
