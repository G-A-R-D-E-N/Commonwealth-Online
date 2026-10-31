#include "ServerProtocol.h"

#include "F4TDropLedger.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TSnapshotComponent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::DirectComponentPublicationContract;
	using CommonwealthOnline::Server::DirectComponentPublicationPeer;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeTransform(const Json& a_packet, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, Family::Transform, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	std::optional<Json> TryJson(const OutboundFrame& a_frame)
	{
		try {
			return Json::parse(std::string{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() });
		} catch (...) {
			return std::nullopt;
		}
	}

	Json Result(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			const auto packet = frame.connectionId == a_connectionId ? TryJson(frame) : std::nullopt;
			if (packet && packet->value("type", std::string{}) == a_type) {
				assert(frame.delivery == Delivery::ReliableOrdered);
				return *packet;
			}
		}
		assert(false);
		return {};
	}

	struct Harness
	{
		ProtocolHost host{ "Drop Sync Test" };
		std::vector<std::uint32_t> peers;
		std::uint32_t sequence{};
		double clock{ 100.0 };

		Harness()
		{
			F4T::Replication::ComponentRegistry registry;
			assert(registry.RegisterDefinition(Family::Transform, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable));
			assert(registry.RegisterDefinition(Family::DroppedItem, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Reliable));
			assert(host.SetComponentDefinitions(*registry.Freeze()));
		}

		std::uint32_t Join(std::uint32_t a_connectionId, std::string_view a_cell)
		{
			clock += 1.0;
			const auto welcome = host.HandleConnected(a_connectionId, clock);
			const auto playerId = TryJson(welcome.front())->at("playerId").get<std::uint32_t>();
			host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 },
				{ "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1" }) } }), clock, clock);
			peers.push_back(playerId);
			DirectComponentPublicationContract contract;
			for (const auto peer : peers) {
				contract.peers.push_back(DirectComponentPublicationPeer{ peer, true, true, { Family::Transform, Family::DroppedItem } });
			}
			assert(host.SetDirectComponentPublicationContract(std::move(contract)));
			Move(a_connectionId, a_cell, "spawn");
			return playerId;
		}

		void Move(std::uint32_t a_connectionId, std::string_view a_cell, std::string_view a_type = "teleport", double a_x = 0.0)
		{
			clock += 1.0;
			host.HandleMessage(a_connectionId, EncodeTransform(Json{
				{ "type", "transform" }, { "cellId", std::string{ a_cell } }, { "worldspaceId", a_cell == "100" ? "F00D" : "DEAD" },
				{ "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", std::string{ a_type } } }, ++sequence), clock, clock);
		}

		std::vector<OutboundFrame> Send(std::uint32_t a_connectionId, const Json& a_packet)
		{
			clock += 1.0;
			return host.HandleMessage(a_connectionId, EncodeJson(a_packet), clock, clock);
		}
	};

	Json DropRequest(std::string a_operation, std::uint32_t a_revision)
	{
		return Json{ { "type", "inventoryDropRequest" }, { "operationId", std::move(a_operation) }, { "expectedRevision", a_revision },
			{ "itemForm", "1F66A" }, { "count", 3 }, { "cellId", "100" }, { "worldspaceId", "F00D" }, { "x", 5.0 }, { "y", 0.0 }, { "z", 0.0 } };
	}

	Json Pickup(std::string a_operation, std::uint32_t a_entityId)
	{
		return Json{ { "type", "inventoryPickupRequest" }, { "operationId", std::move(a_operation) }, { "entityId", a_entityId } };
	}
}

int main()
{
	Harness harness;
	harness.Join(1, "100");
	harness.Join(2, "100");
	harness.Join(3, "200");

	harness.Send(1, Json{ { "type", "playerState" }, { "equippedItems", Json::array({
		Json{ { "slot", "rightHand" }, { "formId", "1F66A" } } }) }, { "actionEvents", Json::array() } });
	const auto request = DropRequest("unowned-item", 0);
	const auto unowned = Result(harness.Send(1, request), 1, "inventoryDropResult");
	assert(unowned.at("status") == "rejected" && "Source:co-core/src/validation/F4TPlayerStateValidation.cpp; client equipment is not inventory ownership");
	assert(unowned.at("reason") == "inventoryAuthorityUnavailable");
	assert(unowned.at("operationId") == "unowned-item");
	assert(unowned.at("revision") == 0);
	assert(!unowned.contains("entityId"));
	assert(harness.host.DroppedObjectState().empty());
	assert(Result(harness.Send(1, request), 1, "inventoryDropResult") == unowned);

	auto changed = request;
	changed["itemForm"] = "4822";
	changed["count"] = F4T::Drops::kMaximumDropCount;
	changed["expectedRevision"] = 1;
	const auto altered = Result(harness.Send(1, changed), 1, "inventoryDropResult");
	assert(altered == unowned);
	assert(harness.host.DroppedObjectState().empty());

	for (const auto connectionId : { 1U, 2U, 3U }) {
		const auto guessed = Result(harness.Send(connectionId, Pickup("pickup-guess", F4T::Drops::kFirstDroppedEntityId)), connectionId, "inventoryPickupResult");
		assert(guessed.at("status") == "rejected");
		assert(guessed.at("reason") == "inventoryAuthorityUnavailable");
		assert(guessed.at("operationId") == "pickup-guess");
		assert(guessed.at("entityId") == F4T::Drops::kFirstDroppedEntityId);
		assert(!guessed.contains("count") && !guessed.contains("itemForm"));
		assert(Result(harness.Send(connectionId, Pickup("pickup-guess", F4T::Drops::kFirstDroppedEntityId)), connectionId, "inventoryPickupResult") == guessed);
	}

	harness.Join(4, "100");
	assert(harness.host.DroppedObjectState().empty());
	harness.host.HandleDisconnected(1, harness.clock);
	harness.Join(1, "100");
	assert(Result(harness.Send(1, request), 1, "inventoryDropResult") == unowned);
	assert(harness.host.DroppedObjectState().empty());

	const auto before = harness.host.Stats().packetsRejected;
	assert(harness.Send(1, Json{ { "type", "inventoryDropRequest" }, { "operationId", "x" } }).empty());
	assert(harness.Send(1, Json{ { "type", "inventoryPickupRequest" }, { "operationId", "" }, { "entityId", 1 } }).empty());
	assert(harness.host.Stats().packetsRejected == before + 2);
	return 0;
}
