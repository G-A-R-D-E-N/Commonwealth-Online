#pragma once

#include "DurableCommitWriter.h"
#include "F4TDurableState.h"
#include "F4TDropLedger.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CommonwealthOnline::Server
{
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
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> GetDropSnapshot() const;
		[[nodiscard]] bool InventoryAuthorityBlocked() const;

		[[nodiscard]] F4T::Drops::TransactionResult DropInventory(const std::string& a_profileId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);
		[[nodiscard]] F4T::Drops::TransactionResult PickupInventory(const std::string& a_profileId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);

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
		[[nodiscard]] bool UpdatePlayerState(std::string_view a_profileId, const nlohmann::json& a_playerState);
		[[nodiscard]] bool UpdatePlayerStates(const std::unordered_map<std::string, nlohmann::json>& a_playerStates);
		[[nodiscard]] std::map<std::uint32_t, nlohmann::json> GetCharacterSlotRecords(std::string_view a_account) const;
		[[nodiscard]] bool SetCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record);
		[[nodiscard]] bool EraseCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot);
		[[nodiscard]] bool EraseProfileByKey(std::string_view a_profileKey);
		[[nodiscard]] bool MigrateCharacterSlotsToSlotZero(std::string& a_conflictKey);
		[[nodiscard]] nlohmann::json GetServerWorldState() const;
		void UpdateServerWorldState(const nlohmann::json& a_worldState);
		[[nodiscard]] nlohmann::json GetFactions() const;
		[[nodiscard]] nlohmann::json GetSettlementAccess() const;
		void SaveFactions(const nlohmann::json& a_factions);
		void SaveSettlementAccess(const nlohmann::json& a_settlementAccess);
		[[nodiscard]] nlohmann::json GetQuests() const;
		void SaveQuests(const nlohmann::json& a_quests, bool a_awaitCommit = true);
		void SaveQuestsBackup(const nlohmann::json& a_quests);
		[[nodiscard]] nlohmann::json GetWorkshop() const;
		void SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit = true);
		[[nodiscard]] nlohmann::json GetSteamIdentities() const;
		void SaveSteamIdentities(const nlohmann::json& a_steamIdentities);
		void WaitForCommits();

	private:
		[[nodiscard]] nlohmann::json LoadFile(const std::filesystem::path& a_path, bool& a_migrated) const;
		void LoadOrCreate();
		void RestoreRecoveredPrimaryLocked();
		void PersistLocked();
		void PostRootLocked(bool a_awaitCommit);
		bool MutateAndPersistLocked(const std::function<bool(nlohmann::json&)>& a_mutate, bool a_awaitCommit);
		[[nodiscard]] std::string BuildContentsLocked();
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> BuildDropSnapshotLocked() const;
		void CommitContents(std::string_view a_contents, bool& a_committed);
		void CommitFromWriter(const std::string& a_contents);

		static void WriteFileSync(const std::filesystem::path& a_path, std::string_view a_contents);
		static void SyncExistingFile(const std::filesystem::path& a_path);
		static void AtomicReplace(
			const std::filesystem::path& a_source,
			const std::filesystem::path& a_destination,
			bool* a_replaced = nullptr);
		static void SyncDirectory(const std::filesystem::path& a_directory);
		[[nodiscard]] static std::string UniqueSuffix();

		std::filesystem::path _path;
		std::filesystem::path _backupPath;
		Clock _clock;
		InstanceIdGenerator _instanceIdGenerator;
		PostCommitHook _postCommitHook;
		mutable std::mutex _gate;
		nlohmann::json _root;
		mutable std::optional<F4T::Replication::SnapshotState> _dropSnapshot;
		mutable bool _dropSnapshotCached{};
		std::optional<std::string> _recoveryNotice;
		std::optional<std::string> _persistenceWarning;
		DurableCommitWriter _writer;
	};
}
