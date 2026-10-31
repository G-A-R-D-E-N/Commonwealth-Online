#pragma once

#include "F4TDropLedger.h"
#include "F4TFormIdentity.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::InventoryContract
{
	inline constexpr std::size_t kMaximumItemKeyChars = 137;

	inline constexpr std::string_view kInventoryTransferType = "inventoryTransfer";
	inline constexpr std::size_t kMaximumInventoryTransferChunkBytes = 48 * 1024;
	inline constexpr std::size_t kMaximumInventoryTransferBytes = 512 * 1024;
	inline constexpr std::size_t kMaximumInventoryTransferChunks = 64;
	inline constexpr std::size_t kMaximumInventoryTransferItems = 4096;

	struct InventoryTransferHeader
	{
		std::uint32_t revision{};
		std::uint32_t itemCount{};
		std::uint32_t chunkCount{};
	};

	class InventoryTransferReceiver
	{
	public:
		enum class Result : std::uint8_t
		{
			Accepted,
			Complete,
			Rejected
		};

		bool Begin(const InventoryTransferHeader& a_header);
		Result Accept(const nlohmann::json& a_chunk, nlohmann::json& a_inventory);
		void Reset();
		[[nodiscard]] bool Active() const { return _active; }

	private:
		InventoryTransferHeader _header{};
		std::uint32_t           _nextChunk{};
		std::uint32_t           _receivedItems{};
		std::size_t             _receivedBytes{};
		bool                    _active{};
		nlohmann::json          _items = nlohmann::json::object();
	};

	struct ItemKeyParts
	{
		std::string plugin;
		std::uint32_t localId{};
	};

	struct ServerItem
	{
		std::string plugin;
		std::string localFormId;
		std::uint32_t count{};
	};

	struct ServerInventoryView
	{
		std::uint32_t revision{};
		std::map<std::string, ServerItem> items;
	};

	enum class OwnedOperation : std::uint8_t
	{
		Drop,
		Pickup
	};

	enum class OwnedReason : std::uint8_t
	{
		None,
		InvalidRequest,
		OperationMismatch,
		StaleRevision,
		InsufficientItems,
		CapacityExceeded,
		OutOfScope,
		UnknownEntity,
		RateLimited,
		InventoryAuthorityUnavailable,
		InventoryPersistenceUnavailable,
		Unknown
	};

	struct OwnedResult
	{
		std::string operationId;
		bool committed{};
		OwnedReason reason{ OwnedReason::None };
		std::optional<std::uint32_t> revision;
		std::optional<Drops::EntityId> entityId;
		nlohmann::json drop;
	};

	[[nodiscard]] std::optional<std::string> MakeItemKey(const std::string& a_plugin, std::uint32_t a_localId, F4T::FormIdentity::Kind a_kind);
	[[nodiscard]] std::optional<ItemKeyParts> ParseItemKey(const std::string& a_key);
	[[nodiscard]] std::optional<ServerInventoryView> ParseServerInventory(const nlohmann::json& a_inventory);
	[[nodiscard]] std::optional<InventoryTransferHeader> ParseInventoryTransferHeader(const nlohmann::json& a_serverInventory);
	[[nodiscard]] std::optional<std::vector<nlohmann::json>> BuildInventoryTransferChunks(
		const nlohmann::json& a_inventory,
		std::string& a_error);
	[[nodiscard]] std::optional<OwnedResult> ParseOwnedResult(const nlohmann::json& a_packet, OwnedOperation a_expected);
	[[nodiscard]] std::optional<std::string> MakeOperationId(std::string_view a_sessionNonce, std::uint32_t a_counter);
	[[nodiscard]] std::optional<nlohmann::json> BuildDropRequest(const std::string& a_operationId, std::uint32_t a_revision,
		const std::string& a_itemKey, std::uint32_t a_count);
	[[nodiscard]] std::optional<nlohmann::json> BuildPickupRequest(const std::string& a_operationId, std::uint32_t a_revision,
		Drops::EntityId a_entityId);
}
