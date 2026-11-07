#include "DurableStateFileStore.h"
#include "DurableStateSections.h"
#include "F4TCharacterSlots.h"
#include "F4TSnapshotDeltaWire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using namespace DurableStateSections;
		constexpr std::string_view kDeathBagSections[] = { "deathBags" };

		constexpr double kGainLogIntervalSeconds = 30.0;
		constexpr std::size_t kMaximumRecentInventoryDrops = 16;

		std::vector<std::string> ProfileIds(const std::unordered_map<std::string, nlohmann::json>& a_playerStates)
		{
			std::vector<std::string> ids;
			ids.reserve(a_playerStates.size());
			for (const auto& entry : a_playerStates) ids.push_back(entry.first);
			return ids;
		}

		std::map<std::string, std::uint32_t> InventoryCounts(const nlohmann::json& a_root, std::string_view a_profileId)
		{
			std::map<std::string, std::uint32_t> counts;
			if (const auto inventory = F4T::DurableState::GetInventory(a_root, a_profileId)) {
				for (const auto& entry : inventory->at("items").items()) {
					if (const auto count = entry.value().find("count"); count != entry.value().end() && count->is_number_unsigned()) counts[entry.key()] = count->get<std::uint32_t>();
				}
			}
			return counts;
		}

		std::vector<RecentInventoryDrop> UnclaimedDrops(const nlohmann::json& a_root, std::string_view a_profileId,
			const std::map<std::string, std::uint32_t>& a_before, double a_now)
		{
			const auto after = InventoryCounts(a_root, a_profileId);
			const auto claims = F4T::DurableState::GetSurvivalNeeds(a_root, a_profileId).claims;
			std::vector<RecentInventoryDrop> drops;
			for (const auto& [itemKey, count] : a_before) {
				const auto found = after.find(itemKey);
				const std::uint32_t remaining = found == after.end() ? 0u : found->second;
				const bool claimed = std::any_of(claims.begin(), claims.end(),
					[&](const F4T::SurvivalNeeds::Claim& a_claim) { return a_claim.itemKey == itemKey; });
				if (remaining < count && !claimed) {
					drops.push_back(RecentInventoryDrop{ itemKey, count - remaining, a_now });
				}
			}
			return drops;
		}

	}

	DurableStateFileStore::DurableStateFileStore(
		std::filesystem::path a_path,
		Clock a_clock,
		InstanceIdGenerator a_instanceIdGenerator,
		PostCommitHook a_postCommitHook) :
		_path(std::filesystem::absolute(std::move(a_path))),
		_backupPath(_path.string() + ".bak"),
		_clock(std::move(a_clock)),
		_instanceIdGenerator(std::move(a_instanceIdGenerator)),
		_postCommitHook(std::move(a_postCommitHook))
	{
		if (!_clock || !_instanceIdGenerator) {
			throw std::invalid_argument("Durable state store requires clock and server-instance-id generator callbacks.");
		}
		std::filesystem::create_directories(_path.parent_path());
		_pipeline = std::make_unique<DurableWritePipeline>(_path, _clock, _postCommitHook);
		LoadOrCreate();
		_pipeline->SetDocument(_root);
	}

	nlohmann::json DurableStateFileStore::InitializeInventory(std::string_view a_profileId, const nlohmann::json& a_catalog)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::InitializeInventory(a_root, a_profileId, a_catalog, _clock());
		}, true);
		return *F4T::DurableState::GetInventory(_root, a_profileId);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetInventory(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetInventory(_root, a_profileId);
	}

	std::optional<nlohmann::json> DurableStateFileStore::PendingStartingGrant(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::PendingStartingGrant(_root, a_profileId);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetStartingGrant(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetStartingGrant(_root, a_profileId);
	}

	bool DurableStateFileStore::AckStartingGrant(std::string_view a_profileId, std::string_view a_grantId)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::AckStartingGrant(a_root, a_profileId, a_grantId);
		}, true);
	}

	bool DurableStateFileStore::BeginStartingGrant(std::string_view a_profileId, std::string_view a_characterId,
		const F4T::SaveProfileLogic::AttestedSave& a_baseline)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::BeginStartingGrant(a_root, a_profileId, a_characterId, a_baseline);
		}, true, true);
	}

	bool DurableStateFileStore::ConsumeStartingGrantApproval(std::string_view a_profileId, const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::ConsumeStartingGrantApproval(a_root, a_profileId, a_save);
		}, true, true);
	}
	bool DurableStateFileStore::ResolveStartingGrant(std::string_view a_profileId, std::string_view a_characterId,
		const F4T::SaveProfileLogic::AttestedSave& a_save, std::string_view a_note, bool a_legacy)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::ResolveStartingGrant(a_root, a_profileId, a_characterId, a_save, a_note, a_legacy);
		}, true, true);
	}

	std::optional<F4T::Replication::SnapshotState> DurableStateFileStore::GetDropSnapshot() const
	{
		const std::scoped_lock lock(_gate);
		if (!_dropSnapshotCached) {
			_dropSnapshot = BuildDropSnapshotLocked();
			_dropSnapshotCached = true;
		}
		return _dropSnapshot;
	}

	bool DurableStateFileStore::InventoryAuthorityBlocked() const
	{
		const std::scoped_lock lock(_gate);
		return _root.at("inventoryAuthorityBlocked").get<bool>();
	}

	std::optional<F4T::Replication::SnapshotState> DurableStateFileStore::BuildDropSnapshotLocked() const
	{
		if (_root.at("inventoryAuthorityBlocked").get<bool>()) return std::nullopt;
		auto snapshot = F4T::Drops::BuildStableDropSnapshot(_root.at("drops").at("live"));
		const auto baseline = snapshot ? F4T::Replication::EncodeBaseline(*snapshot, 1, F4T::Replication::Channel::Reliable) : std::nullopt;
		if (!baseline || baseline->size() > F4T::Drops::kMaximumDropBaselineBytes) return std::nullopt;
		return snapshot;
	}

	F4T::Drops::TransactionResult DurableStateFileStore::DropInventory(const std::string& a_profileId,
		const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		if (auto screened = F4T::Drops::DropLedger::Precheck(_root, a_profileId, a_request, true)) return *screened;
		F4T::Drops::TransactionResult result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, kDropOnlySections, [&](nlohmann::json& a_root) {
			result = F4T::Drops::DropLedger::Drop(a_root, a_profileId, a_request, a_position);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (committed) _dropSnapshotCached = false;
		return result;
	}

	F4T::Drops::TransactionResult DurableStateFileStore::PickupInventory(const std::string& a_profileId,
		const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		if (auto screened = F4T::Drops::DropLedger::Precheck(_root, a_profileId, a_request, false)) return *screened;
		F4T::Drops::TransactionResult result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, kDropOnlySections, [&](nlohmann::json& a_root) {
			result = F4T::Drops::DropLedger::Pickup(a_root, a_profileId, a_request, a_position);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (committed) _dropSnapshotCached = false;
		return result;
	}

	F4T::InventorySnapshot::Outcome DurableStateFileStore::ReconcileInventory(const std::string& a_profileId,
		const nlohmann::json& a_request)
	{
		const std::scoped_lock lock(_gate);
		F4T::InventorySnapshot::Outcome outcome;
		std::vector<RecentInventoryDrop> drops;
		F4T::InventorySnapshot::GainBuckets pendingBuckets;
		bool charged = false;
		F4T::InventorySnapshot::GainCheck gain;
		if (_gainPolicy.mode != F4T::InventorySnapshot::GainMode::Off) {
			gain = [&](const nlohmann::json& a_before, const nlohmann::json& a_after, std::uint32_t a_revision) {
				const auto now = _clock();
				const auto found = _gainBuckets.find(a_profileId);
				const auto verdict = F4T::InventorySnapshot::EvaluateGain(a_before, a_after, a_revision == 0, _gainPolicy,
					found == _gainBuckets.end() ? F4T::InventorySnapshot::GainBuckets{} : found->second, now);
				if (verdict.limited) LogGainLimited(a_profileId, verdict.detail, now);
				if (verdict.limited && _gainPolicy.mode == F4T::InventorySnapshot::GainMode::Enforce) return true;
				pendingBuckets = verdict.next;
				charged = true;
				return false;
			};
		}
		const auto committed = MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, kDeathBagSections, [&](nlohmann::json& a_root) {
			const auto before = InventoryCounts(a_root, a_profileId);
			outcome = F4T::InventorySnapshot::Reconcile(a_root, a_profileId, a_request, gain);
			if (outcome.changed) drops = UnclaimedDrops(a_root, a_profileId, before, _clock());
			return outcome.changed;
		}, true);
		if (committed) {
			if (charged) _gainBuckets[a_profileId] = std::move(pendingBuckets);
			const auto now = _clock();
			const auto found = _recentInventoryDrops.find(a_profileId);
			if (found != _recentInventoryDrops.end()) {
				std::erase_if(found->second, [now](const RecentInventoryDrop& a_drop) { return now - a_drop.at > kRecentInventoryDropWindowSeconds; });
				if (found->second.empty()) _recentInventoryDrops.erase(found);
			}
			if (!drops.empty()) {
				auto& recent = _recentInventoryDrops[a_profileId];
				recent.insert(recent.end(), drops.begin(), drops.end());
				if (recent.size() > kMaximumRecentInventoryDrops) {
					recent.erase(recent.begin(), recent.begin() + static_cast<std::ptrdiff_t>(recent.size() - kMaximumRecentInventoryDrops));
				}
			}
		}
		return outcome;
	}

	void DurableStateFileStore::SetInventoryGainPolicy(F4T::InventorySnapshot::GainPolicy a_policy)
	{
		const std::scoped_lock lock(_gate);
		_gainPolicy = std::move(a_policy);
	}

	void DurableStateFileStore::LogGainLimited(const std::string& a_profileId, const std::string& a_detail, double a_now)
	{
		const auto found = _gainLoggedAt.find(a_profileId);
		if (found != _gainLoggedAt.end() && a_now - found->second < kGainLogIntervalSeconds) return;
		_gainLoggedAt[a_profileId] = a_now;
		std::cerr << "Inventory gain limited (" << (_gainPolicy.mode == F4T::InventorySnapshot::GainMode::Enforce ? "enforce" : "warn")
			<< "): profile=" << a_profileId << ' ' << a_detail << '\n';
	}

	F4T::Drops::TransactionResult DurableStateFileStore::TransferContainer(const std::string& a_profileId,
		const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		F4T::Drops::TransactionResult result;
		const auto committed = MutateProfilesAndPersistLocked({ a_profileId }, kContainerOnlySections, [&](nlohmann::json& a_root) {
			result = F4T::Drops::DropLedger::ContainerTransfer(a_root, a_profileId, a_request, a_position);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (!committed && result.outcome == F4T::Drops::Outcome::Committed) {
			result.outcome = F4T::Drops::Outcome::Rejected;
			result.packet["status"] = "rejected";
			result.packet["reason"] = "inventoryPersistenceUnavailable";
			result.packet.erase("revision");
			result.packet.erase("containerRevision");
		}
		return result;
	}

	F4T::Trade::CommitResult DurableStateFileStore::TransferTrade(const F4T::Trade::CommitRequest& a_request)
	{
		const std::scoped_lock lock(_gate);
		F4T::Trade::CommitResult result;
		const auto committed = MutateProfilesAndPersistLocked({ a_request.first.profileId, a_request.second.profileId }, {}, [&](nlohmann::json& a_root) {
			result = F4T::Trade::Commit(a_root, a_request);
			return result.outcome == F4T::Drops::Outcome::Committed;
		}, true);
		if (!committed && result.outcome == F4T::Drops::Outcome::Committed) {
			result.outcome = F4T::Drops::Outcome::Rejected;
			result.reason = "inventoryPersistenceUnavailable";
			result.firstRevision.reset();
			result.secondRevision.reset();
		}
		return result;
	}

	std::string DurableStateFileStore::ServerInstanceId() const
	{
		const std::scoped_lock lock(_gate);
		return _root.at("serverInstanceId").get<std::string>();
	}

	std::optional<std::string> DurableStateFileStore::RecoveryNotice() const
	{
		const std::scoped_lock lock(_gate);
		return _recoveryNotice;
	}

	std::optional<std::string> DurableStateFileStore::TakePersistenceWarning()
	{
		const std::scoped_lock lock(_gate);
		if (auto failure = _pipeline->TakeFailure()) {
			_persistenceWarning = std::move(failure);
		}
		auto warning = std::move(_persistenceWarning);
		_persistenceWarning.reset();
		return warning;
	}

	std::optional<F4T::Identity::PersistedResumeCredential> DurableStateFileStore::GetResumeCredential(
		std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetResumeCredential(_root, a_profileId);
	}

	void DurableStateFileStore::SaveResumeCredential(
		std::string_view a_profileId,
		const F4T::Identity::TokenHash& a_tokenHash,
		const std::optional<F4T::Identity::TokenHash>& a_previousTokenHash,
		bool a_confirmed,
		double a_expiresAt)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			F4T::DurableState::SaveResumeCredential(
				a_root, a_profileId, a_tokenHash, a_previousTokenHash, a_confirmed, a_expiresAt, _clock());
			return true;
		}, true);
	}

	bool DurableStateFileStore::DeleteResumeCredential(std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::DeleteResumeCredential(a_root, a_profileId, _clock());
		}, true);
	}

	std::optional<nlohmann::json> DurableStateFileStore::GetPlayerState(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetPlayerState(_root, a_profileId);
	}

	std::optional<std::string> DurableStateFileStore::GetProfileSecret(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetProfileSecret(_root, a_profileId);
	}

	bool DurableStateFileStore::EnsureProfileSecret(std::string_view a_profileId, std::string_view a_secret)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::EnsureProfileSecret(a_root, a_profileId, a_secret, _clock());
		}, true);
	}

	std::vector<F4T::SaveProfileLogic::AttestedSave> DurableStateFileStore::GetSaveAttestations(
		std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetSaveAttestations(_root, a_profileId);
	}

	bool DurableStateFileStore::RecordSaveAttestation(
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			F4T::DurableState::RecordSaveAttestation(a_root, a_profileId, a_save, _clock());
			return true;
		}, true);
	}

	bool DurableStateFileStore::UpdatePlayerState(std::string_view a_profileId, const nlohmann::json& a_playerState)
	{
		if (!F4T::DurableState::CarriesDurablePlayerState(a_playerState)) {
			return false;
		}

		const std::scoped_lock lock(_gate);
		if (F4T::DurableState::InspectPlayerStateUpdate(_root, a_profileId, a_playerState) !=
			F4T::DurableState::PlayerStateMutationOutcome::Changed) {
			return false;
		}
		return MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::UpdatePlayerState(a_root, a_profileId, a_playerState, _clock()) ==
				F4T::DurableState::PlayerStateMutationOutcome::Changed;
		}, true);
	}

	bool DurableStateFileStore::UpdatePlayerStates(const std::unordered_map<std::string, nlohmann::json>& a_playerStates)
	{
		const std::scoped_lock lock(_gate);
		bool queued = !a_playerStates.empty() && MutateProfilesAndPersistLocked(ProfileIds(a_playerStates), {}, [&](nlohmann::json& a_root) {
			bool changed = false;
			for (const auto& [profileId, playerState] : a_playerStates) {
				if (F4T::DurableState::InspectPlayerStateUpdate(a_root, profileId, playerState) !=
					F4T::DurableState::PlayerStateMutationOutcome::Changed) {
					continue;
				}
				changed = F4T::DurableState::UpdatePlayerState(a_root, profileId, playerState, _clock()) ==
					F4T::DurableState::PlayerStateMutationOutcome::Changed || changed;
			}
			return changed;
		}, false);
		if (!queued && _pipeline->LastWriteFailed()) {
			const auto started = std::chrono::steady_clock::now();
			PostSectionsLocked(kProfileSections, false);
			const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
			if (elapsed > kSlowMainThreadSaveSeconds) {
				LogMainThreadSave(elapsed, std::size(kProfileSections), false);
			}
			queued = true;
		}
		return queued;
	}

	nlohmann::json DurableStateFileStore::GetServerWorldState() const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetWorldState(_root);
	}

	void DurableStateFileStore::UpdateServerWorldState(const nlohmann::json& a_worldState)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked(kWorldStateSections, [&](nlohmann::json& a_root) {
			F4T::DurableState::UpdateWorldState(a_root, a_worldState);
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetFactions() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("factions", nlohmann::json::object());
	}

	void DurableStateFileStore::SaveFactions(const nlohmann::json& a_factions)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked(kFactionSections, [&](nlohmann::json& a_root) {
			a_root["factions"] = a_factions;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetQuests() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("quests", nlohmann::json::object());
	}

	bool DurableStateFileStore::SaveQuests(const nlohmann::json& a_quests, bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kQuestSections, [&](nlohmann::json& a_root) {
			a_root["quests"] = a_quests;
			return true;
		}, a_awaitCommit);
	}

	void DurableStateFileStore::SaveQuestsBackup(const nlohmann::json& a_quests)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked(kQuestBackupSections, [&](nlohmann::json& a_root) {
			a_root["quests_backup"] = a_quests;
			return true;
		}, true);
	}

	nlohmann::json DurableStateFileStore::GetParkedSuits() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("parkedSuits", nlohmann::json::array());
	}

	bool DurableStateFileStore::SaveParkedSuits(const nlohmann::json& a_parkedSuits, bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kParkedSuitSections, [&](nlohmann::json& a_root) {
			a_root["parkedSuits"] = a_parkedSuits;
			return true;
		}, a_awaitCommit);
	}

	nlohmann::json DurableStateFileStore::GetSteamIdentities() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("steamIdentities", nlohmann::json(nullptr));
	}

	void DurableStateFileStore::SaveSteamIdentities(const nlohmann::json& a_steamIdentities)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked(kSteamIdentitySections, [&](nlohmann::json& a_root) {
			if (a_root.value("steamIdentities", nlohmann::json(nullptr)) == a_steamIdentities) return false;
			a_root["steamIdentities"] = a_steamIdentities;
			return true;
		}, false);
	}

}
