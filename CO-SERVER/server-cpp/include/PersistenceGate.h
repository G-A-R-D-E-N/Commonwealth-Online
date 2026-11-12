#pragma once

#include "DurableStateFileStore.h"
#include "ServerProtocol.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DurableReplayPlan
	{
		std::string profileId;
		std::uint32_t playerId{};
		std::optional<nlohmann::json> playerState;
		std::vector<nlohmann::json> worldPackets;
	};

	class PersistenceGate
	{
	public:
		explicit PersistenceGate(DurableStateFileStore& a_store, std::optional<nlohmann::json> a_startingInventory = std::nullopt);
		[[nodiscard]] bool PrepareSessionReady(OutboundFrame& a_frame, std::string& a_error);

		[[nodiscard]] std::optional<nlohmann::json> CapturePlayerStateCandidate(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_payload,
			const std::optional<nlohmann::json>* a_parsed = nullptr) const;
		[[nodiscard]] bool QueueAcceptedPlayerState(
			std::uint32_t a_connectionId,
			const nlohmann::json& a_playerState);
		[[nodiscard]] bool FlushPendingPlayerStates(bool a_awaitCommit = false, bool* a_refusedOut = nullptr);
		[[nodiscard]] std::vector<OutboundFrame> TakeProgressionRejections();
		bool SetSavingEnabled(bool a_enabled);
		[[nodiscard]] bool SavingEnabled() const;
		[[nodiscard]] std::optional<DurableReplayPlan> SessionReadyDelivered(
			const OutboundFrame& a_frame,
			double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> TakeInventoryTransferFrames(std::uint32_t a_connectionId);
		[[nodiscard]] std::vector<OutboundFrame> TakeStartingGrantFrames(std::uint32_t a_connectionId);
		[[nodiscard]] bool AckStartingGrant(std::uint32_t a_connectionId, std::string_view a_profileId, std::string_view a_grantId);
		[[nodiscard]] std::vector<OutboundFrame> BeginStartingGrant(std::uint32_t a_connectionId, std::string_view a_profileId,
			std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_baseline);
		[[nodiscard]] bool VerifyStartingGrantSave(std::uint32_t a_connectionId, const F4T::SaveProfileLogic::AttestedSave& a_save);
		void InvalidateStartingGrantSave(std::uint32_t a_connectionId);
		[[nodiscard]] bool InventorySaveReady(std::uint32_t a_connectionId) const;
		[[nodiscard]] std::optional<F4T::Trade::CommitResult> TransferTrade(std::uint32_t a_first, std::uint32_t a_second,
			const F4T::Trade::CommitRequest& a_request);
		void ConnectionClosed(std::uint32_t a_connectionId);
		void RebindConnectionProfile(std::uint32_t a_connectionId, std::string_view a_profileKey);
		[[nodiscard]] DurableStateFileStore& Store() noexcept;
		[[nodiscard]] std::optional<std::string> ProfileForConnection(std::uint32_t a_connectionId) const;
		[[nodiscard]] bool ProfileInUseByOtherConnection(std::string_view a_profileKey, std::uint32_t a_exceptConnection) const;
		[[nodiscard]] std::optional<F4T::InventorySnapshot::Outcome> ReconcileInventory(std::uint32_t a_connectionId,
			const nlohmann::json& a_request);
		[[nodiscard]] std::optional<F4T::Drops::TransactionResult> ApplyInventoryRequest(std::uint32_t a_connectionId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position, bool a_drop);
		[[nodiscard]] std::optional<F4T::Drops::TransactionResult> ApplyContainerRequest(std::uint32_t a_connectionId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position);
		void NoteRefusedContainerMove(std::uint32_t a_connectionId, const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position, std::string_view a_reason);
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> GetDropSnapshot() const;
		[[nodiscard]] nlohmann::json GetFactions() const;
		[[nodiscard]] nlohmann::json ReadStoredFactions() const;
		bool SaveFactions(const nlohmann::json& a_factions);
		[[nodiscard]] nlohmann::json ReadStoredKnownPeople() const;
		bool SaveKnownPeople(const nlohmann::json& a_knownPeople);
		[[nodiscard]] nlohmann::json ReadStoredSettlementAccess() const;
		bool SaveSettlementAccess(const nlohmann::json& a_settlementAccess, std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		[[nodiscard]] nlohmann::json GetQuests() const;
		bool SaveQuests(const nlohmann::json& a_quests);
		void QueueQuests(const nlohmann::json& a_quests);
		[[nodiscard]] bool FlushQuests(bool a_awaitCommit = false);
		bool SaveQuestsBackup(const nlohmann::json& a_quests);
		[[nodiscard]] nlohmann::json GetWorkshop() const;
		bool SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit = false, std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		[[nodiscard]] nlohmann::json ReadStoredParkedSuits() const;
		bool SaveParkedSuits(const nlohmann::json& a_parkedSuits, bool a_awaitCommit = false);

	private:
		[[nodiscard]] bool InventorySaveReadyLocked(std::uint32_t a_connectionId) const;
		[[nodiscard]] bool FlushQuestsLocked(bool a_awaitCommit);
		[[nodiscard]] bool RetryPendingPlayerStatesLocked(bool& a_refusedOut);

		DurableStateFileStore& _store;
		nlohmann::json _startingInventory;
		mutable std::mutex _gate;
		bool _savingEnabled{ true };
		std::unordered_map<std::uint32_t, std::string> _profilesByConnection;
		std::unordered_set<std::uint32_t> _inventoryVerified;
		std::unordered_set<std::uint32_t> _refusedProgression;
		std::unordered_map<std::string, nlohmann::json> _pendingPlayerStates;
		std::optional<nlohmann::json> _pendingQuests;
		std::unordered_map<std::uint32_t, std::vector<nlohmann::json>> _inventoryTransfers;
		std::unordered_map<std::uint32_t, std::vector<nlohmann::json>> _startingGrants;
	};
}
