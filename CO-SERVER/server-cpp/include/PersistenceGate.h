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
			std::span<const std::uint8_t> a_payload) const;
		[[nodiscard]] bool QueueAcceptedPlayerState(
			std::uint32_t a_connectionId,
			const nlohmann::json& a_playerState);
		[[nodiscard]] bool FlushPendingPlayerStates(bool a_awaitCommit = false);
		void SetSavingEnabled(bool a_enabled);
		[[nodiscard]] bool SavingEnabled() const;
		[[nodiscard]] std::optional<DurableReplayPlan> SessionReadyDelivered(
			const OutboundFrame& a_frame,
			double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> TakeInventoryTransferFrames(std::uint32_t a_connectionId);
		void ConnectionClosed(std::uint32_t a_connectionId);
		void RebindConnectionProfile(std::uint32_t a_connectionId, std::string_view a_profileKey);
		[[nodiscard]] DurableStateFileStore& Store() noexcept;
		[[nodiscard]] std::optional<std::string> ProfileForConnection(std::uint32_t a_connectionId) const;
		[[nodiscard]] std::optional<F4T::Drops::TransactionResult> ApplyInventoryRequest(std::uint32_t a_connectionId,
			const nlohmann::json& a_request, const F4T::Drops::InventoryPosition& a_position, bool a_drop);
		[[nodiscard]] std::optional<F4T::Replication::SnapshotState> GetDropSnapshot() const;
		[[nodiscard]] nlohmann::json GetFactions() const;
		[[nodiscard]] nlohmann::json ReadStoredFactions() const;
		void SaveFactions(const nlohmann::json& a_factions);
		[[nodiscard]] nlohmann::json ReadStoredSettlementAccess() const;
		void SaveSettlementAccess(const nlohmann::json& a_settlementAccess);
		[[nodiscard]] nlohmann::json GetQuests() const;
		void SaveQuests(const nlohmann::json& a_quests);
		void QueueQuests(const nlohmann::json& a_quests);
		[[nodiscard]] bool FlushQuests(bool a_awaitCommit = false);
		void SaveQuestsBackup(const nlohmann::json& a_quests);
		[[nodiscard]] nlohmann::json GetWorkshop() const;
		void SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit = false);

	private:
		[[nodiscard]] bool FlushQuestsLocked(bool a_awaitCommit);

		DurableStateFileStore& _store;
		std::optional<nlohmann::json> _startingInventory;
		mutable std::mutex _gate;
		bool _savingEnabled{ true };
		std::unordered_map<std::uint32_t, std::string> _profilesByConnection;
		std::unordered_map<std::string, nlohmann::json> _pendingPlayerStates;
		std::optional<nlohmann::json> _pendingQuests;
		std::unordered_map<std::uint32_t, std::vector<nlohmann::json>> _inventoryTransfers;
	};
}
