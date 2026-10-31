#include "F4TInventoryContract.h"

#include "F4TDurableState.h"

#include <charconv>
#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace F4T::InventoryContract
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::string> NormalizedKey(const std::string& a_plugin, std::uint32_t a_localId)
		{
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", a_localId);
			try {
				const auto catalog = F4T::DurableState::NormalizeStartingInventory(Json::array({ Json{ { "plugin", a_plugin }, { "localFormId", form }, { "count", 1u } } }));
				return catalog.begin().key();
			} catch (const std::exception&) {
				return std::nullopt;
			}
		}

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_packet, const char* a_field)
		{
			const auto found = a_packet.find(a_field);
			if (found == a_packet.end() || !found->is_number_unsigned()) return std::nullopt;
			const auto value = found->get<std::uint64_t>();
			if (value > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
			return static_cast<std::uint32_t>(value);
		}

		OwnedReason ReasonFrom(const std::string& a_text)
		{
			constexpr std::pair<std::string_view, OwnedReason> reasons[] = {
				{ "invalidRequest", OwnedReason::InvalidRequest },
				{ "operationMismatch", OwnedReason::OperationMismatch },
				{ "staleRevision", OwnedReason::StaleRevision },
				{ "insufficientItems", OwnedReason::InsufficientItems },
				{ "capacityExceeded", OwnedReason::CapacityExceeded },
				{ "outOfScope", OwnedReason::OutOfScope },
				{ "unknownEntity", OwnedReason::UnknownEntity },
				{ "rateLimited", OwnedReason::RateLimited },
				{ "inventoryAuthorityUnavailable", OwnedReason::InventoryAuthorityUnavailable },
				{ "inventoryPersistenceUnavailable", OwnedReason::InventoryPersistenceUnavailable } };
			for (const auto& [name, reason] : reasons) {
				if (a_text == name) return reason;
			}
			return OwnedReason::Unknown;
		}

		bool ValidDropObject(const Json& a_object)
		{
			if (!a_object.is_object() || a_object.size() != 4 || !a_object.contains("ownerProfileId") || !a_object.at("ownerProfileId").is_string() ||
				!a_object.contains("itemKey") || !a_object.at("itemKey").is_string() || !a_object.contains("item") || !a_object.contains("position") ||
				!a_object.at("position").is_object() || a_object.at("position").size() != 5) return false;
			try {
				const auto& item = a_object.at("item");
				const auto& position = a_object.at("position");
				const auto catalog = F4T::DurableState::NormalizeStartingInventory(Json::array({ item }));
				const F4T::Drops::StableDroppedObject object{ item, { position.at("cellId").get<std::string>(), position.at("worldspaceId").get<std::string>(),
					position.at("x").get<float>(), position.at("y").get<float>(), position.at("z").get<float>() } };
				return catalog.contains(a_object.at("itemKey").get<std::string>()) && F4T::Drops::EncodeStableDroppedObject(object).has_value();
			} catch (const std::exception&) {
				return false;
			}
		}
	}

	std::optional<std::string> MakeItemKey(const std::string& a_plugin, std::uint32_t a_localId, F4T::FormIdentity::Kind a_kind)
	{
		if (!F4T::FormIdentity::IsValidRecordIdentity({ a_plugin, a_localId }, a_kind)) return std::nullopt;
		return NormalizedKey(a_plugin, a_localId);
	}

	std::optional<ItemKeyParts> ParseItemKey(const std::string& a_key)
	{
		const auto colon = a_key.rfind(':');
		if (colon == std::string::npos || a_key.size() - colon != 9) return std::nullopt;
		const auto hex = a_key.substr(colon + 1);
		std::uint32_t localId{};
		const auto [end, error] = std::from_chars(hex.data(), hex.data() + hex.size(), localId, 16);
		if (error != std::errc{} || end != hex.data() + hex.size()) return std::nullopt;
		ItemKeyParts parts{ a_key.substr(0, colon), localId };
		const auto key = NormalizedKey(parts.plugin, parts.localId);
		if (!key || *key != a_key) return std::nullopt;
		return parts;
	}

	std::optional<ServerInventoryView> ParseServerInventory(const Json& a_inventory)
	{
		try {
			F4T::DurableState::ValidateInventory(a_inventory);
		} catch (const std::exception&) {
			return std::nullopt;
		}
		ServerInventoryView view;
		view.revision = a_inventory.at("revision").get<std::uint32_t>();
		for (const auto& [key, item] : a_inventory.at("items").items()) {
			view.items[key] = ServerItem{ item.at("plugin").get<std::string>(), item.at("localFormId").get<std::string>(), item.at("count").get<std::uint32_t>() };
		}
		return view;
	}

	namespace
	{
		bool ValidTransferItem(const std::string& a_key, const Json& a_item)
		{
			if (!a_item.is_object() || a_item.size() != 3 || !a_item.contains("plugin") || !a_item.at("plugin").is_string() ||
				!a_item.contains("localFormId") || !a_item.at("localFormId").is_string() ||
				!a_item.contains("count") || !a_item.at("count").is_number_unsigned()) return false;
			const auto parts = ParseItemKey(a_key);
			if (!parts || parts->plugin != a_item.at("plugin").get_ref<const std::string&>()) return false;
			char normalized[9]{};
			std::snprintf(normalized, sizeof(normalized), "%08X", parts->localId);
			if (a_item.at("localFormId").get_ref<const std::string&>() != normalized) return false;
			const auto count = a_item.at("count").get<std::uint64_t>();
			return count >= 1 && count <= F4T::Drops::kMaximumDropCount;
		}

		Json BuildTransferChunk(const Json& a_items, std::uint32_t a_revision, std::uint32_t a_chunkIndex,
			std::uint32_t a_chunkCount, std::uint32_t a_itemCount, bool a_lastChunk)
		{
			return Json{ { "type", std::string{ kInventoryTransferType } }, { "revision", a_revision },
				{ "chunkIndex", a_chunkIndex }, { "chunkCount", a_chunkCount }, { "itemCount", a_itemCount },
				{ "lastChunk", a_lastChunk }, { "items", a_items } };
		}
	}

	std::optional<InventoryTransferHeader> ParseInventoryTransferHeader(const Json& a_serverInventory)
	{
		if (!a_serverInventory.is_object() || a_serverInventory.size() != 4 || !a_serverInventory.contains("initialized") ||
			!a_serverInventory.at("initialized").is_boolean() || a_serverInventory.at("initialized") != true) return std::nullopt;
		const auto revision = ReadUnsigned(a_serverInventory, "revision");
		const auto itemCount = ReadUnsigned(a_serverInventory, "itemCount");
		const auto chunkCount = ReadUnsigned(a_serverInventory, "chunkCount");
		if (!revision || !itemCount || !chunkCount || *chunkCount == 0 || *chunkCount > kMaximumInventoryTransferChunks ||
			*itemCount > kMaximumInventoryTransferItems) return std::nullopt;
		return InventoryTransferHeader{ *revision, *itemCount, *chunkCount };
	}

	std::optional<std::vector<Json>> BuildInventoryTransferChunks(const Json& a_inventory, std::string& a_error)
	{
		a_error.clear();
		const auto fail = [&](std::string a_message) -> std::optional<std::vector<Json>> {
			a_error = std::move(a_message);
			return std::nullopt;
		};
		if (!a_inventory.is_object() || a_inventory.size() != 3 || !a_inventory.contains("initialized") ||
			!a_inventory.at("initialized").is_boolean() || a_inventory.at("initialized") != true ||
			!a_inventory.contains("items") || !a_inventory.at("items").is_object()) {
			return fail("inventory transfer source has an invalid shape");
		}
		const auto revision = ReadUnsigned(a_inventory, "revision");
		if (!revision) return fail("inventory transfer source has an invalid revision");
		const auto& items = a_inventory.at("items");
		if (items.size() > kMaximumInventoryTransferItems) return fail("inventory transfer exceeds the item limit");
		const auto itemCount = static_cast<std::uint32_t>(items.size());

		constexpr std::size_t kChunkEnvelopeBytes = 160;
		constexpr std::size_t kItemsBudget = kMaximumInventoryTransferChunkBytes - kChunkEnvelopeBytes;
		std::vector<Json> chunks;
		Json current = Json::object();
		for (const auto& [key, item] : items.items()) {
			if (!ValidTransferItem(key, item)) return fail("inventory transfer item '" + key + "' is invalid");
			current[key] = item;
			if (current.dump().size() <= kItemsBudget) continue;
			current.erase(key);
			if (current.empty()) return fail("inventory transfer item '" + key + "' exceeds the per-chunk byte limit");
			chunks.push_back(std::move(current));
			if (chunks.size() >= kMaximumInventoryTransferChunks) return fail("inventory transfer exceeds the chunk limit");
			current = Json::object();
			current[key] = item;
			if (current.dump().size() > kItemsBudget) return fail("inventory transfer item '" + key + "' exceeds the per-chunk byte limit");
		}
		chunks.push_back(std::move(current));

		const auto chunkCount = static_cast<std::uint32_t>(chunks.size());
		std::size_t totalBytes = 0;
		for (std::size_t index = 0; index < chunks.size(); ++index) {
			const bool last = index + 1 == chunks.size();
			Json packet = BuildTransferChunk(std::move(chunks[index]), *revision, static_cast<std::uint32_t>(index),
				chunkCount, itemCount, last);
			totalBytes += packet.dump().size();
			chunks[index] = std::move(packet);
		}
		if (totalBytes > kMaximumInventoryTransferBytes) return fail("inventory transfer exceeds the total byte limit");
		return chunks;
	}

	bool InventoryTransferReceiver::Begin(const InventoryTransferHeader& a_header)
	{
		Reset();
		if (a_header.chunkCount == 0 || a_header.chunkCount > kMaximumInventoryTransferChunks ||
			a_header.itemCount > kMaximumInventoryTransferItems) return false;
		_header = a_header;
		_active = true;
		return true;
	}

	InventoryTransferReceiver::Result InventoryTransferReceiver::Accept(const Json& a_chunk, Json& a_inventory)
	{
		if (!_active) return Result::Rejected;
		const auto reject = [this] {
			Reset();
			return Result::Rejected;
		};
		if (!a_chunk.is_object() || a_chunk.size() != 7 || !a_chunk.contains("type") || !a_chunk.at("type").is_string() ||
			a_chunk.at("type").get_ref<const std::string&>() != kInventoryTransferType ||
			!a_chunk.contains("lastChunk") || !a_chunk.at("lastChunk").is_boolean() ||
			!a_chunk.contains("items") || !a_chunk.at("items").is_object()) return reject();
		const auto revision = ReadUnsigned(a_chunk, "revision");
		const auto chunkIndex = ReadUnsigned(a_chunk, "chunkIndex");
		const auto chunkCount = ReadUnsigned(a_chunk, "chunkCount");
		const auto itemCount = ReadUnsigned(a_chunk, "itemCount");
		if (!revision || !chunkIndex || !chunkCount || !itemCount || *revision != _header.revision ||
			*chunkCount != _header.chunkCount || *itemCount != _header.itemCount) return reject();
		if (*chunkIndex != _nextChunk) return reject();
		if (a_chunk.at("lastChunk").get<bool>() != (*chunkIndex + 1 == *chunkCount)) return reject();
		for (const auto& [key, item] : a_chunk.at("items").items()) {
			if (!ValidTransferItem(key, item) || _items.contains(key)) return reject();
			_items[key] = item;
			++_receivedItems;
			_receivedBytes += item.dump().size();
			if (_receivedItems > _header.itemCount || _receivedBytes > kMaximumInventoryTransferBytes) return reject();
		}
		++_nextChunk;
		if (_nextChunk < _header.chunkCount) return Result::Accepted;
		if (_receivedItems != _header.itemCount) return reject();
		a_inventory = Json{ { "initialized", true }, { "revision", _header.revision }, { "items", std::move(_items) } };
		Reset();
		return Result::Complete;
	}

	void InventoryTransferReceiver::Reset()
	{
		_header = {};
		_nextChunk = 0;
		_receivedItems = 0;
		_receivedBytes = 0;
		_active = false;
		_items = Json::object();
	}

	std::optional<OwnedResult> ParseOwnedResult(const Json& a_packet, OwnedOperation a_expected)
	{
		const bool drop = a_expected == OwnedOperation::Drop;
		if (!a_packet.is_object() || !a_packet.contains("type") || a_packet.at("type") != (drop ? "inventoryDropResult" : "inventoryPickupResult") ||
			!a_packet.contains("operationId") || !a_packet.at("operationId").is_string() || !a_packet.contains("status") || !a_packet.at("status").is_string()) return std::nullopt;
		OwnedResult result;
		result.operationId = a_packet.at("operationId").get<std::string>();
		if (result.operationId.empty() || result.operationId.size() > F4T::Drops::kMaximumOperationIdChars) return std::nullopt;
		const auto status = a_packet.at("status").get<std::string>();
		if (status == "committed") {
			const auto revision = ReadUnsigned(a_packet, "revision");
			const auto entity = ReadUnsigned(a_packet, "entityId");
			if (a_packet.size() != 6 || !revision || !entity || *entity < F4T::Drops::kFirstDroppedEntityId ||
				!a_packet.contains("drop") || !ValidDropObject(a_packet.at("drop"))) return std::nullopt;
			result.committed = true;
			result.revision = revision;
			result.entityId = entity;
			result.drop = a_packet.at("drop");
			return result;
		}
		if (status != "rejected" || !a_packet.contains("reason") || !a_packet.at("reason").is_string()) return std::nullopt;
		const auto extras = static_cast<std::size_t>(a_packet.contains("revision")) + static_cast<std::size_t>(a_packet.contains("entityId"));
		const auto revision = ReadUnsigned(a_packet, "revision");
		const auto entity = ReadUnsigned(a_packet, "entityId");
		if (a_packet.size() != 4 + extras || (a_packet.contains("revision") && !revision) || (a_packet.contains("entityId") && !entity)) return std::nullopt;
		result.reason = ReasonFrom(a_packet.at("reason").get<std::string>());
		result.revision = revision;
		result.entityId = entity;
		return result;
	}

	std::optional<std::string> MakeOperationId(std::string_view a_sessionNonce, std::uint32_t a_counter)
	{
		auto operation = std::string{ a_sessionNonce } + ":" + std::to_string(a_counter);
		if (a_sessionNonce.empty() || operation.size() > F4T::Drops::kMaximumOperationIdChars) return std::nullopt;
		return operation;
	}

	std::optional<Json> BuildDropRequest(const std::string& a_operationId, std::uint32_t a_revision, const std::string& a_itemKey, std::uint32_t a_count)
	{
		if (a_operationId.empty() || a_operationId.size() > F4T::Drops::kMaximumOperationIdChars || !ParseItemKey(a_itemKey) ||
			a_count == 0 || a_count > F4T::Drops::kMaximumDropCount) return std::nullopt;
		return Json{ { "type", "inventoryDropRequest" }, { "operationId", a_operationId }, { "expectedRevision", a_revision },
			{ "itemKey", a_itemKey }, { "count", a_count } };
	}

	std::optional<Json> BuildPickupRequest(const std::string& a_operationId, std::uint32_t a_revision, F4T::Drops::EntityId a_entityId)
	{
		if (a_operationId.empty() || a_operationId.size() > F4T::Drops::kMaximumOperationIdChars || a_entityId < F4T::Drops::kFirstDroppedEntityId) return std::nullopt;
		return Json{ { "type", "inventoryPickupRequest" }, { "operationId", a_operationId }, { "expectedRevision", a_revision },
			{ "entityId", a_entityId } };
	}
}
