#include "F4TDropLedger.h"

#include "F4TNetworkValidation.h"
#include "F4TDurableState.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <set>
#include "F4TSnapshotComponent.h"
#include "F4TSnapshotDeltaWire.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

namespace F4T::Drops
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::string> ReadHex(const Json& a_packet, const char* a_field)
		{
			const auto found = a_packet.find(a_field);
			if (found == a_packet.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto text = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(text, false, false)) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
			if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
				return std::nullopt;
			}
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", value);
			return std::string{ buffer };
		}

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_packet, const char* a_field)
		{
			const auto found = a_packet.find(a_field);
			if (found == a_packet.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			const auto value = found->get<std::uint64_t>();
			if (value > 0xFFFFFFFFULL) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<float> ReadCoordinate(const Json& a_packet, const char* a_field)
		{
			const auto found = a_packet.find(a_field);
			if (found == a_packet.end() || !found->is_number()) {
				return std::nullopt;
			}
			const auto value = found->get<double>();
			if (!std::isfinite(value) || std::fabs(value) > 1.0e7) {
				return std::nullopt;
			}
			return static_cast<float>(value);
		}

		std::uint32_t HexValue(const std::string& a_text)
		{
			std::uint32_t value = 0;
			std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
			return value;
		}

		std::string HexText(std::uint32_t a_value)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
			return buffer;
		}

		void SerializeDroppedObject(F4T::Replication::SnapshotStream& a_stream, std::uint32_t& a_item, std::uint32_t& a_count,
			std::uint32_t& a_cell, std::uint32_t& a_worldspace, float& a_x, float& a_y, float& a_z)
		{
			a_stream.U32(a_item);
			a_stream.U32(a_count);
			a_stream.U32(a_cell);
			a_stream.U32(a_worldspace);
			a_stream.F32(a_x);
			a_stream.F32(a_y);
			a_stream.F32(a_z);
		}
	}

	std::vector<std::uint8_t> EncodeDroppedObject(const DroppedObject& a_object)
	{
		auto stream = F4T::Replication::SnapshotStream::Writer();
		auto item = HexValue(a_object.itemForm);
		auto count = a_object.count;
		auto cell = HexValue(a_object.cellId);
		auto worldspace = HexValue(a_object.worldspaceId);
		auto x = a_object.x;
		auto y = a_object.y;
		auto z = a_object.z;
		SerializeDroppedObject(stream, item, count, cell, worldspace, x, y, z);
		const auto written = stream.Written();
		return { written.begin(), written.end() };
	}

	std::optional<DroppedObject> DecodeDroppedObject(std::span<const std::uint8_t> a_bytes)
	{
		auto stream = F4T::Replication::SnapshotStream::Reader(a_bytes);
		std::uint32_t item = 0;
		std::uint32_t count = 0;
		std::uint32_t cell = 0;
		std::uint32_t worldspace = 0;
		DroppedObject object;
		SerializeDroppedObject(stream, item, count, cell, worldspace, object.x, object.y, object.z);
		if (!stream.Ok() || stream.BytesRead() != a_bytes.size() || item == 0 || cell == 0 ||
			count == 0 || count > kMaximumDropCount || !std::isfinite(object.x) || !std::isfinite(object.y) || !std::isfinite(object.z)) {
			return std::nullopt;
		}
		object.itemForm = HexText(item);
		object.count = count;
		object.cellId = HexText(cell);
		object.worldspaceId = worldspace == 0 ? std::string{} : HexText(worldspace);
		return object;
	}

	std::optional<DropRequest> NormalizeDropRequest(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto operation = a_packet.find("operationId");
		if (operation == a_packet.end() || !operation->is_string()) {
			return std::nullopt;
		}
		DropRequest request;
		request.operationId = operation->get<std::string>();
		if (request.operationId.empty() || request.operationId.size() > kMaximumOperationIdChars) {
			return std::nullopt;
		}
		const auto revision = ReadUnsigned(a_packet, "expectedRevision");
		const auto item = ReadHex(a_packet, "itemForm");
		const auto count = ReadUnsigned(a_packet, "count");
		const auto cell = ReadHex(a_packet, "cellId");
		const auto x = ReadCoordinate(a_packet, "x");
		const auto y = ReadCoordinate(a_packet, "y");
		const auto z = ReadCoordinate(a_packet, "z");
		if (!revision || !item || !count || *count == 0 || *count > kMaximumDropCount || !cell || !x || !y || !z) {
			return std::nullopt;
		}
		if (a_packet.contains("worldspaceId")) {
			const auto worldspace = ReadHex(a_packet, "worldspaceId");
			if (!worldspace) {
				return std::nullopt;
			}
			request.worldspaceId = *worldspace;
		}
		request.expectedRevision = *revision;
		request.itemForm = *item;
		request.count = *count;
		request.cellId = *cell;
		request.x = *x;
		request.y = *y;
		request.z = *z;
		return request;
	}

	namespace
	{
		constexpr float MaximumInteractionDistance = 256.0F;

		std::optional<Json> NormalizeOwnedRequest(const Json& a_packet, bool a_drop)
		{
			if (!a_packet.is_object() || a_packet.size() != (a_drop ? 5 : 4)) return std::nullopt;
			const auto operation = a_packet.find("operationId");
			const auto revision = ReadUnsigned(a_packet, "expectedRevision");
			if (operation == a_packet.end() || !operation->is_string() || operation->get_ref<const std::string&>().empty() ||
				operation->get_ref<const std::string&>().size() > kMaximumOperationIdChars || !revision ||
				(!a_packet.contains("type") || !a_packet.at("type").is_string() || a_packet.at("type") != (a_drop ? "inventoryDropRequest" : "inventoryPickupRequest"))) return std::nullopt;
			if (a_drop) {
				const auto key = a_packet.find("itemKey");
				const auto count = ReadUnsigned(a_packet, "count");
				if (key == a_packet.end() || !key->is_string() || key->get_ref<const std::string&>().empty() ||
					key->get_ref<const std::string&>().size() > 137 || !count || *count == 0 || *count > kMaximumDropCount) return std::nullopt;
			} else {
				const auto entity = ReadUnsigned(a_packet, "entityId");
				if (!entity || *entity < kFirstDroppedEntityId) return std::nullopt;
			}
			return a_packet;
		}

		std::optional<Json> Position(const InventoryPosition& a_position)
		{
			Json packet{ { "cellId", a_position.cellId }, { "x", a_position.x }, { "y", a_position.y }, { "z", a_position.z } };
			if (!a_position.worldspaceId.empty()) packet["worldspaceId"] = a_position.worldspaceId;
			const auto cell = ReadHex(packet, "cellId");
			const auto world = a_position.worldspaceId.empty() ? std::optional{ std::string{} } : ReadHex(packet, "worldspaceId");
			if (!cell || !world || !ReadCoordinate(packet, "x") || !ReadCoordinate(packet, "y") || !ReadCoordinate(packet, "z")) return std::nullopt;
			packet["cellId"] = *cell;
			packet["worldspaceId"] = *world;
			return packet;
		}

		Json ResultPacket(const Json& a_request, std::uint32_t a_revision, const Json& a_object, EntityId a_id)
		{
			return Json{ { "type", a_request.at("type") == "inventoryDropRequest" ? "inventoryDropResult" : "inventoryPickupResult" },
				{ "operationId", a_request.at("operationId") }, { "status", "committed" },
				{ "revision", a_revision }, { "entityId", a_id }, { "drop", a_object } };
		}

		TransactionResult Rejected(const Json& a_request, bool a_drop, const char* a_reason, std::uint32_t a_revision = 0)
		{
			Json packet{ { "type", a_drop ? "inventoryDropResult" : "inventoryPickupResult" },
				{ "status", "rejected" }, { "reason", a_reason }, { "revision", a_revision } };
			if (a_request.is_object() && a_request.contains("operationId") && a_request.at("operationId").is_string()) {
				packet["operationId"] = a_request.at("operationId");
			}
			return { Outcome::Rejected, std::move(packet) };
		}

		std::optional<TransactionResult> Screen(const Json& a_root, const std::string& a_profileId, const Json& a_request, bool a_drop)
		{
			if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return Rejected(a_request, a_drop, "inventoryAuthorityUnavailable");
			const auto& profiles = a_root.at("profiles");
			const auto profile = profiles.find(a_profileId);
			if (profile == profiles.end() || !profile->contains("inventory")) return Rejected(a_request, a_drop, "inventoryAuthorityUnavailable");
			const auto revision = profile->at("inventory").at("revision").get<std::uint32_t>();
			for (const auto& operation : profile->at("inventoryOperations")) {
				if (operation.at("request").at("operationId") == a_request.at("operationId")) {
					if (operation.at("request") != a_request) return Rejected(a_request, a_drop, "operationMismatch", revision);
					return TransactionResult{ Outcome::Replayed, operation.at("result") };
				}
			}
			if (a_request.at("expectedRevision") != revision) return Rejected(a_request, a_drop, "staleRevision", revision);
			return std::nullopt;
		}

		TransactionResult Apply(Json& a_root, const std::string& a_profileId, const Json& a_packet,
			const InventoryPosition& a_position, bool a_drop)
		{
			const auto request = NormalizeOwnedRequest(a_packet, a_drop);
			if (!request) return Rejected(a_packet, a_drop, "invalidRequest");
			if (auto screened = Screen(a_root, a_profileId, *request, a_drop)) return *screened;
			auto& profile = a_root.at("profiles").at(a_profileId);
			auto& inventory = profile.at("inventory");
			const auto revision = inventory.at("revision").get<std::uint32_t>();
			auto& history = profile.at("inventoryOperations");
			if (revision == (std::numeric_limits<std::uint32_t>::max)()) return Rejected(*request, a_drop, "capacityExceeded", revision);
			const auto position = Position(a_position);
			if (!position) return Rejected(*request, a_drop, "outOfScope", revision);
			auto& shared = a_root.at("drops");
			auto& live = shared.at("live");
			auto items = inventory.at("items");
			Json object;
			EntityId entityId{};
			if (a_drop) {
				const auto key = request->at("itemKey").get<std::string>();
				const auto item = items.find(key);
				const auto count = request->at("count").get<std::uint32_t>();
				if (item == items.end() || item->at("count").get<std::uint32_t>() < count) return Rejected(*request, true, "insufficientItems", revision);
				std::size_t ownedDrops = 0;
				for (const auto& drop : live) if (drop.at("ownerProfileId") == a_profileId) ++ownedDrops;
				entityId = shared.at("nextEntityId").get<EntityId>();
				if (ownedDrops >= kMaximumLiveDropsPerPlayer || live.size() >= kMaximumLiveDrops || entityId == 0xFFFFFFFFU) return Rejected(*request, true, "capacityExceeded", revision);
				auto descriptor = *item;
				descriptor["count"] = count;
				object = Json{ { "ownerProfileId", a_profileId }, { "itemKey", key }, { "item", descriptor }, { "position", *position } };
				auto candidateLive = live;
				candidateLive[std::to_string(entityId)] = object;
				const auto projection = BuildStableDropSnapshot(candidateLive);
				const auto baseline = projection ? F4T::Replication::EncodeBaseline(*projection, 1, F4T::Replication::Channel::Reliable) : std::nullopt;
				if (!baseline || baseline->size() > kMaximumDropBaselineBytes) return Rejected(*request, true, "capacityExceeded", revision);
				const auto remaining = item->at("count").get<std::uint32_t>() - count;
				if (remaining == 0) items.erase(item); else (*item)["count"] = remaining;
			} else {
				entityId = request->at("entityId").get<EntityId>();
				const auto drop = live.find(std::to_string(entityId));
				if (drop == live.end()) return Rejected(*request, false, "unknownEntity", revision);
				object = *drop;
				const auto& placed = object.at("position");
				double distanceSquared = 0.0;
				for (const char* axis : { "x", "y", "z" }) {
					const auto delta = position->at(axis).get<double>() - placed.at(axis).get<double>();
					distanceSquared += delta * delta;
				}
				if (position->at("cellId") != placed.at("cellId") || position->at("worldspaceId") != placed.at("worldspaceId") ||
					distanceSquared > MaximumInteractionDistance * MaximumInteractionDistance) return Rejected(*request, false, "outOfScope", revision);
				const auto key = object.at("itemKey").get<std::string>();
				const auto count = object.at("item").at("count").get<std::uint32_t>();
				const auto existing = items.find(key);
				const auto owned = existing == items.end() ? 0u : existing->at("count").get<std::uint32_t>();
				if (owned > kMaximumDropCount - count) return Rejected(*request, false, "capacityExceeded", revision);
				items[key] = object.at("item");
				items[key]["count"] = owned + count;
			}
			Json candidateInventory{ { "initialized", true }, { "revision", revision + 1 }, { "items", items } };
			try { F4T::DurableState::ValidateInventory(candidateInventory); }
			catch (const std::invalid_argument&) { return Rejected(*request, a_drop, "capacityExceeded", revision); }
			const auto result = ResultPacket(*request, revision + 1, object, entityId);
			inventory = std::move(candidateInventory);
			if (a_drop) {
				live[std::to_string(entityId)] = object;
				shared["nextEntityId"] = entityId + 1;
			} else live.erase(std::to_string(entityId));
			if (history.size() >= kMaximumOperationHistory) history.erase(history.begin());
			history.push_back(Json{ { "request", *request }, { "result", result } });
			return { Outcome::Committed, result };
		}

		void ValidateObject(const Json& a_object, const Json& a_profiles)
		{
			if (!a_object.is_object() || a_object.size() != 4 || !a_object.contains("ownerProfileId") ||
				!a_object.at("ownerProfileId").is_string() || !a_profiles.contains(a_object.at("ownerProfileId").get<std::string>()) ||
				!a_object.contains("itemKey") || !a_object.at("itemKey").is_string() || !a_object.contains("item") || !a_object.contains("position")) throw std::invalid_argument("Invalid durable drop object.");
			const auto item = F4T::DurableState::NormalizeStartingInventory(Json::array({ a_object.at("item") }));
			if (!item.contains(a_object.at("itemKey").get<std::string>()) || item.at(a_object.at("itemKey").get<std::string>()) != a_object.at("item")) throw std::invalid_argument("Invalid durable drop item identity.");
			const auto& position = a_object.at("position");
			if (!position.is_object() || position.size() != 5) throw std::invalid_argument("Invalid durable drop position.");
			const auto cell = ReadHex(position, "cellId");
			const auto world = position.value("worldspaceId", std::string{});
			if (!cell || *cell != position.at("cellId").get<std::string>() || (!world.empty() && ReadHex(position, "worldspaceId") != world) ||
				!ReadCoordinate(position, "x") || !ReadCoordinate(position, "y") || !ReadCoordinate(position, "z")) throw std::invalid_argument("Invalid durable drop position.");
		}
	}

	std::optional<std::vector<std::uint8_t>> EncodeStableDroppedObject(const StableDroppedObject& a_object)
	{
		try {
			const auto normalized = F4T::DurableState::NormalizeStartingInventory(Json::array({ a_object.item }));
			if (normalized.begin().value() != a_object.item || !Position(a_object.position)) return std::nullopt;
			auto stream = F4T::Replication::SnapshotStream::Writer();
			std::uint8_t version = 1;
			stream.U8(version);
			const auto plugin = a_object.item.at("plugin").get<std::string>();
			auto length = static_cast<std::uint8_t>(plugin.size());
			stream.U8(length);
			for (const auto ch : plugin) {
				auto byte = static_cast<std::uint8_t>(ch);
				stream.U8(byte);
			}
			auto localId = HexValue(a_object.item.at("localFormId").get<std::string>());
			auto count = a_object.item.at("count").get<std::uint16_t>();
			auto cell = HexValue(a_object.position.cellId);
			auto world = HexValue(a_object.position.worldspaceId);
			auto x = a_object.position.x;
			auto y = a_object.position.y;
			auto z = a_object.position.z;
			stream.U32(localId);
			stream.U16(count);
			stream.U32(cell);
			stream.U32(world);
			stream.F32(x);
			stream.F32(y);
			stream.F32(z);
			return std::vector<std::uint8_t>{ stream.Written().begin(), stream.Written().end() };
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	std::optional<StableDroppedObject> DecodeStableDroppedObject(std::span<const std::uint8_t> a_bytes)
	{
		if (a_bytes.size() < 29 || a_bytes.size() > 156) return std::nullopt;
		auto stream = F4T::Replication::SnapshotStream::Reader(a_bytes);
		std::uint8_t version{}, length{};
		stream.U8(version);
		stream.U8(length);
		if (version != 1 || length == 0 || length > 128 || a_bytes.size() != 28U + length) return std::nullopt;
		std::string plugin;
		for (std::uint8_t index = 0; index < length; ++index) {
			std::uint8_t byte{};
			stream.U8(byte);
			plugin.push_back(static_cast<char>(byte));
		}
		std::uint32_t localId{}, cell{}, world{};
		std::uint16_t count{};
		StableDroppedObject object;
		stream.U32(localId);
		stream.U16(count);
		stream.U32(cell);
		stream.U32(world);
		stream.F32(object.position.x);
		stream.F32(object.position.y);
		stream.F32(object.position.z);
		if (!stream.Ok() || stream.BytesRead() != a_bytes.size()) return std::nullopt;
		object.item = Json{ { "plugin", plugin }, { "localFormId", HexText(localId) }, { "count", count } };
		object.position.cellId = HexText(cell);
		object.position.worldspaceId = world == 0 ? std::string{} : HexText(world);
		const auto canonical = EncodeStableDroppedObject(object);
		if (!canonical || !std::equal(canonical->begin(), canonical->end(), a_bytes.begin(), a_bytes.end())) return std::nullopt;
		return object;
	}

	std::optional<F4T::Replication::SnapshotState> BuildStableDropSnapshot(const Json& a_live)
	{
		if (!a_live.is_object() || a_live.size() > kMaximumLiveDrops) return std::nullopt;
		F4T::Replication::SnapshotState state;
		try {
			for (auto drop = a_live.begin(); drop != a_live.end(); ++drop) {
				EntityId entity{};
				const auto& key = drop.key();
				const auto parsed = std::from_chars(key.data(), key.data() + key.size(), entity);
				if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || key != std::to_string(entity) ||
					entity < kFirstDroppedEntityId || entity == 0xFFFFFFFFU) return std::nullopt;
				const auto& position = drop->at("position");
				StableDroppedObject object{ drop->at("item"), { position.at("cellId").get<std::string>(),
					position.at("worldspaceId").get<std::string>(), position.at("x").get<float>(), position.at("y").get<float>(), position.at("z").get<float>() } };
				const auto bytes = EncodeStableDroppedObject(object);
				if (!bytes) return std::nullopt;
				state[{ entity, F4T::Replication::Family::StableDroppedItem }] = *bytes;
			}
		} catch (const std::exception&) {
			return std::nullopt;
		}
		return state;
	}

	TransactionResult DropLedger::Drop(Json& a_root, const std::string& a_profileId, const Json& a_request, const InventoryPosition& a_position)
	{
		return Apply(a_root, a_profileId, a_request, a_position, true);
	}

	TransactionResult DropLedger::Pickup(Json& a_root, const std::string& a_profileId, const Json& a_request, const InventoryPosition& a_position)
	{
		return Apply(a_root, a_profileId, a_request, a_position, false);
	}

	std::optional<TransactionResult> DropLedger::Precheck(const Json& a_root, const std::string& a_profileId, const Json& a_request, bool a_drop)
	{
		const auto request = NormalizeOwnedRequest(a_request, a_drop);
		if (!request) return Rejected(a_request, a_drop, "invalidRequest");
		return Screen(a_root, a_profileId, *request, a_drop);
	}

	void DropLedger::Validate(const Json& a_root)
	{
		const auto sharedIt = a_root.find("drops");
		if (sharedIt == a_root.end()) throw std::invalid_argument("Invalid durable drop ledger.");
		const auto& shared = *sharedIt;
		const auto next = ReadUnsigned(shared, "nextEntityId");
		if (!shared.is_object() || shared.size() != 2 || !next || *next < kFirstDroppedEntityId ||
			!shared.contains("live") || !shared.at("live").is_object() || shared.at("live").size() > kMaximumLiveDrops) throw std::invalid_argument("Invalid durable drop ledger.");
		const auto& profiles = a_root.at("profiles");
		std::map<std::string, std::size_t> ownerCounts;
		std::set<EntityId> recordedDrops;
		std::set<EntityId> recordedPickups;
		std::map<EntityId, Json> recordedObjects;
		for (auto drop = shared.at("live").begin(); drop != shared.at("live").end(); ++drop) {
			EntityId id{};
			const auto& key = drop.key();
			const auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
			if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || key != std::to_string(id) || id < kFirstDroppedEntityId || id >= *next) throw std::invalid_argument("Invalid durable drop entity ID.");
			ValidateObject(drop.value(), profiles);
			const auto owner = drop.value().at("ownerProfileId").get<std::string>();
			if (!profiles.at(owner).contains("inventory") || ++ownerCounts[owner] > kMaximumLiveDropsPerPlayer) throw std::invalid_argument("Invalid durable drop owner.");
		}
		for (auto profileIt = profiles.begin(); profileIt != profiles.end(); ++profileIt) {
			const auto& profile = profileIt.value();
			if (!profile.contains("inventory")) {
				if (profile.contains("inventoryOperations")) throw std::invalid_argument("Inventory history requires inventory.");
				continue;
			}
			const auto& history = profile.at("inventoryOperations");
			const auto revision = profile.at("inventory").at("revision").get<std::uint32_t>();
			if (!history.is_array() || history.size() != (std::min)(static_cast<std::size_t>(revision), kMaximumOperationHistory)) throw std::invalid_argument("Invalid inventory history.");
			std::set<std::string> seen;
			std::optional<std::uint32_t> last;
			for (const auto& operation : history) {
				if (!operation.is_object() || operation.size() != 2) throw std::invalid_argument("Invalid inventory operation.");
				const auto& request = operation.at("request");
				const bool drop = request.value("type", std::string{}) == "inventoryDropRequest";
				if (!NormalizeOwnedRequest(request, drop) || !seen.insert(request.at("operationId").get<std::string>()).second) throw std::invalid_argument("Invalid inventory operation request.");
				const auto& result = operation.at("result");
				const auto resultRevision = ReadUnsigned(result, "revision");
				const auto expectedRevision = request.at("expectedRevision").get<std::uint32_t>();
				const auto entity = ReadUnsigned(result, "entityId");
				if (!resultRevision || expectedRevision == 0xFFFFFFFFU || *resultRevision != expectedRevision + 1 || *resultRevision > revision ||
					(last ? *resultRevision != *last + 1 : *resultRevision != revision - static_cast<std::uint32_t>(history.size()) + 1) || !entity || *entity < kFirstDroppedEntityId || *entity >= *next) throw std::invalid_argument("Invalid inventory operation result.");
				const auto& object = result.at("drop");
				ValidateObject(object, profiles);
				const auto [snapshot, inserted] = recordedObjects.emplace(*entity, object);
				if (!inserted && snapshot->second != object) throw std::invalid_argument("Recorded drop snapshots disagree.");
				if (drop) {
					if (object.at("ownerProfileId").get<std::string>() != profileIt.key() || !recordedDrops.insert(*entity).second) throw std::invalid_argument("Invalid recorded drop ownership.");
					const auto live = shared.at("live").find(std::to_string(*entity));
					if (live != shared.at("live").end() && *live != object) throw std::invalid_argument("Live drop does not match its recorded creation.");
				} else {
					if (!recordedPickups.insert(*entity).second || shared.at("live").contains(std::to_string(*entity))) throw std::invalid_argument("Picked drop remains live or was picked twice.");
				}
				if (result != ResultPacket(request, *resultRevision, object, *entity) ||
					(drop && (object.at("itemKey") != request.at("itemKey") || object.at("item").at("count") != request.at("count"))) ||
					(!drop && *entity != request.at("entityId"))) throw std::invalid_argument("Inventory operation/result mismatch.");
				last = *resultRevision;
			}
			if (last && *last != revision) throw std::invalid_argument("Inventory history revision mismatch.");
		}
		for (auto live = shared.at("live").begin(); live != shared.at("live").end(); ++live) {
			const auto owner = live.value().at("ownerProfileId").get<std::string>();
			if (profiles.at(owner).at("inventory").at("revision").get<std::uint32_t>() <= kMaximumOperationHistory &&
				!recordedDrops.contains(static_cast<EntityId>(std::stoul(live.key())))) throw std::invalid_argument("Live drop has no retained creation.");
		}
	}
}
