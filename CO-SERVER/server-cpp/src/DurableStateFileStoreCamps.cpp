#include "DurableStateFileStore.h"
#include "DurableStateSections.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using namespace DurableStateSections;

		constexpr std::string_view kSettlementChargeSections[] = { "settlementCharges", "settlementChargeNextOperationId" };
		constexpr std::string_view kCampStoreSections[] = { kDropsSection, kWorkshopSection };

		bool EraseSettlementChargeFromRoot(nlohmann::json& a_root, std::uint64_t a_operationId)
		{
			const auto charges = a_root.find("settlementCharges");
			if (charges == a_root.end() || !charges->is_array()) return false;
			for (auto it = charges->begin(); it != charges->end(); ++it) {
				if (it->value("operationId", std::uint64_t{}) == a_operationId) {
					charges->erase(it);
					return true;
				}
			}
			return false;
		}
	}

	F4T::CampGrants::Commit DurableStateFileStore::CommitCamp(F4T::Drops::EntityId a_entityId, const nlohmann::json& a_expectedDrop,
		const nlohmann::json& a_workshop, const nlohmann::json& a_settlementAccess, const std::string& a_profileId,
		std::uint64_t a_operationId, const std::vector<F4T::Trade::Line>& a_refund)
	{
		const std::scoped_lock lock(_gate);
		F4T::CampGrants::Commit result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, kCampSections, [&](nlohmann::json& a_root) {
			auto& live = a_root.at("drops").at("live");
			const auto found = live.find(std::to_string(a_entityId));
			if (found == live.end() || *found != a_expectedDrop || a_root.at("inventoryAuthorityBlocked").get<bool>()) return false;
			result = F4T::CampGrants::CommitMove(a_root, a_profileId, a_operationId, a_refund);
			if (result.outcome != F4T::CampGrants::Outcome::Committed) return false;
			live.erase(std::to_string(a_entityId));
			a_root["workshop"] = a_workshop;
			a_root["settlementAccess"] = a_settlementAccess;
			return true;
		}, true, true);
		if (!committed && result.outcome == F4T::CampGrants::Outcome::Committed) {
			result = F4T::CampGrants::Commit{ F4T::CampGrants::Outcome::Rejected, "inventoryPersistenceUnavailable", {}, false };
		}
		if (committed) _dropSnapshotCached = false;
		return result;
	}

	F4T::CampGrants::Commit DurableStateFileStore::CommitCampMove(const nlohmann::json& a_workshop, const std::string& a_profileId,
		std::uint64_t a_operationId, const std::vector<F4T::Trade::Line>& a_lines)
	{
		const std::scoped_lock lock(_gate);
		F4T::CampGrants::Commit result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, kWorkshopSections, [&](nlohmann::json& a_root) {
			result = F4T::CampGrants::CommitMove(a_root, a_profileId, a_operationId, a_lines);
			if (result.outcome != F4T::CampGrants::Outcome::Committed) return false;
			a_root["workshop"] = a_workshop;
			return true;
		}, true, true);
		if (!committed && result.outcome == F4T::CampGrants::Outcome::Committed) {
			result = F4T::CampGrants::Commit{ F4T::CampGrants::Outcome::Rejected, "inventoryPersistenceUnavailable", {}, false };
		}
		return result;
	}

	F4T::CampGrants::Commit DurableStateFileStore::CommitChargeRefund(const std::string& a_profileId, std::uint64_t a_operationId,
		const std::vector<F4T::Trade::Line>& a_lines)
	{
		const std::scoped_lock lock(_gate);
		F4T::CampGrants::Commit result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, {}, [&](nlohmann::json& a_root) {
			result = F4T::CampGrants::CommitMove(a_root, a_profileId, a_operationId, a_lines);
			return result.outcome == F4T::CampGrants::Outcome::Committed;
		}, true, true);
		if (!committed && result.outcome == F4T::CampGrants::Outcome::Committed) {
			result = F4T::CampGrants::Commit{ F4T::CampGrants::Outcome::Rejected, "inventoryPersistenceUnavailable", {}, false };
		}
		return result;
	}

	std::optional<F4T::CampGrants::Commit> DurableStateFileStore::FindCampMove(const std::string& a_profileId, std::uint64_t a_operationId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::CampGrants::Replay(_root, a_profileId, a_operationId);
	}

	bool DurableStateFileStore::CampKitDropLimited(const std::string& a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		const auto drops = _root.find("drops");
		if (drops == _root.end() || !drops->is_object()) return false;
		const auto live = drops->find("live");
		if (live == drops->end() || !live->is_object()) return false;
		std::size_t owned = 0;
		for (const auto& drop : *live) {
			if (drop.is_object() && drop.value("ownerProfileId", std::string{}) == a_profileId) ++owned;
		}
		return owned >= F4T::Drops::kMaximumLiveDropsPerPlayer || live->size() >= F4T::Drops::kMaximumLiveDrops;
	}

	std::vector<F4T::CampGrants::Grant> DurableStateFileStore::PendingCampGrants(const std::string& a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::CampGrants::Pending(_root, a_profileId);
	}

	bool DurableStateFileStore::AckCampGrant(const std::string& a_profileId, std::uint64_t a_operationId)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ a_profileId }, {}, [&](nlohmann::json& a_root) {
			return F4T::CampGrants::Ack(a_root, a_profileId, a_operationId);
		}, true);
	}

	bool DurableStateFileStore::PostSettlementCharge(const nlohmann::json& a_record)
	{
		const std::scoped_lock lock(_gate);
		try {
			return MutateAndPersistLocked(kSettlementChargeSections, [&](nlohmann::json& a_root) {
				auto& charges = a_root["settlementCharges"];
				if (!charges.is_array()) charges = nlohmann::json::array();
				const auto operationId = a_record.value("operationId", std::uint64_t{});
				auto& counter = a_root["settlementChargeNextOperationId"];
				if (!counter.is_number_unsigned() || counter.get<std::uint64_t>() <= operationId) {
					counter = operationId + 1;
				}
				for (auto& charge : charges) {
					if (charge.value("operationId", std::uint64_t{}) == operationId) {
						charge = a_record;
						return true;
					}
				}
				charges.push_back(a_record);
				return true;
			}, true, true);
		} catch (const std::exception&) {
			return false;
		}
	}

	bool DurableStateFileStore::EraseSettlementCharge(std::uint64_t a_operationId)
	{
		const std::scoped_lock lock(_gate);
		bool found = false;
		try {
			const auto persisted = MutateAndPersistLocked(kSettlementChargeSections, [&](nlohmann::json& a_root) {
				found = EraseSettlementChargeFromRoot(a_root, a_operationId);
				return found;
			}, true, true);
			return persisted || !found;
		} catch (const std::exception&) {
			return false;
		}
	}

	nlohmann::json DurableStateFileStore::PendingSettlementCharges() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("settlementCharges", nlohmann::json::array());
	}

	std::uint64_t DurableStateFileStore::SettlementChargeNextOperationId() const
	{
		const std::scoped_lock lock(_gate);
		const auto found = _root.find("settlementChargeNextOperationId");
		return found != _root.end() && found->is_number_unsigned() ? found->get<std::uint64_t>() : std::uint64_t{};
	}

	std::uint64_t DurableStateFileStore::SnapshotRemovals(const std::string& a_profileId, const std::string& a_itemKey) const
	{
		const std::scoped_lock lock(_gate);
		const auto removals = _root.find("snapshotRemovals");
		if (removals == _root.end() || !removals->contains(a_profileId)) return std::uint64_t{};
		const auto& profile = removals->at(a_profileId);
		const auto found = profile.find(a_itemKey);
		return found != profile.end() && found->is_number_unsigned() ? found->get<std::uint64_t>() : std::uint64_t{};
	}

	nlohmann::json DurableStateFileStore::GetVendorLedger() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value(std::string{ kVendorLedgerSection }, nlohmann::json(nullptr));
	}

	bool DurableStateFileStore::SetVendorLedger(const nlohmann::json& a_ledger)
	{
		const std::scoped_lock lock(_gate);
		try {
			return MutateAndPersistLocked(kVendorLedgerSections, [&](nlohmann::json& a_root) {
				a_root[std::string{ kVendorLedgerSection }] = a_ledger;
				return true;
			}, false);
		} catch (const std::exception&) {
			return false;
		}
	}

	nlohmann::json DurableStateFileStore::GetLiveDrop(F4T::Drops::EntityId a_entityId) const
	{
		const std::scoped_lock lock(_gate);
		const auto& live = _root.at("drops").at("live");
		const auto found = live.find(std::to_string(a_entityId));
		return found == live.end() ? nlohmann::json() : *found;
	}

	bool DurableStateFileStore::ConsumeLiveDrop(F4T::Drops::EntityId a_entityId)
	{
		const std::scoped_lock lock(_gate);
		const auto committed = MutateProfilesAndPersistLocked({}, kDropOnlySections, [&](nlohmann::json& a_root) {
			auto& live = a_root.at("drops").at("live");
			if (!live.contains(std::to_string(a_entityId))) return false;
			live.erase(std::to_string(a_entityId));
			return true;
		}, true);
		if (committed) _dropSnapshotCached = false;
		return committed;
	}

	bool DurableStateFileStore::CommitCampStore(const nlohmann::json& a_workshop, const std::string& a_profileId,
		std::string_view a_itemKey, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		const auto committed = MutateProfilesAndPersistLocked({}, kCampStoreSections, [&](nlohmann::json& a_root) {
			if (a_root.at("inventoryAuthorityBlocked").get<bool>() ||
				!F4T::Drops::DropLedger::PlaceOwnedDrop(a_root, a_profileId, std::string{ a_itemKey }, a_position)) return false;
			a_root["workshop"] = a_workshop;
			return true;
		}, true, true);
		if (committed) _dropSnapshotCached = false;
		return committed;
	}

	nlohmann::json DurableStateFileStore::GetSettlementAccess() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("settlementAccess", nlohmann::json::array());
	}

	bool DurableStateFileStore::SaveSettlementAccess(const nlohmann::json& a_settlementAccess, std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kSettlementAccessSections, [&](nlohmann::json& a_root) {
			if (a_eraseChargeOperationId && !EraseSettlementChargeFromRoot(a_root, *a_eraseChargeOperationId)) return false;
			a_root["settlementAccess"] = a_settlementAccess;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetWorkshop() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("workshop", nlohmann::json::object());
	}

	bool DurableStateFileStore::SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit, std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kWorkshopSections, [&](nlohmann::json& a_root) {
			if (a_eraseChargeOperationId && !EraseSettlementChargeFromRoot(a_root, *a_eraseChargeOperationId)) return false;
			a_root["workshop"] = a_workshop;
			return true;
		}, a_awaitCommit);
	}
}
