#pragma once

#include "F4TClientCompatibility.h"
#include "F4TDropMirrorTable.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryOperations.h"
#include "F4TTradeLedger.h"
#include "F4TTradeWindow.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::InventorySync
{
	using EntityId = F4T::Drops::EntityId;
	using OwnedResult = F4T::InventoryContract::OwnedResult;
	using ServerInventoryView = F4T::InventoryContract::ServerInventoryView;

	inline constexpr std::size_t kMaximumInboundResults = 64;
	inline constexpr std::string_view kInventoryProtocolCapability = "inventory-stable-v1";
	inline constexpr std::string_view kSnapshotCapability = "component-snapshot-v1";
	inline constexpr std::string_view kSnapshotChannelCapability = "component-snapshot-channels-v1";
	inline constexpr std::string_view kTradeCapability = "player-trade-v1";
	inline constexpr std::array<std::string_view, 4> kSessionCapabilities{
		kInventoryProtocolCapability, kSnapshotCapability, kSnapshotChannelCapability, kTradeCapability
	};
	inline thread_local bool g_tradeApplying = false;

	inline void AddSessionCapabilities(F4T::ClientCompatibility::Metadata& a_metadata)
	{
		for (const auto capability : kSessionCapabilities) {
			if (capability == kTradeCapability && !F4T::TradeWindow::Attached()) {
				continue;
			}
			a_metadata.capabilities.emplace_back(capability);
		}
	}

	struct Status
	{
		bool sessionActive{ false };
		bool hasServerInventory{ false };
		std::uint32_t serverRevision{};
		std::size_t pendingOperations{};
		std::size_t queuedResults{};
		std::uint64_t droppedResults{};
		bool resyncNeeded{ false };
		std::size_t trackedEntities{};
		std::size_t mirroredEntities{};
	};

	void NoteWelcome(const nlohmann::json& a_welcome);
	void NoteOwnedResult(const OwnedResult& a_result);
	bool NoteTradeCommitted(std::uint32_t a_revision, const std::vector<F4T::Trade::Line>& a_give, const std::vector<F4T::Trade::Line>& a_receive);
	bool HoldsLines(const std::vector<F4T::Trade::Line>& a_lines);
	[[nodiscard]] std::vector<F4T::Trade::Line> LocalHoldings();
	void NoteResyncNeeded();
	[[nodiscard]] bool TakeResyncNeeded();
	void NoteTransferChunk(const nlohmann::json& a_chunk);
	void NoteSnapshotResult(const nlohmann::json& a_result);
	[[nodiscard]] std::optional<std::string> BeginOwnedDrop(const std::string& a_itemKey, std::uint32_t a_count);
	[[nodiscard]] std::optional<std::string> BeginOwnedPickup(EntityId a_entityId);
	bool ReserveOwnedEntity(EntityId a_entityId);
	bool BindOwnedEntity(EntityId a_entityId, const std::string& a_refKey);
	bool UnbindOwnedEntity(EntityId a_entityId);
	[[nodiscard]] std::optional<EntityId> FindOwnedEntity(const std::string& a_refKey);
	void BindMirrorEntity(EntityId a_entityId, std::uint32_t a_refFormId);
	void UnbindMirrorEntity(EntityId a_entityId);
	bool ApplyDropFrame(const F4T::Replication::DecodedFrame& a_frame);
	[[nodiscard]] F4T::Drops::MirrorDiff TakeDropMirrorDiff(std::uint64_t& a_generation);
	void Tick(std::uint64_t a_nowMs);
	void Reset();
	[[nodiscard]] Status GetStatus();
	[[nodiscard]] std::optional<ServerInventoryView> ServerInventory();
	[[nodiscard]] bool TradeSupported();
}
