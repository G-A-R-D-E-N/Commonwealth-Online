#pragma once

#include "DurableWritePipeline.h"
#include "F4TCampGrants.h"
#include "F4TDurableState.h"
#include "F4TDropLedger.h"
#include "F4TInventorySnapshot.h"
#include "F4TTradeLedger.h"
#include "F4TCharacterSlots.h"
#include "F4TDeathIntent.h"
#include "F4TDeathBag.h"
#include "F4TDeathBagUnplaced.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct RecentInventoryDrop
	{
		std::string itemKey;
		std::uint32_t count{};
		double at{};
	};

	struct FirstJoinAbandonMark
	{
		std::uint32_t slot{};
		double at{};
	};

	class DurableStateFileStore final : public F4T::Identity::ResumeCredentialStore
	{
	public:
		using Clock = std::function<double()>;
		using InstanceIdGenerator = std::function<std::string()>;
		using PostCommitHook = std::function<void()>;

		DurableStateFileStore(
			std::filesystem::path a_path,
			Clock a_clock,
			InstanceIdGenerator a_instanceIdGenerator,
			PostCommitHook a_postCommitHook = {});

		[[nodiscard]] nlohmann::json InitializeInventory(std::string_view a_profileId, const nlohmann::json& a_catalog);
		[[nodiscard]] std::optional<nlohmann::json> GetInventory(std::string_view a_profileId) const;
		[[nodiscard]] std::optional<nlohmann::json> PendingStartingGrant(std::string_view a_profileId) const;
		[[nodiscard]] std::optional<nlohmann::json> GetStartingGrant(std::string_view a_profileId) const;
		[[nodiscard]] bool AckStartingGrant(std::string_view a_profileId, std::string_view a_grantId);
		[[nodiscard]] bool BeginStartingGrant(std::string_view a_profileId, std::string_view a_characterId,
			const F4T::SaveProfileLogic::AttestedSave& a_baseline);
		[[nodiscard]] bool ConsumeStartingGrantApproval(std::string_view a_profileId, const F4T::SaveProfileLogic::AttestedSave& a_save);
		[[nodiscard]] bool ResolveStartingGrant(std::string_view a_profileId, std::string_view a_characterId,
			const F4T::SaveProfileLogic::AttestedSave& a_save, std::string_view a_note, bool a_legacy);
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> GetDropSnapshot() const;
		[[nodiscard]] bool InventoryAuthorityBlocked() const;
		[[nodiscard]] F4T::DeathIntent::Outcome RecordDeathIntent(const std::string& a_deathId, const nlohmann::json& a_record);
		[[nodiscard]] F4T::DeathIntent::Allocation AllocateDeathIntent(const nlohmann::json& a_record);
		[[nodiscard]] std::optional<nlohmann::json> GetDeathIntent(const std::string& a_deathId) const;
		[[nodiscard]] std::optional<nlohmann::json> GetActiveDeathIntent(const std::string& a_profileId) const;
		[[nodiscard]] std::optional<std::string> GetActiveDeathIntentId(const std::string& a_profileId) const;
		[[nodiscard]] bool RetireUntransferredDeathIntent(const std::string& a_profileId, const std::string& a_deathId);
		std::size_t ReclaimDeathHistory();
		[[nodiscard]] bool CreateEmptyDeathBag(const std::string& a_deathId);
		[[nodiscard]] bool CreateAndDepositDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character, const nlohmann::json& a_stacks, const nlohmann::json& a_retained = nlohmann::json::array());
		[[nodiscard]] bool WithdrawDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character,
			bool a_allowOthers, const std::string& a_itemKey, std::uint32_t a_count, std::uint32_t a_revision, const std::string& a_operationId,
			nlohmann::json* a_metadataOut = nullptr, bool* a_freshOut = nullptr, bool a_receiptProof = false);
		[[nodiscard]] std::optional<nlohmann::json> GetDeathBagRecipientGrants(const std::string& a_profile, const std::string& a_character,
			const std::optional<std::string>& a_cursor = std::nullopt, std::optional<std::string>* a_nextCursor = nullptr);
		[[nodiscard]] bool MarkDeathBagGrantAttempted(const std::string& a_profile, const std::string& a_character, const std::string& a_deathId, const std::string& a_operationId);
		[[nodiscard]] bool ClaimDeathBagGrant(const std::string& a_profile, const std::string& a_character, const std::string& a_deathId, const std::string& a_operationId);
		[[nodiscard]] std::optional<nlohmann::json> GetDeathBag(const std::string& a_deathId) const;
		[[nodiscard]] std::optional<nlohmann::json> DiscoverDeathBags(const std::string& a_profile, const std::string& a_character, bool a_allowOthers) const;
		[[nodiscard]] bool PlaceDeathBag(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character);
		[[nodiscard]] bool VoidDeathBag(const std::string& a_deathId, nlohmann::json* a_stacksOut = nullptr);
		[[nodiscard]] bool QueueDeathBagRefundGrant(const std::string& a_profile, const std::string& a_character,
			const std::string& a_deathId, const std::string& a_operationId, const std::string& a_itemKey, std::uint32_t a_count,
			const nlohmann::json& a_metadata);
		[[nodiscard]] F4T::DeathBag::UnplacedOutcome ReportDeathBagUnplaced(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character, const std::string& a_operationId);
		[[nodiscard]] F4T::DeathBag::PlaceFailedOutcome ApplyDeathBagPlaceFailed(const std::string& a_deathId, const std::string& a_profile, const std::string& a_character, bool a_restored, const nlohmann::json& a_remaining, nlohmann::json* a_stacksOut = nullptr);
		[[nodiscard]] bool RecoverArchivedDeathBag(const std::string& a_deathId);
		[[nodiscard]] bool ResolveUnverifiedDeathBag(const std::string& a_deathId, F4T::DeathBag::Resolution a_resolution);
		[[nodiscard]] nlohmann::json ListArchivedDeathBags(std::size_t a_offset) const;
		[[nodiscard]] std::vector<F4T::DeathBag::Expired> ExpirePendingDeathBags();
		[[nodiscard]] bool MarkDeathBagDeparted(const std::string& a_character, bool a_cleanGoodbye);
		[[nodiscard]] bool ClearDeathBagDeparted(const std::string& a_character);
		[[nodiscard]] bool MarkDeathIntentDead(const std::string& a_characterId, const std::string& a_deathId);
		[[nodiscard]] double CurrentUnixTime() const;
		[[nodiscard]] std::size_t ExpireDueDeathIntents(const std::set<std::string>& a_onlineCharacters);

		[[nodiscard]] F4T::Drops::TransactionResult DropInventory(const std::string& a_profileId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);
		[[nodiscard]] F4T::Drops::TransactionResult PickupInventory(const std::string& a_profileId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);
		[[nodiscard]] F4T::InventorySnapshot::Outcome ReconcileInventory(const std::string& a_profileId,
			const nlohmann::json& a_request);
		void SetInventoryGainPolicy(F4T::InventorySnapshot::GainPolicy a_policy);
		[[nodiscard]] F4T::SurvivalNeeds::Record GetSurvivalNeeds(std::string_view a_profileId) const;
		[[nodiscard]] F4T::SurvivalNeeds::Admission BeginSurvivalClaim(std::string_view a_profileId, F4T::SurvivalNeeds::Claim a_claim);
		[[nodiscard]] bool MutateSurvivalNeeds(const std::vector<std::string>& a_profileIds,
			const std::function<bool(std::string_view, F4T::SurvivalNeeds::Record&)>& a_mutate, bool a_awaitCommit);
		[[nodiscard]] F4T::SurvivalNeeds::Settled SettleSurvivalNeeds(std::string_view a_profileId);
		[[nodiscard]] F4T::Drops::TransactionResult TransferContainer(const std::string& a_profileId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);
		void NoteRefusedContainerMove(const std::string& a_profileId, const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position, std::string_view a_reason);

		[[nodiscard]] nlohmann::json GetLiveDrop(F4T::Drops::EntityId a_entityId) const;
		[[nodiscard]] bool ConsumeLiveDrop(F4T::Drops::EntityId a_entityId);
		[[nodiscard]] F4T::CampGrants::Commit CommitCamp(F4T::Drops::EntityId a_entityId, const nlohmann::json& a_expectedDrop,
			const nlohmann::json& a_workshop, const nlohmann::json& a_settlementAccess, const std::string& a_profileId,
			std::uint64_t a_operationId, const std::vector<F4T::Trade::Line>& a_refund);
		[[nodiscard]] bool CommitCampStore(const nlohmann::json& a_workshop, const std::string& a_profileId,
			std::string_view a_itemKey, const F4T::Drops::InventoryPosition& a_position);
		[[nodiscard]] F4T::CampGrants::Commit CommitCampMove(const nlohmann::json& a_workshop, const std::string& a_profileId,
			std::uint64_t a_operationId, const std::vector<F4T::Trade::Line>& a_lines);
		[[nodiscard]] std::optional<F4T::CampGrants::Commit> FindCampMove(const std::string& a_profileId, std::uint64_t a_operationId) const;
		[[nodiscard]] bool CampKitDropLimited(const std::string& a_profileId) const;
		[[nodiscard]] std::vector<F4T::CampGrants::Grant> PendingCampGrants(const std::string& a_profileId) const;
		[[nodiscard]] bool AckCampGrant(const std::string& a_profileId, std::uint64_t a_operationId);
		[[nodiscard]] F4T::CampGrants::Commit CommitChargeRefund(const std::string& a_profileId, std::uint64_t a_operationId, const std::vector<F4T::Trade::Line>& a_lines);
		[[nodiscard]] bool PostSettlementCharge(const nlohmann::json& a_record);
		[[nodiscard]] bool EraseSettlementCharge(std::uint64_t a_operationId);
		[[nodiscard]] nlohmann::json PendingSettlementCharges() const;
		[[nodiscard]] std::uint64_t SettlementChargeNextOperationId() const;
		[[nodiscard]] std::uint64_t SnapshotRemovals(const std::string& a_profileId, const std::string& a_itemKey) const;
		[[nodiscard]] nlohmann::json GetVendorLedger() const;
		bool SetVendorLedger(const nlohmann::json& a_ledger);
		[[nodiscard]] F4T::Trade::CommitResult TransferTrade(const F4T::Trade::CommitRequest& a_request);
		[[nodiscard]] std::string ServerInstanceId() const;
		[[nodiscard]] std::optional<std::string> RecoveryNotice() const;
		[[nodiscard]] std::optional<std::string> TakePersistenceWarning();

		[[nodiscard]] std::optional<F4T::Identity::PersistedResumeCredential> GetResumeCredential(
			std::string_view a_profileId) override;
		void SaveResumeCredential(
			std::string_view a_profileId,
			const F4T::Identity::TokenHash& a_tokenHash,
			const std::optional<F4T::Identity::TokenHash>& a_previousTokenHash,
			bool a_confirmed,
			double a_expiresAt) override;
		[[nodiscard]] bool DeleteResumeCredential(std::string_view a_profileId) override;

		[[nodiscard]] std::optional<nlohmann::json> GetPlayerState(std::string_view a_profileId) const;
		[[nodiscard]] std::optional<std::string> GetProfileSecret(std::string_view a_profileId) const;
		[[nodiscard]] bool EnsureProfileSecret(std::string_view a_profileId, std::string_view a_secret);
		[[nodiscard]] std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
			std::string_view a_profileId) const;
		[[nodiscard]] bool RecordSaveAttestation(
			std::string_view a_profileId,
			const F4T::SaveProfileLogic::AttestedSave& a_save);
		[[nodiscard]] bool ScopeLegacySaveAttestations(
			std::string_view a_account,
			const std::vector<std::pair<std::uint32_t, std::string>>& a_slotCharacters);
		[[nodiscard]] bool EraseSlotSaveAttestations(std::string_view a_account, std::uint32_t a_slot);
		[[nodiscard]] bool UpdatePlayerState(std::string_view a_profileId, const nlohmann::json& a_playerState);
		[[nodiscard]] bool UpdatePlayerStates(
			const std::unordered_map<std::string, nlohmann::json>& a_playerStates,
			bool* a_refusedOut = nullptr,
			std::vector<std::string>* a_refusedProgressionOut = nullptr);
		[[nodiscard]] std::map<std::uint32_t, nlohmann::json> GetCharacterSlotRecords(std::string_view a_account) const;
		[[nodiscard]] bool SetCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record);
		[[nodiscard]] bool EraseCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation = false);
		[[nodiscard]] bool HasProfileState(std::string_view a_profileId) const;
		[[nodiscard]] bool CreateCharacterSlot(std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record, const nlohmann::json& a_playerState);
		[[nodiscard]] bool EraseCharacterSlotState(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation, bool a_dropParent);
		[[nodiscard]] std::vector<std::string> PendingQuestErase() const;
		[[nodiscard]] bool ClearPendingQuestErase(const std::vector<std::string>& a_questProfiles);
		[[nodiscard]] bool EraseQuestProfilesFromStore(const std::vector<std::string>& a_questProfiles);
		[[nodiscard]] std::vector<FirstJoinAbandonMark> GetFirstJoinAbandonedSlots(std::string_view a_account) const;
		[[nodiscard]] bool SetFirstJoinAbandonedSlot(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds);
		[[nodiscard]] bool DropFirstJoinAbandonedSlot(std::string_view a_account, std::uint32_t a_slot);
		[[nodiscard]] bool ClearFirstJoinAbandonedSlots(std::string_view a_account);
		[[nodiscard]] std::optional<std::string> EnsureLegacyQuestProfile(std::string_view a_account, std::uint32_t a_slot, std::string_view a_questProfile, bool a_allowEmpty);
		[[nodiscard]] bool EraseProfileByKey(std::string_view a_profileKey, bool a_dropParent = true);
		[[nodiscard]] bool MigrateCharacterSlotsToSlotZero(std::string& a_conflictKey);
		[[nodiscard]] nlohmann::json GetServerWorldState() const;
		void UpdateServerWorldState(const nlohmann::json& a_worldState);
		[[nodiscard]] nlohmann::json GetFactions() const;
		[[nodiscard]] nlohmann::json GetSettlementAccess() const;
		[[nodiscard]] nlohmann::json GetKnownPeople() const;
		bool SaveFactions(const nlohmann::json& a_factions);
		bool SaveKnownPeople(const nlohmann::json& a_knownPeople);
		bool SaveSettlementAccess(const nlohmann::json& a_settlementAccess, std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		[[nodiscard]] nlohmann::json GetQuests() const;
		bool SaveQuests(const nlohmann::json& a_quests, bool a_awaitCommit = true);
		bool SaveQuestsBackup(const nlohmann::json& a_quests);
		[[nodiscard]] nlohmann::json GetWorkshop() const;
		bool SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit = true, std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		[[nodiscard]] nlohmann::json GetParkedSuits() const;
		bool SaveParkedSuits(const nlohmann::json& a_parkedSuits, bool a_awaitCommit = true);
		[[nodiscard]] nlohmann::json GetSteamIdentities() const;
		void SaveSteamIdentities(const nlohmann::json& a_steamIdentities);
		void WaitForCommits();
		[[nodiscard]] bool LastWriteFailed() const { return _pipeline->LastWriteFailed(); }
		[[nodiscard]] double Now() const { return _clock(); }
		[[nodiscard]] bool QuestProfileOwnedByAnotherAccount(std::string_view a_account, std::string_view a_profile) const;
		[[nodiscard]] std::vector<std::string> PersistedAccountKeys() const
		{
			std::lock_guard<std::mutex> lock(_gate);
			std::vector<std::string> keys;
			std::multimap<std::string, std::string> questOwners;
			if (const auto table = _root.find("characterSlots"); table != _root.end() && table->is_object()) {
				for (const auto& account : table->items()) {
					if (!account.value().is_object()) continue;
					const auto addOwner = [&](const nlohmann::json& a_id) {
						if (a_id.is_string()) questOwners.emplace(a_id.get<std::string>(), account.key());
					};
					for (const auto& [slot, record] : F4T::CharacterSlots::GetSlotRecords(_root, account.key())) {
						(void)slot;
						if (const auto id = record.find("questProfile"); id != record.end()) addOwner(*id);
					}
					if (const auto pending = account.value().find("_legacyQuest0"); pending != account.value().end()) addOwner(*pending);
					if (const auto archive = account.value().find("_legacyQuestArchive"); archive != account.value().end() && archive->is_array()) {
						for (const auto& id : *archive) addOwner(id);
					}
				}
			}
			const auto addKeys = [&keys, &questOwners](const nlohmann::json& a_map) {
				if (a_map.is_object()) {
					for (const auto& entry : a_map.items()) {
						if (const auto [first, last] = questOwners.equal_range(entry.key()); first != last) {
							for (auto owner = first; owner != last; ++owner) keys.push_back(owner->second);
							continue;
						}
						const auto slot = F4T::CharacterSlots::ParseSlotKey(entry.key());
						keys.push_back(slot ? slot->first : std::string{ entry.key() });
					}
				}
			};
			if (const auto profiles = _root.find("profiles"); profiles != _root.end()) {
				addKeys(*profiles);
			}
			if (const auto quests = _root.find("quests"); quests != _root.end() && quests->is_object()) {
				for (const char* name : { "profiles", "profileActivity" }) {
					if (const auto map = quests->find(name); map != quests->end()) {
						addKeys(*map);
					}
				}
			}
			if (const auto table = _root.find("characterSlots"); table != _root.end() && table->is_object()) {
				for (const auto& account : table->items()) {
					if (!F4T::CharacterSlots::GetSlotRecords(_root, account.key()).empty()) {
						keys.push_back(account.key());
					}
				}
			}
			return keys;
		}
		struct MainThreadSaveTotals
		{
			std::uint64_t saves{};
			double saveSeconds{};
			std::uint64_t commitWaits{};
			double commitWaitSeconds{};
		};
		[[nodiscard]] static MainThreadSaveTotals TakeMainThreadSaveTotals();
		void SetDeferCommits(bool a_deferCommits);
		[[nodiscard]] std::uint64_t LastPostedTicket() const;
		[[nodiscard]] bool TicketDurable(std::uint64_t a_ticket) const;
		[[nodiscard]] bool AllPostedDurable() const;
		[[nodiscard]] bool TicketFailed(std::uint64_t a_ticket) const;
		[[nodiscard]] bool TicketQuarantined(std::uint64_t a_ticket) const;
		[[nodiscard]] bool QuarantinedSince(std::uint64_t a_afterBatch, std::uint64_t a_throughBatch) const;

	private:
		friend struct DurableStateTestAccess;
		[[nodiscard]] static bool PreserveLegacyQuestIdentity(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record);
		static void PreserveLegacyAccountQuests(nlohmann::json& a_root, std::string_view a_account);
		[[nodiscard]] nlohmann::json LoadFile(const std::filesystem::path& a_path, bool& a_migrated) const;
		void LoadOrCreate();
		void RestoreRecoveredPrimaryLocked();
		void PersistLocked();
		void PostSectionsLocked(std::span<const std::string_view> a_sections, bool a_awaitCommit, bool a_forceAwait = false);
		bool MutateAndPersistLocked(
			std::span<const std::string_view> a_sections,
			const std::function<bool(nlohmann::json&)>& a_mutate,
			bool a_awaitCommit, bool a_forceAwait = false);
		// constraint: a_mutate may add, change or erase only the listed profiles; everything else it touches must be in a_sections.
		bool MutateProfilesAndPersistLocked(
			std::vector<std::string> a_profileIds,
			std::span<const std::string_view> a_sections,
			const std::function<bool(nlohmann::json&)>& a_mutate,
			bool a_awaitCommit, bool a_forceAwait = false);
		bool MutateLocked(
			const std::vector<std::string>* a_profileIds,
			std::span<const std::string_view> a_sections,
			const std::function<bool(nlohmann::json&)>& a_mutate,
			bool a_awaitCommit, bool a_forceAwait);
		void PostLocked(
			const std::vector<std::string>* a_profileIds,
			std::span<const std::string_view> a_sections,
			bool a_awaitCommit, bool a_forceAwait);
		static void LogWidenedProfileSave(bool a_membership);
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> BuildDropSnapshotLocked() const;
		void LogMainThreadSave(double a_seconds, std::size_t a_sections, bool a_waited) const;
		static void LogQuarantineWarning(std::string_view a_section, std::string_view a_error);
		void LogGainLimited(const std::string& a_profileId, const std::string& a_detail, double a_now);

		std::filesystem::path _path;
		std::filesystem::path _backupPath;
		Clock _clock;
		InstanceIdGenerator _instanceIdGenerator;
		PostCommitHook _postCommitHook;
		mutable std::mutex _gate;
		nlohmann::json _root;
		mutable std::optional<F4T::Replication::SnapshotState> _dropSnapshot;
		mutable bool _dropSnapshotCached{};
		std::map<std::string, std::vector<RecentInventoryDrop>> _recentInventoryDrops;
		F4T::InventorySnapshot::GainPolicy _gainPolicy;
		std::map<std::string, F4T::InventorySnapshot::GainBuckets> _gainBuckets;
		std::map<std::string, double> _gainLoggedAt;
		std::optional<std::string> _recoveryNotice;
		std::optional<std::string> _persistenceWarning;
		bool _deferCommits{};
		bool _auditUnlistedProfiles{};
		std::uint64_t _profilesPostedWholeThrough{};
		std::unique_ptr<DurableWritePipeline> _pipeline;
	};

	[[nodiscard]] bool ScrubKnownPeopleReferences(nlohmann::json& a_root, std::string_view a_profileKey, bool a_dropParent = true);
	[[nodiscard]] bool QueuePendingQuestErase(nlohmann::json& a_root, const nlohmann::json& a_erasedRecord, const nlohmann::json& a_keptRecord);
}
