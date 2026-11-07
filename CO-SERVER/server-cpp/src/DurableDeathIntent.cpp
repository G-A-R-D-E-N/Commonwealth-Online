#include "DurableStateFileStore.h"

#include <iostream>
#include <mutex>
#include <string_view>

namespace CommonwealthOnline::Server
{
	namespace
	{
		void AddProfile(std::vector<std::string>& a_profiles, const nlohmann::json& a_bag)
		{
			if (a_bag.is_object() && a_bag.contains("ownerProfileId") && a_bag.at("ownerProfileId").is_string()) {
				a_profiles.push_back(a_bag.at("ownerProfileId").get<std::string>());
			}
		}

		std::vector<std::string> BagOwners(const nlohmann::json& a_root, const std::string& a_deathId)
		{
			std::vector<std::string> owners;
			const auto bags = a_root.find("deathBags");
			if (bags != a_root.end() && bags->is_object() && bags->contains(a_deathId)) AddProfile(owners, bags->at(a_deathId));
			const auto archive = a_root.find("deathBagArchive");
			if (archive != a_root.end() && archive->is_object() && archive->contains(a_deathId) && archive->at(a_deathId).is_object() &&
				archive->at(a_deathId).contains("bag")) AddProfile(owners, archive->at(a_deathId).at("bag"));
			return owners;
		}

		std::vector<std::string> DepositedBagOwners(const nlohmann::json& a_root)
		{
			std::vector<std::string> owners;
			const auto bags = a_root.find("deathBags");
			if (bags == a_root.end() || !bags->is_object()) return owners;
			for (const auto& [id, bag] : bags->items()) {
				(void)id;
				if (bag.is_object() && bag.contains("deposited") && bag.at("deposited") == true && bag.contains("placed") &&
					bag.at("placed") == false && !bag.contains("placeRequestedAt")) AddProfile(owners, bag);
			}
			return owners;
		}

		std::vector<std::string> IntentOwner(const nlohmann::json& a_root, const std::string& a_deathId)
		{
			std::vector<std::string> owners;
			const auto intents = a_root.find("deathIntents");
			if (intents == a_root.end() || !intents->is_object() || !intents->contains(a_deathId)) return owners;
			const auto& intent = intents->at(a_deathId);
			if (intent.is_object() && intent.contains("profileId") && intent.at("profileId").is_string()) {
				owners.push_back(intent.at("profileId").get<std::string>());
			}
			return owners;
		}
	}

	bool DurableStateFileStore::CreateEmptyDeathBag(const std::string& a_deathId)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::CreateEmpty(a_root, a_deathId, _clock());
		}, true, true);
	}

	bool DurableStateFileStore::CreateAndDepositDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character, const nlohmann::json& a_stacks, const nlohmann::json& a_retained)
	{
		const std::scoped_lock lock(_gate);
		if (F4T::DeathBag::OwnedBy(_root, a_deathId, a_profile, a_character)) {
			const auto bags = _root.find("deathBags");
			if (bags == _root.end() || !bags->is_object()) return true;
			const auto bag = bags->find(a_deathId);
			return bag == bags->end() || !bag->is_object() || bag->value("stacks", nlohmann::json::array()) == a_stacks;
		}
		const auto owners = IntentOwner(_root, a_deathId);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		return MutateProfilesAndPersistLocked(owners, sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::CreateEmpty(a_root, a_deathId, _clock()) && F4T::DeathBag::Deposit(a_root, a_deathId, a_stacks, a_retained) && F4T::DeathBag::RequestPlacement(a_root, a_deathId, _clock());
		}, true, true);
	}

	bool DurableStateFileStore::WithdrawDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character,
		bool a_allowOthers, const std::string& a_itemKey, std::uint32_t a_count, std::uint32_t a_revision, const std::string& a_operationId,
		nlohmann::json* a_metadataOut, bool* a_freshOut, bool a_receiptProof)
	{
		const std::scoped_lock lock(_gate);
		if (a_freshOut) *a_freshOut = false;
		const auto operation = "bag:" + a_deathId + ":" + a_operationId;
		const bool existed = F4T::DeathBag::FindGrant(_root, a_profile, operation) != nullptr;
		constexpr std::string_view sections[]{ "deathBags", "deathGenerations" };
		nlohmann::json metadata;
		const bool committed = MutateProfilesAndPersistLocked({ std::string{ a_profile } }, sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::Withdraw(a_root, a_deathId, a_profile, a_character, a_allowOthers, a_itemKey, a_count, a_revision, a_operationId, _clock(), a_metadataOut ? &metadata : nullptr, a_receiptProof);
		}, true, true);
		if (committed && a_metadataOut) *a_metadataOut = std::move(metadata);
		if (committed && a_freshOut) *a_freshOut = !existed;
		return committed;
	}

	bool DurableStateFileStore::PlaceDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::Place(a_root, a_deathId, a_profile, a_character);
		}, true, true);
	}

	bool DurableStateFileStore::VoidDeathBag(const std::string& a_deathId, nlohmann::json* a_stacksOut)
	{
		const std::scoped_lock lock(_gate);
		const auto owners = BagOwners(_root, a_deathId);
		constexpr std::string_view sections[]{ "deathBags" };
		nlohmann::json stacks;
		const bool committed = MutateProfilesAndPersistLocked(owners, sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::Void(a_root, a_deathId, a_stacksOut ? &stacks : nullptr);
		}, true, true);
		if (committed && a_stacksOut) *a_stacksOut = std::move(stacks);
		return committed;
	}

	F4T::DeathBag::PlaceFailedOutcome DurableStateFileStore::ApplyDeathBagPlaceFailed(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character,
		bool a_restored, const nlohmann::json& a_remaining, nlohmann::json* a_stacksOut)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		auto outcome = F4T::DeathBag::PlaceFailedOutcome::Rejected;
		nlohmann::json stacks;
		const bool committed = MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			outcome = F4T::DeathBag::ApplyPlaceFailed(a_root, a_deathId, a_profile, a_character, a_restored, a_remaining, a_stacksOut ? &stacks : nullptr);
			return outcome != F4T::DeathBag::PlaceFailedOutcome::Rejected && outcome != F4T::DeathBag::PlaceFailedOutcome::Unchanged;
		}, true, true);
		if (committed && a_stacksOut) *a_stacksOut = std::move(stacks);
		if (committed && outcome == F4T::DeathBag::PlaceFailedOutcome::Parked) {
			std::cerr << "Death bag " << a_deathId << " could not be archived and is parked so it is not voided.\n";
		}
		if (!committed && outcome != F4T::DeathBag::PlaceFailedOutcome::Unchanged) return F4T::DeathBag::PlaceFailedOutcome::Rejected;
		return outcome;
	}

	bool DurableStateFileStore::RecoverArchivedDeathBag(const std::string& a_deathId)
	{
		const std::scoped_lock lock(_gate);
		const auto owners = BagOwners(_root, a_deathId);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		return MutateProfilesAndPersistLocked(owners, sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::RecoverArchived(a_root, a_deathId);
		}, true, true);
	}

	bool DurableStateFileStore::ResolveUnverifiedDeathBag(const std::string& a_deathId, F4T::DeathBag::Resolution a_resolution)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::ResolveUnverified(a_root, a_deathId, a_resolution);
		}, true, true);
	}

	nlohmann::json DurableStateFileStore::ListArchivedDeathBags(std::size_t a_offset) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DeathBag::ArchiveSummary(_root, a_offset);
	}

	bool DurableStateFileStore::QueueDeathBagRefundGrant(const std::string& a_profile, const std::string& a_character,
		const std::string& a_deathId, const std::string& a_operationId, const std::string& a_itemKey, std::uint32_t a_count,
		const nlohmann::json& a_metadata)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profile } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::QueueRefundGrant(a_root, a_profile, a_character, a_deathId, a_operationId, a_itemKey, a_count, a_metadata);
		}, true, true);
	}

	F4T::DeathBag::UnplacedOutcome DurableStateFileStore::ReportDeathBagUnplaced(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character, const std::string& a_operationId)
	{
		const std::scoped_lock lock(_gate);
		auto outcome = F4T::DeathBag::UnplacedOutcome::Refused;
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive" };
		const bool committed = MutateProfilesAndPersistLocked({ std::string{ a_profile } }, sections, [&](nlohmann::json& a_root) {
			outcome = F4T::DeathBag::ReportUnplaced(a_root, a_deathId, a_profile, a_character, a_operationId);
			return outcome == F4T::DeathBag::UnplacedOutcome::Refunded;
		}, true, true);
		if (outcome == F4T::DeathBag::UnplacedOutcome::Refunded && !committed) return F4T::DeathBag::UnplacedOutcome::Refused;
		return outcome;
	}

	std::vector<F4T::DeathBag::Expired> DurableStateFileStore::ExpirePendingDeathBags()
	{
		const std::scoped_lock lock(_gate);
		const auto now = _clock();
		if (!F4T::DeathBag::HasExpiryWork(_root, now)) return {};
		const auto owners = DepositedBagOwners(_root);
		constexpr std::string_view sections[]{ "deathBags", "deathBagArchive", "deathGenerations" };
		std::vector<F4T::DeathBag::Expired> expired;
		const auto committed = MutateProfilesAndPersistLocked(owners, sections, [&](nlohmann::json& a_root) {
			expired = F4T::DeathBag::ExpirePending(a_root, now);
			const auto held = F4T::DeathBag::ArchiveOverdueClaims(a_root, now);
			const auto swept = F4T::DeathBag::SweepEmptied(a_root);
			return !expired.empty() || held != 0 || swept != 0;
		}, true, true);
		return committed ? expired : std::vector<F4T::DeathBag::Expired>{};
	}

	bool DurableStateFileStore::MarkDeathBagDeparted(const std::string& a_character, bool a_cleanGoodbye)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::MarkDeparted(a_root, a_character, a_cleanGoodbye, _clock());
		}, true, true);
	}

	bool DurableStateFileStore::ClearDeathBagDeparted(const std::string& a_character)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathBags" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::ClearDeparted(a_root, a_character);
		}, true, true);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetDeathBagRecipientGrants(const std::string& a_profile, const std::string& a_character,
		const std::optional<std::string>& a_cursor, std::optional<std::string>* a_nextCursor)
	{
		const std::scoped_lock lock(_gate);
		return F4T::DeathBag::ReconcileGrants(_root, a_profile, a_character, a_cursor, a_nextCursor);
	}

	bool DurableStateFileStore::MarkDeathBagGrantAttempted(const std::string& a_profile, const std::string& a_character, const std::string& a_deathId, const std::string& a_operationId)
	{
		const std::scoped_lock lock(_gate);
		const auto operation = "bag:" + a_deathId + ":" + a_operationId;
		const auto* past = F4T::DeathBag::GrantHistory(_root, a_profile);
		if (past && past->contains(operation) && past->at(operation).value("characterId", std::string{}) == a_character) return true;
		constexpr std::string_view sections[]{ "deathBagGrantHistory" };
		return MutateProfilesAndPersistLocked({ std::string{ a_profile } }, sections, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::MarkGrantAttempted(a_root, a_profile, a_character, a_deathId, a_operationId, _clock());
		}, true, true);
	}

	bool DurableStateFileStore::ClaimDeathBagGrant(const std::string& a_profile, const std::string& a_character, const std::string& a_deathId, const std::string& a_operationId)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profile } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DeathBag::ClaimQueuedGrant(a_root, a_profile, a_character, a_deathId, a_operationId);
		}, true, true);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetDeathBag(const std::string& a_deathId) const
	{
		const std::scoped_lock lock(_gate);
		const auto bags = _root.find("deathBags");
		if (bags == _root.end() || !bags->is_object()) return std::nullopt;
		const auto bag = bags->find(a_deathId);
		return bag == bags->end() ? std::nullopt : std::optional<nlohmann::json>{ *bag };
	}

	std::optional<nlohmann::json> DurableStateFileStore::DiscoverDeathBags(const std::string& a_profile, const std::string& a_character, bool a_allowOthers) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DeathBag::Discovery(_root, a_profile, a_character, a_allowOthers);
	}

	std::size_t DurableStateFileStore::ExpireDueDeathIntents(const std::set<std::string>& a_onlineCharacters)
	{
		const std::scoped_lock lock(_gate);
		const auto now = _clock();
		if (!F4T::DeathIntent::HasDueExpiry(_root, now, a_onlineCharacters)) return 0;
		constexpr std::string_view sections[]{ "deathIntents" };
		std::size_t changed = 0;
		const auto committed = MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			changed = F4T::DeathIntent::ExpireDue(a_root, now, a_onlineCharacters);
			return changed != 0;
		}, true, true);
		return committed ? changed : 0;
	}

	double DurableStateFileStore::CurrentUnixTime() const
	{
		const std::scoped_lock lock(_gate);
		return _clock();
	}

	bool DurableStateFileStore::MarkDeathIntentDead(const std::string& a_characterId, const std::string& a_deathId)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathIntents" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathIntent::MarkDead(a_root, a_characterId, a_deathId);
		}, true, true);
	}
	bool DurableStateFileStore::RetireUntransferredDeathIntent(const std::string& a_profileId, const std::string& a_deathId)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathIntents", "deathGenerations" };
		return MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			return F4T::DeathIntent::RetireUntransferred(a_root, a_profileId, a_deathId);
		}, true, true);
	}

	std::optional<std::string> DurableStateFileStore::GetActiveDeathIntentId(const std::string& a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		const auto generations = _root.find("deathGenerations");
		if (generations == _root.end() || !generations->is_object()) return std::nullopt;
		const auto generation = generations->find(a_profileId);
		if (generation == generations->end() || !generation->is_object() || !generation->contains("activeId") ||
			!generation->at("activeId").is_string()) return std::nullopt;
		return generation->at("activeId").get<std::string>();
	}

	F4T::DeathIntent::Allocation DurableStateFileStore::AllocateDeathIntent(const nlohmann::json& a_record)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathIntents", "deathGenerations" };
		F4T::DeathIntent::Allocation result;
		const auto committed = MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			result = F4T::DeathIntent::Allocate(a_root, a_record);
			return result.outcome == F4T::DeathIntent::Outcome::Created;
		}, true, true);
		if (result.outcome == F4T::DeathIntent::Outcome::Created && !committed) return {};
		return result;
	}

	F4T::DeathIntent::Outcome DurableStateFileStore::RecordDeathIntent(const std::string& a_deathId, const nlohmann::json& a_record)
	{
		const std::scoped_lock lock(_gate);
		constexpr std::string_view sections[]{ "deathIntents" };
		auto outcome = F4T::DeathIntent::Outcome::Rejected;
		const auto committed = MutateAndPersistLocked(sections, [&](nlohmann::json& a_root) {
			outcome = F4T::DeathIntent::Record(a_root, a_deathId, a_record);
			return outcome == F4T::DeathIntent::Outcome::Created;
		}, true, true);
		if (outcome == F4T::DeathIntent::Outcome::Created && !committed) return F4T::DeathIntent::Outcome::Rejected;
		return outcome;
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetDeathIntent(const std::string& a_deathId) const
	{
		const std::scoped_lock lock(_gate);
		const auto records = _root.find("deathIntents");
		if (records == _root.end() || !records->is_object()) return std::nullopt;
		const auto record = records->find(a_deathId);
		return record == records->end() ? std::nullopt : std::optional<nlohmann::json>{ *record };
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetActiveDeathIntent(const std::string& a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		const auto generations = _root.find("deathGenerations");
		if (generations == _root.end() || !generations->is_object()) return std::nullopt;
		const auto generation = generations->find(a_profileId);
		if (generation == generations->end() || !generation->is_object() || !generation->contains("activeId") ||
			!generation->at("activeId").is_string()) return std::nullopt;
		const auto records = _root.find("deathIntents");
		if (records == _root.end() || !records->is_object()) return std::nullopt;
		const auto record = records->find(generation->at("activeId").get_ref<const std::string&>());
		return record == records->end() ? std::nullopt : std::optional<nlohmann::json>{ *record };
	}
}
