#pragma once

#include "F4TClientCompatibility.h"
#include "F4TDropMirrorTable.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryOperations.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::InventorySync
{
	using EntityId = F4T::Drops::EntityId;
	using OwnedResult = F4T::InventoryContract::OwnedResult;
	using ServerInventoryView = F4T::InventoryContract::ServerInventoryView;

	inline constexpr std::size_t kMaximumInboundResults = 64;
	inline constexpr std::string_view kInventoryProtocolCapability = "inventory-stable-v1";
	inline constexpr std::string_view kSnapshotCapability = "component-snapshot-v1";
	inline constexpr std::string_view kSnapshotChannelCapability = "component-snapshot-channels-v1";
	inline constexpr std::array<std::string_view, 3> kSessionCapabilities{
		kInventoryProtocolCapability, kSnapshotCapability, kSnapshotChannelCapability
	};

	inline void AddSessionCapabilities(F4T::ClientCompatibility::Metadata& a_metadata)
	{
		for (const auto capability : kSessionCapabilities) {
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
	void NoteTransferChunk(const nlohmann::json& a_chunk);
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
}
