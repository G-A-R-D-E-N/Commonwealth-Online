#include "F4TIdentityProtocol.h"
#include "F4TVoiceWire.h"
#include "IdentityGate.h"
#include "OpenSslTokenService.h"
#include "PersistenceGate.h"
#include "F4TInventoryContract.h"
#include "F4TMessageFragments.h"
#include "F4TPayloadCodec.h"
#include "co_gns_server_bridge.h"
#include "ServerDeferredFrames.h"
#include "ServerDeferredRuntime.h"
#include "ServerLogSanitizer.h"
#include "ServerReadySurvivalPolicy.h"
#include "StartingGrantFixture.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
	using Json = nlohmann::json;
	using namespace CommonwealthOnline::Server;
	std::vector<Json> sent;
	int sendResult = CO_GNS_SEND_SENT;
	int sendCount = 0;
	int failSendIndex = -1;
	std::uint32_t lastSendBytes{};
	DurableStateFileStore* inspectedStore{};

	std::vector<std::uint8_t> JsonBytes(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::optional<std::string> RawPacketType(std::span<const std::uint8_t> a_payload)
	{
		const auto packet = Json::parse(a_payload.begin(), a_payload.end(), nullptr, false);
		return packet.is_object() && packet.contains("type") && packet.at("type").is_string() ?
			std::optional{ packet.at("type").get<std::string>() } : std::nullopt;
	}

	bool SendReliableJson(co_gns_server_handle a_server, std::uint32_t a_connectionId, const Json& a_packet)
	{
		const auto bytes = JsonBytes(a_packet);
		return co_gns_server_send(a_server, a_connectionId, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 1) == CO_GNS_SEND_SENT;
	}

	%:include "../src/ServerFrameDelivery.inc"
	std::vector<OutboundFrame> ReliableTo(std::vector<OutboundFrame> a_frames, std::uint32_t a_connection = 17)
	{
		std::erase_if(a_frames, [a_connection](const auto& frame) { return frame.connectionId != a_connection || frame.delivery != Delivery::ReliableOrdered || RawPacketType(frame.payload); });
		return a_frames;
	}

	F4T::Replication::DecodedFrame ReadComponent(const OutboundFrame& a_frame)
	{
		F4T::Replication::DecodedFrame decoded;
		assert(F4T::Replication::DecodeFrame(a_frame.payload, decoded, [](auto) { return true; }) == F4T::Replication::DecodeStatus::Valid);
		return decoded;
	}

	void AcceptComponent(F4T::Replication::SnapshotReceiver& a_receiver, const OutboundFrame& a_frame)
	{
		const auto decoded = ReadComponent(a_frame);
		if (decoded.kind == F4T::Replication::FrameKind::Baseline) assert(a_receiver.AcceptBaseline(decoded.baseline, decoded.snapshotSequence));
		else assert(a_receiver.AcceptDelta(decoded.delta, decoded.snapshotSequence, decoded.baselineSequence));
	}

	void TestComponentSendRecovery()
	{
		using namespace F4T::Replication;
		ProtocolHost host;
		ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::DroppedItem, AuthorityClass::Server, Channel::Reliable));
		assert(registry.RegisterDefinition(Family::Transform, AuthorityClass::Server, Channel::Unreliable));
		assert(host.SetComponentDefinitions(*registry.Freeze()));
		std::vector<std::uint32_t> players;
		for (std::uint32_t connection : { 17u, 18u }) {
			const auto welcome = host.HandleConnected(connection, 1000.0);
			players.push_back(Json::parse(welcome.front().payload).at("playerId").get<std::uint32_t>());
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2 },
				{ "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1" }) } }), 1.0, 1000.0);
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", "CAFE" },
				{ "worldspaceId", "F00D" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "spawn" } }), 2.0, 1000.0);
		}
		DirectComponentPublicationContract contract;
		for (const auto player : players) contract.peers.push_back({ player, true, true, { Family::DroppedItem, Family::Transform } });
		assert(host.SetDirectComponentPublicationContract(std::move(contract)));
		SnapshotState state;
		state[{ players.at(1), Family::DroppedItem }] = { 1 };
		auto frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && frames.front().connectionId == 17);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 3.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		auto retry = ReliableTo(host.PublishComponentSnapshot(state));
		assert(retry.size() == 1);
		assert(ReadComponent(retry.front()).kind == FrameKind::Baseline);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 3.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		SnapshotReceiver receiver;
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 2 };
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && ReadComponent(frames.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 4.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline == state);
		SendFrames(nullptr, host, retry, nullptr, nullptr, 4.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		const auto failedSequence = ReadComponent(retry.front()).snapshotSequence;
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && F4T::GnsSnapshotEnvelope::IsNewer(ReadComponent(retry.front()).snapshotSequence, failedSequence));
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 5.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 5 };
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 10.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		const auto moveRecipient = [&](std::string_view a_cell, double a_time) {
			(void)host.HandleMessage(17, JsonBytes(Json{ { "type", "transform" }, { "cellId", a_cell },
				{ "worldspaceId", a_cell == "CAFE" ? "F00D" : "DEAD" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "teleport" } }), a_time, 1000.0);
		};
		moveRecipient("BEEF", 20.0);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline.empty());
		SendFrames(nullptr, host, retry, nullptr, nullptr, 20.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		moveRecipient("CAFE", 30.0);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline == state);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 30.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state.clear();
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && ReadComponent(frames.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 6.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).kind == FrameKind::Baseline && ReadComponent(retry.front()).baseline.empty());
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 6.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView().empty() && host.RetryComponentSnapshots().empty());
		frames = { OutboundFrame{ 17, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "worldState" } }) } };
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 7.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		assert(host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 3 };
		state[{ players.at(1), Family::Transform }] = { 4 };
		auto combined = host.PublishComponentSnapshot(state);
		frames = ReliableTo(combined);
		SendFrames(nullptr, host, frames, nullptr, nullptr, 8.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		std::erase_if(combined, [](const auto& frame) { return frame.connectionId != 17 || frame.delivery != Delivery::UnreliableSequenced; });
		assert(combined.size() == 1);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, combined, nullptr, nullptr, 8.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && retry.front().connectionId == 17 && retry.front().delivery == Delivery::ReliableOrdered);
		(void)host.HandleDisconnected(17, 1001.0);
		assert(host.RetryComponentSnapshots().empty());
	}

	void TestCompatibilitySendRecovery()
	{
		using namespace F4T::Replication;
		using namespace F4T::Compatibility;
		ProtocolHost host;
		ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::DroppedItem, AuthorityClass::Server, Channel::Reliable));
		assert(host.SetComponentDefinitions(*registry.Freeze()));
		FamilyAdapter adapter;
		adapter.metadata = { { "testState" }, { Family::DroppedItem }, AllConversionDirections, "component-test-state-v1" };
		adapter.legacyToCanonical = [](std::string_view a_text) -> std::optional<CanonicalValue> {
			const auto value = Json::parse(a_text, nullptr, false);
			if (!value.is_number_unsigned() || value.get<std::uint64_t>() > 255) return std::nullopt;
			return CanonicalValue{ value.get<std::uint8_t>() };
		};
		adapter.canonicalToLegacy = [](const CanonicalValue& a_value) -> std::optional<std::string> {
			const auto value = std::any_cast<std::uint8_t>(&a_value);
			return value ? std::optional{ std::to_string(*value) } : std::nullopt;
		};
		adapter.canonicalToComponent = [](const CanonicalValue& a_value) -> std::optional<std::vector<std::uint8_t>> {
			const auto value = std::any_cast<std::uint8_t>(&a_value);
			return value ? std::optional{ std::vector<std::uint8_t>{ *value } } : std::nullopt;
		};
		adapter.componentToCanonical = [](std::span<const std::uint8_t> a_bytes) -> std::optional<CanonicalValue> {
			return a_bytes.size() == 1 ? std::optional{ CanonicalValue{ a_bytes.front() } } : std::nullopt;
		};
		assert(host.RegisterCompatibilityAdapter(std::move(adapter)));
		std::vector<std::uint32_t> players;
		for (std::uint32_t connection : { 27u, 28u }) {
			const auto welcome = host.HandleConnected(connection, 1000.0);
			players.push_back(Json::parse(welcome.front().payload).at("playerId").get<std::uint32_t>());
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2 },
				{ "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1", "component-test-state-v1" }) } }), 1.0, 1000.0);
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", "CAFE" },
				{ "worldspaceId", "F00D" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "spawn" } }), 2.0, 1000.0);
		}
		CompatiblePublication publication{ players.at(1), Family::DroppedItem, "testState", 1,
			CanonicalValue{ std::uint8_t{ 9 } }, { players.front() }, {} };
		auto result = host.PublishCompatible(publication);
		assert(result.status == CompatibilityPublishStatus::Accepted && result.frames.size() == 1);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, result.frames, nullptr, nullptr, 3.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		auto retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && retry.front().connectionId == 27 && ReadComponent(retry.front()).kind == FrameKind::Baseline);
		SnapshotState expected;
		expected[{ players.at(1), Family::DroppedItem }] = { 9 };
		assert(ReadComponent(retry.front()).baseline == expected);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 3.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		SnapshotReceiver receiver;
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == expected && host.RetryComponentSnapshots().empty());
		auto removal = host.RemoveCompatibilityEntity(players.at(1));
		assert(removal.size() == 1 && ReadComponent(removal.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, removal, nullptr, nullptr, 4.0, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline.empty());
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 4.5, 1000.0, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView().empty() && host.RetryComponentSnapshots().empty());
	}
	void TestOwnedInventoryProtocol()
	{
		using namespace F4T::Drops;
		using namespace F4T::Replication;
		const auto directory = std::filesystem::temp_directory_path() / ("co-owned-protocol-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto path = directory / "state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		try {
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
			inspectedStore = &store;
			OpenSslTokenService tokens;
			IdentityGate identity({}, tokens, &store);
			PersistenceGate persistence(store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5u } } }));
			ProtocolHost host;
			assert(host.EnableInventoryTransactions(persistence));
			assert(!host.EnableInventoryTransactions(persistence));
			ComponentRegistry otherDefinitions;
			assert(otherDefinitions.RegisterDefinition(Family::Transform, AuthorityClass::Server, Channel::Unreliable));
			assert(!host.SetComponentDefinitions(*otherDefinitions.Freeze()));
			assert(!host.SetDirectComponentPublicationContract({}));
			const auto move = [&](std::uint32_t a_connection, double a_time, const char* a_cell = "CAFE", const char* a_world = "F00D", double a_x = 1.0, const char* a_type = "teleport") {
				return host.HandleMessage(a_connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", a_cell }, { "worldspaceId", a_world },
					{ "x", a_x }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", a_type } }), a_time, 1000.0);
			};
			std::uint32_t joinedPlayerId{};
		const auto join = [&](std::uint32_t a_connection, bool a_stable, bool a_components = true, bool a_channels = true, bool a_trade = false) {
				const auto welcome = host.HandleConnected(a_connection, 1000.0);
				const auto welcomeCapabilities = Json::parse(welcome.front().payload).at("capabilities").get<std::vector<std::string>>();
				assert(std::find(welcomeCapabilities.begin(), welcomeCapabilities.end(), "inventory-stable-v1") != welcomeCapabilities.end());
					joinedPlayerId = Json::parse(welcome.front().payload).at("playerId").get<std::uint32_t>();
				auto caps = Json::array();
				caps.push_back("save-attestation-v1");
				if (a_components) caps.push_back("component-snapshot-v1");
				if (a_channels) caps.push_back("component-snapshot-channels-v1");
				if (a_trade) caps.push_back("player-trade-v1");
					if (a_stable) caps.push_back("inventory-stable-v1");
				const auto hello = JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2u }, { "clientVersion", "1.2.0" },
					{ "falloutRuntimeVersion", "1.11.240" }, { "profileId", "profile-" + std::to_string(a_connection) },
					{ "contentManifestFingerprint", std::string(64, 'a') }, { "capabilities", caps } });
				assert(identity.InspectInbound(a_connection, hello, 1000.0).forward);
				return host.HandleMessage(a_connection, hello, 1.0, 1000.0);
			};
			const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", "first" }, { "expectedRevision", 0u }, { "itemKey", "test.esm:00000123" }, { "count", 2u } };
			const auto request = [&](std::uint32_t a_connection, const Json& a_packet, double a_time) { return host.HandleMessage(a_connection, JsonBytes(a_packet), a_time, 1000.0); };
			const auto result = [](const auto& a_frames) {
				assert(!a_frames.empty());
				return Json::parse(a_frames.front().payload);
			};
			const auto reconcile = [&](std::uint32_t a_connection) {
				const auto profile = "profile-" + std::to_string(a_connection);
				const auto save = ReconcileStartingGrantFixture(store, profile);
				assert(persistence.VerifyStartingGrantSave(a_connection, save));
			};
			auto ready = join(11, true);
			(void)move(11, 2.0, "CAFE", "F00D", 1, "spawn");
			assert(result(request(11, drop, 2.1)).at("reason") == "inventoryAuthorityUnavailable");
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, ready, &identity, &persistence, 2.2, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			assert(result(request(11, drop, 2.3)).at("reason") == "inventoryAuthorityUnavailable");
			assert(store.GetInventory("profile-11")->at("revision") == 0);
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, ready, &identity, &persistence, 2.4, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(11);
			for (const auto connection : { 12u, 13u, 14u }) {
				ready = join(connection, connection != 13);
				SendFrames(nullptr, host, ready, &identity, &persistence, 2.5, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
				if (connection == 12) reconcile(connection);
				(void)move(connection, 2.6, connection == 14 ? "BEEF" : "CAFE", connection == 14 ? "DEAD" : "F00D", 1, "spawn");
			}
			assert(result(request(13, drop, 2.7)).at("reason") == "inventoryAuthorityUnavailable");
			for (const auto connection : { 16u, 17u }) {
				ready = join(connection, true, connection != 17, connection != 16);
				SendFrames(nullptr, host, ready, &identity, &persistence, 2.7, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
				(void)move(connection, 2.7, "CAFE", "F00D", 1, "spawn");
				assert(result(request(connection, drop, 2.7)).at("reason") == "inventoryAuthorityUnavailable");
				assert(store.GetInventory("profile-" + std::to_string(connection))->at("revision") == 0);
			}
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			assert(result(request(11, drop, 2.8)).at("reason") == "inventoryPersistenceUnavailable");
			assert(store.GetInventory("profile-11")->at("revision") == 0 && store.GetDropSnapshot()->empty());
			std::filesystem::remove_all(backup);
			const auto projectionsBeforeDrop = host.Stats().inventoryProjections;
			auto frames = request(11, drop, 3.0);
			const auto committed = result(frames);
			assert(committed.at("status") == "committed" && committed.at("revision") == 1);
			assert(host.Stats().inventoryProjections == projectionsBeforeDrop + 1);
			assert(store.GetInventory("profile-11")->at("items").at("test.esm:00000123").at("count") == 3);
			assert(ReliableTo(frames, 12).size() == 1 && ReliableTo(frames, 13).empty() && ReliableTo(frames, 14).empty());
			SnapshotReceiver receiver;
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 3.1, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			frames = host.RetryComponentSnapshots();
			assert(frames.size() == 2);
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 3.2, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			AcceptComponent(receiver, ReliableTo(frames, 12).front());
			assert(receiver.CurrentView() == *store.GetDropSnapshot());
			frames = move(12, 4.0, "BEEF", "DEAD");
			assert(ReliableTo(frames, 12).size() == 1);
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 4.1, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			frames = host.RetryComponentSnapshots();
			assert(ReadComponent(ReliableTo(frames, 12).front()).baseline.empty());
			SendFrames(nullptr, host, frames, nullptr, &persistence, 4.2, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			frames = move(12, 5.0);
			const auto visibleAgain = ReliableTo(frames, 12);
			assert(visibleAgain.size() == 1 && ReadComponent(visibleAgain.front()).baseline == *store.GetDropSnapshot());
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 5.1, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			AcceptComponent(receiver, visibleAgain.front());
			assert(receiver.CurrentView() == *store.GetDropSnapshot());
			assert(result(request(11, drop, 20.0)) == committed);
			auto second = drop;
			second["operationId"] = "second";
			second["expectedRevision"] = 1u;
			assert(result(request(11, second, 20.1)).at("reason") == "outOfScope");
			const auto rejectedBefore = host.Stats().movementRejected;
			(void)move(11, 20.2, "CAFE", "F00D", 9999999.0, "normal");
			assert(host.Stats().movementRejected == rejectedBefore + 1);
			assert(result(request(11, second, 20.3)).at("reason") == "outOfScope");
			(void)move(11, 21.0);
			auto forged = second;
			forged["profileId"] = "profile-12";
			assert(result(request(11, forged, 21.1)).at("reason") == "invalidRequest");
			(void)host.HandleDisconnected(11, 1001);
			persistence.ConnectionClosed(11);
			assert(store.GetDropSnapshot()->size() == 1);
			ready = join(15, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 22.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			frames = move(15, 22.1, "CAFE", "F00D", 1, "spawn");
			const auto late = ReliableTo(frames, 15);
			assert(late.size() == 1 && ReadComponent(late.front()).baseline == *store.GetDropSnapshot());
			(void)move(12, 23.0);
			const Json pickup{ { "type", "inventoryPickupRequest" }, { "operationId", "pickup" }, { "expectedRevision", 0u }, { "entityId", committed.at("entityId") } };
			frames = request(12, pickup, 23.1);
			assert(result(frames).at("status") == "committed");
			assert(store.GetDropSnapshot()->empty() && store.GetInventory("profile-12")->at("items").at("test.esm:00000123").at("count") == 7);
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 23.2, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			frames = host.RetryComponentSnapshots();
			const auto removal = ReliableTo(frames, 12);
			assert(removal.size() == 1 && ReadComponent(removal.front()).baseline.empty());
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 23.3, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			AcceptComponent(receiver, removal.front());
			assert(receiver.CurrentView().empty());
			const auto projectionsBeforeIdle = host.Stats().inventoryProjections;
			(void)move(15, 24.0, "CAFE", "F00D", 1, "spawn");
			assert(host.Stats().inventoryProjections == projectionsBeforeIdle);
			const Json stale{ { "type", "inventoryDropRequest" }, { "operationId", "flood" }, { "expectedRevision", 99u }, { "itemKey", "test.esm:00000123" }, { "count", 1u } };
			const auto projectionsBeforeFlood = host.Stats().inventoryProjections;
			for (int burst = 0; burst < 4; ++burst) assert(result(request(12, stale, 30.0)).at("reason") == "staleRevision");
			const auto limited = result(request(12, stale, 30.0));
			assert(limited.at("status") == "rejected" && limited.at("reason") == "rateLimited" && limited.at("operationId") == "flood");
			assert(result(request(12, stale, 31.0)).at("reason") == "staleRevision");
			assert(host.Stats().inventoryProjections == projectionsBeforeFlood);
			const Json container{ { "type", "inventoryContainerRequest" }, { "operationId", "container" }, { "expectedRevision", 0u },
				{ "direction", "take" }, { "container", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
				{ "itemKey", "test.esm:00000123" }, { "count", 1u } };
			const Json trade{ { "type", "inventoryTradeRequest" }, { "operationId", "trade" }, { "expectedRevision", 0u },
				{ "vendor", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
				{ "sell", Json::array({ Json{ { "itemKey", "test.esm:00000123" }, { "count", 1u } } }) }, { "buy", Json::array() } };
			ready = join(18, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 40.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(18);
			(void)move(18, 40.1, "CAFE", "F00D", 1, "spawn");
			const auto containerRejected = result(request(18, container, 40.2));
			assert(containerRejected.at("status") == "rejected" && containerRejected.at("reason") == "insufficientItems" && containerRejected.at("operationId") == "container");
			assert(containerRejected.at("revision") == 0u && containerRejected.at("direction") == "take" && containerRejected.at("itemKey") == "test.esm:00000123" && containerRejected.at("count") == 1u);
			const auto tradeRejected = result(request(18, trade, 40.3));
			assert(tradeRejected.at("status") == "rejected" && tradeRejected.at("reason") == "inventoryAuthorityUnavailable" && tradeRejected.at("operationId") == "trade");
			auto padded = container;
			padded["unexpected"] = 1u;
			const auto paddedBefore = host.Stats().packetsRejected;
			assert(request(18, padded, 40.4).empty());
			assert(host.Stats().packetsRejected == paddedBefore + 1);
			auto runtimeRef = container;
			runtimeRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "FF001234" } };
			const auto malformedBefore = host.Stats().packetsRejected;
			assert(request(18, runtimeRef, 40.5).empty());
			assert(host.Stats().packetsRejected == malformedBefore + 1);
			auto putRequest = container;
			putRequest["operationId"] = "put";
			putRequest["direction"] = "put";
			const auto putResult = result(request(18, putRequest, 41.0));
			assert(putResult.at("status") == "committed" && putResult.at("type") == "inventoryContainerResult" && putResult.at("revision") == 1u && putResult.at("containerRevision") == 1u && putResult.at("direction") == "put");
			assert(store.GetInventory("profile-18")->at("items").at("test.esm:00000123").at("count") == 4);
			auto takeRequest = container;
			takeRequest["operationId"] = "take";
			takeRequest["expectedRevision"] = 1u;
			const auto takeResult = result(request(18, takeRequest, 41.1));
			assert(takeResult.at("status") == "committed" && takeResult.at("revision") == 2u && takeResult.at("containerRevision") == 2u);
			assert(store.GetInventory("profile-18")->at("items").at("test.esm:00000123").at("count") == 5);
			assert(result(request(18, takeRequest, 41.2)) == takeResult);
			auto mismatchRequest = takeRequest;
			mismatchRequest["count"] = 2u;
			const auto mismatchResult = result(request(18, mismatchRequest, 41.3));
			assert(mismatchResult.at("reason") == "operationMismatch" && mismatchResult.at("revision") == 2u);
			auto staleRequest = container;
			staleRequest["operationId"] = "stale";
			const auto staleResult = result(request(18, staleRequest, 42.0));
			assert(staleResult.at("reason") == "staleRevision" && staleResult.at("revision") == 2u);
			auto takeEmpty = takeRequest;
			takeEmpty["operationId"] = "take-empty";
			takeEmpty["expectedRevision"] = 2u;
			assert(result(request(18, takeEmpty, 42.1)).at("reason") == "insufficientItems");
			const auto refuseContainer = host.Events().Subscribe(ServerEventKind::InventoryChange, [](const ServerEvent&) { return false; });
			auto vetoPutRequest = putRequest;
			vetoPutRequest["operationId"] = "veto-put";
			vetoPutRequest["expectedRevision"] = 2u;
			const auto rejectedBeforeContainerVeto = host.Stats().packetsRejected;
			const auto vetoedResult = result(request(18, vetoPutRequest, 43.0));
			assert(vetoedResult.at("status") == "rejected" && vetoedResult.at("reason") == "vetoed" && vetoedResult.at("operationId") == "veto-put" && vetoedResult.at("revision") == 2u);
			assert(host.Stats().packetsRejected == rejectedBeforeContainerVeto + 1);
			assert(store.GetInventory("profile-18")->at("items").at("test.esm:00000123").at("count") == 5);
			assert(host.Events().Unsubscribe(refuseContainer));
			for (int burst = 0; burst < 4; ++burst) {
				auto burstRequest = takeEmpty;
				burstRequest["operationId"] = "burst-" + std::to_string(burst);
				assert(result(request(18, burstRequest, 44.0)).at("reason") == "insufficientItems");
			}
			auto limitedRequest = takeEmpty;
			limitedRequest["operationId"] = "burst-limited";
			const auto limitedResult = result(request(18, limitedRequest, 44.0));
			assert(limitedResult.at("status") == "rejected" && limitedResult.at("reason") == "rateLimited" && limitedResult.at("operationId") == "burst-limited" && !limitedResult.contains("revision"));
			auto dropAfterContainer = drop;
			dropAfterContainer["operationId"] = "drop-after-container";
			assert(result(request(18, dropAfterContainer, 44.0)).at("reason") == "rateLimited");
			ready = join(19, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 42.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(19);
			(void)move(19, 42.0, "CAFE", "F00D", 1, "spawn");
			for (int burst = 0; burst < 4; ++burst) {
				auto flood = stale;
				flood["operationId"] = "shared-" + std::to_string(burst);
				assert(result(request(19, flood, 42.0)).at("reason") == "staleRevision");
			}
			auto containerAfterDrop = container;
			containerAfterDrop["operationId"] = "container-after-drop";
			assert(result(request(19, containerAfterDrop, 42.0)).at("reason") == "rateLimited");
			ready = join(20, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 43.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			(void)move(20, 43.1, "CAFE", "F00D", 1, "spawn");
			const auto refuse = host.Events().Subscribe(ServerEventKind::InventoryChange, [](const ServerEvent&) { return false; });
			const auto snapshotBeforeVeto = *store.GetDropSnapshot();
			const auto rejectedBeforeVeto = host.Stats().packetsRejected;
			auto vetoDrop = drop;
			vetoDrop["operationId"] = "veto-drop";
			const auto vetoResult = result(request(20, vetoDrop, 43.2));
			assert(vetoResult.at("status") == "rejected" && vetoResult.at("reason") == "vetoed" && vetoResult.at("operationId") == "veto-drop");
			assert(vetoResult.contains("revision"));
			assert(host.Stats().packetsRejected == rejectedBeforeVeto + 1);
			assert(*store.GetDropSnapshot() == snapshotBeforeVeto);
			assert(host.Events().Unsubscribe(refuse));
			const auto snapshotPart = [](const char* a_operation, std::uint32_t a_part, std::uint32_t a_parts, const Json& a_items) {
				return Json{ { "type", "inventorySnapshotRequest" }, { "operationId", a_operation }, { "baseRevision", 0u },
					{ "part", a_part }, { "parts", a_parts }, { "items", a_items } };
			};
			const Json testItem{ { "plugin", "test.esm" }, { "localFormId", "00000123" }, { "count", 3u } };
			const Json otherItem{ { "plugin", "other.esm" }, { "localFormId", "00000042" }, { "count", 1u } };
			const auto ack = [&](std::uint32_t a_connection, const std::string& a_profile) {
				return request(a_connection, Json{ { "type", "startingInventoryGrantAck" }, { "profileId", a_profile }, { "grantId", "starting-inventory" } }, 44.1);
			};
			const auto openSnapshotConnection = [&](std::uint32_t a_connection) {
				const auto profile = "profile-" + std::to_string(a_connection);
				ready = join(a_connection, true);
				SendFrames(nullptr, host, ready, &identity, &persistence, 44.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
				assert(ack(a_connection, "profile-other").empty() && store.PendingStartingGrant(profile));
				assert(ack(a_connection, profile).empty() && store.PendingStartingGrant(profile));
				const auto saved = ReconcileStartingGrantFixture(store, profile);
				assert(persistence.VerifyStartingGrantSave(a_connection, saved));
			};
			const auto revisionOf = [&](std::uint32_t a_connection) { return store.GetInventory("profile-" + std::to_string(a_connection))->at("revision"); };
			openSnapshotConnection(22);
			assert(request(22, snapshotPart("ooo", 0, 3, Json::array({ testItem })), 45.0).empty());
			assert(result(request(22, snapshotPart("ooo", 2, 3, Json::array({ testItem })), 45.0)).at("reason") == "invalidRequest");
			assert(result(request(22, snapshotPart("ooo", 1, 3, Json::array({ testItem })), 45.0)).at("reason") == "invalidRequest");
			assert(revisionOf(22) == 0);
			openSnapshotConnection(23);
			assert(result(request(23, snapshotPart("missing-zero", 1, 2, Json::array({ testItem })), 45.0)).at("reason") == "invalidRequest");
			assert(revisionOf(23) == 0);
			openSnapshotConnection(24);
			assert(request(24, snapshotPart("dup", 0, 3, Json::array({ testItem })), 45.0).empty());
			assert(request(24, snapshotPart("dup", 1, 3, Json::array({ testItem })), 45.0).empty());
			assert(result(request(24, snapshotPart("dup", 1, 3, Json::array({ testItem })), 45.0)).at("reason") == "invalidRequest");
			assert(revisionOf(24) == 0);
			openSnapshotConnection(25);
			assert(request(25, snapshotPart("mix-a", 0, 2, Json::array({ testItem })), 45.0).empty());
			assert(result(request(25, snapshotPart("mix-b", 1, 2, Json::array({ otherItem })), 45.0)).at("reason") == "invalidRequest");
			assert(revisionOf(25) == 0);
			openSnapshotConnection(26);
			for (int burst = 0; burst < 4; ++burst) {
				assert(request(26, snapshotPart(("rl-" + std::to_string(burst)).c_str(), 0, 3, Json::array({ testItem })), 45.0).empty());
			}
			const auto limitedPart = result(request(26, snapshotPart("rl-4", 0, 3, Json::array({ testItem })), 45.0));
			assert(limitedPart.at("status") == "rejected" && limitedPart.at("reason") == "rateLimited" && limitedPart.at("operationId") == "rl-4");
			assert(result(request(26, snapshotPart("rl-3", 1, 3, Json::array({ testItem })), 45.0)).at("reason") == "invalidRequest");
			assert(revisionOf(26) == 0);
			openSnapshotConnection(27);
			for (int drain = 0; drain < 3; ++drain) {
				assert(request(27, snapshotPart(("drain-" + std::to_string(drain)).c_str(), 0, 3, Json::array({ testItem })), 46.0).empty());
			}
			assert(request(27, snapshotPart("mid", 0, 3, Json::array({ testItem })), 46.0).empty());
			assert(request(27, snapshotPart("mid", 1, 3, Json::array({ otherItem })), 46.0).empty());
			const auto midCommitted = result(request(27, snapshotPart("mid", 2, 3, Json::array({ Json{ { "plugin", "third.esm" }, { "localFormId", "00000043" }, { "count", 1u } } })), 46.0));
			assert(midCommitted.at("status") == "committed" && midCommitted.at("revision") == 1);
			openSnapshotConnection(28);
			assert(request(28, snapshotPart("good", 0, 2, Json::array({ testItem })), 47.0).empty());
			const auto goodCommitted = result(request(28, snapshotPart("good", 1, 2, Json::array({ otherItem })), 47.0));
			assert(goodCommitted.at("status") == "committed" && goodCommitted.at("revision") == 1);
			assert(store.GetInventory("profile-28")->at("items").size() == 2);
			ready = join(29, true, true, true, true);
			const auto alice = joinedPlayerId;
			SendFrames(nullptr, host, ready, &identity, &persistence, 49.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(29);
			ready = join(30, true, true, true, true);
			const auto bob = joinedPlayerId;
			SendFrames(nullptr, host, ready, &identity, &persistence, 49.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(30);
			(void)move(29, 49.5, "CAFE", "F00D", 1, "spawn");
			(void)move(30, 49.5, "CAFE", "F00D", 1, "spawn");
			const auto packetFor = [](const auto& a_frames, std::uint32_t a_connection) {
				for (const auto& frame : a_frames) {
					if (frame.connectionId != a_connection || frame.delivery != Delivery::ReliableOrdered) continue;
					auto packet = Json::parse(frame.payload, nullptr, false);
					if (packet.is_object() && packet.value("type", std::string{}).starts_with("trade")) return packet;
				}
				assert(false && "no trade packet for connection");
				return Json{};
			};
			const auto offerOf = [](const std::string& a_tradeId, std::uint32_t a_count) {
				return Json{ { "type", "tradeOffer" }, { "tradeId", a_tradeId }, { "lines", Json::array({ Json{ { "itemKey", "test.esm:00000123" }, { "count", a_count } } }) } };
			};
			assert(packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", alice } }, 49.8), 29).at("status") == "rejected");
			const auto opened = request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", bob } }, 50.0);
			const auto tradeId = packetFor(opened, 29).at("tradeId").get<std::string>();
			assert(packetFor(opened, 29).at("phase") == "requested" && packetFor(opened, 29).at("role") == "initiator");
			assert(packetFor(opened, 30).at("role") == "target" && packetFor(opened, 30).at("partnerPlayerId") == alice);
			assert(packetFor(request(30, Json{ { "type", "tradeRespond" }, { "tradeId", tradeId }, { "accept", true } }, 50.5), 30).at("phase") == "negotiating");
			assert(packetFor(request(29, offerOf(tradeId, 2), 51.0), 29).at("ownOffer").size() == 1);
			assert(packetFor(request(30, offerOf(tradeId, 3), 51.5), 30).at("partnerOffer").size() == 1);
			assert(packetFor(request(29, Json{ { "type", "tradeConfirm" }, { "tradeId", tradeId }, { "offerVersion", 1u } }, 51.8), 29).at("reason") == "offerChanged");
			assert(packetFor(request(29, Json{ { "type", "tradeConfirm" }, { "tradeId", tradeId }, { "offerVersion", 2u } }, 52.0), 29).at("ownConfirmed") == true);
			const auto settled = request(30, Json{ { "type", "tradeConfirm" }, { "tradeId", tradeId }, { "offerVersion", 2u } }, 52.5);
			assert(packetFor(settled, 29).at("status") == "committed" && packetFor(settled, 30).at("status") == "committed");
			assert(store.GetInventory("profile-29")->at("items").at("test.esm:00000123").at("count") == 6);
			assert(store.GetInventory("profile-30")->at("items").at("test.esm:00000123").at("count") == 4);
			assert(store.GetInventory("profile-29")->at("revision") == 1 && store.GetInventory("profile-30")->at("revision") == 1);
			assert(result(request(30, Json{ { "type", "tradeConfirm" }, { "tradeId", tradeId }, { "offerVersion", 2u } }, 52.6)).at("reason") == "notFound");
			(void)move(29, 56.0, "CAFE", "F00D", 1, "normal");
			(void)move(30, 56.0, "CAFE", "F00D", 1, "normal");
			const auto reopened = packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", bob } }, 56.1), 29).at("tradeId").get<std::string>();
			assert(result(request(29, offerOf(reopened, 99), 57.0)).at("reason") == "insufficientItems");
			assert(packetFor(request(29, Json{ { "type", "tradeCancel" }, { "tradeId", reopened } }, 58.0), 29).at("phase") == "ended");
			(void)move(29, 58.5, "CAFE", "F00D", 1, "normal");
			(void)move(30, 58.5, "CAFE", "F00D", 1, "normal");
			assert(packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", bob } }, 59.0), 29).at("reason") == "cooldown");
			(void)move(29, 88.0, "CAFE", "F00D", 1, "normal");
			(void)move(30, 88.0, "CAFE", "F00D", 1, "normal");
			assert(packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", bob } }, 88.5), 29).at("phase") == "requested");
			const auto dropped = host.HandleDisconnected(30, 1001);
			persistence.ConnectionClosed(30);
			assert(packetFor(dropped, 29).at("phase") == "ended" && packetFor(dropped, 29).at("reason") == "disconnected");
			ready = join(31, true, true, true, true);
			const auto carol = joinedPlayerId;
			SendFrames(nullptr, host, ready, &identity, &persistence, 89.0, 1000, nullptr, CommonwealthOnline::Server::DeferredFramePolicy{});
			reconcile(31);
			(void)move(29, 89.0, "CAFE", "F00D", 1, "normal");
			(void)move(31, 89.0, "CAFE", "F00D", 1, "spawn");
			assert(packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", carol } }, 89.5), 29).at("phase") == "requested");
			assert(packetFor(host.PollTrades(125.0), 29).at("reason") == "expired");
			(void)move(29, 200.0, "CAFE", "F00D", 1, "normal");
			(void)move(31, 200.0, "CAFE", "F00D", 1, "normal");
			const auto separated = packetFor(request(29, Json{ { "type", "tradeRequest" }, { "targetPlayerId", carol } }, 200.5), 29).at("tradeId").get<std::string>();
			assert(packetFor(request(31, Json{ { "type", "tradeRespond" }, { "tradeId", separated }, { "accept", true } }, 200.6), 31).at("phase") == "negotiating");
			assert(packetFor(request(29, offerOf(separated, 2), 200.7), 29).at("ownOffer").size() == 1);
			assert(packetFor(request(29, Json{ { "type", "tradeConfirm" }, { "tradeId", separated }, { "offerVersion", 1u } }, 200.8), 29).at("ownConfirmed") == true);
			(void)move(31, 201.2, "BEEF", "F00D", 1);
			assert(result(request(31, Json{ { "type", "tradeConfirm" }, { "tradeId", separated }, { "offerVersion", 1u } }, 201.3)).at("reason") == "notNearby");
			assert(packetFor(host.PollTrades(201.4), 29).at("reason") == "separated");
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}
	%:include "../src/ServerSessionEnded.inc"

	void TestAllTradeRecipientsWaitForDurability(const std::filesystem::path& a_path)
	{
		DurableStateFileStore store(a_path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		store.SetDeferCommits(true);
		const auto before = store.LastPostedTicket();
		assert(store.SaveQuests(Json{ { "stages", 1 } }));
		const auto ticket = store.LastPostedTicket();
		assert(ticket > before);
		DeferredFrameQueue queue;
		DeferredFramePolicy policy{ .queue = &queue, .store = &store, .expiresAtMonotonic = 20.0,
			.gateConnectionId = 10, .ticketBefore = before };
		ProtocolHost protocol("Test", "");
		std::vector<OutboundFrame> frames{
			{ 10, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "tradeResult" }, { "status", "committed" } }) },
			{ 20, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "tradeResult" }, { "status", "committed" } }) },
			{ 20, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "tradeState" }, { "phase", "ended" } }) }
		};
		sent.clear();
		SendFrames(nullptr, protocol, frames, nullptr, nullptr, 1.0, 1000.0, nullptr, policy);
		assert(sent.empty() && queue.Entries().size() == 2);
		for (const auto& recipient : queue.Entries()) {
			for (const auto& frame : recipient.frames) {
				assert(frame.ticket == ticket && frame.ticketBefore == before);
			}
		}
		DeferredFramePolicy laterPolicy{ .queue = &queue, .store = &store, .expiresAtMonotonic = 20.0,
			.gateConnectionId = 20, .ticketBefore = ticket };
		std::vector<OutboundFrame> later{ { 20, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "inventorySnapshotResult" }, { "status", "rejected" }, { "revision", 1 } }) } };
		SendFrames(nullptr, protocol, later, nullptr, nullptr, 1.1, 1000.1, nullptr, laterPolicy);
		assert(sent.empty());
		const auto target = std::find_if(queue.Entries().begin(), queue.Entries().end(), [](const auto& entry) { return entry.connectionId == 20; });
		assert(target != queue.Entries().end() && target->frames.size() == 3);
		store.WaitForCommits();
		assert(SendDeferredFrames(nullptr, protocol, nullptr, nullptr, 2.0, 1000.0, nullptr, policy).empty());
		assert(sent.size() == 4 && queue.Empty());
		assert(sent[1].at("type") == "tradeResult" && sent[2].at("type") == "tradeState" && sent[3].at("type") == "inventorySnapshotResult");
		sent.clear();
	}

	void TestMaintenanceCaptureWaitsForDurability(const std::filesystem::path& a_path)
	{
		DurableStateFileStore store(a_path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		store.SetDeferCommits(true);
		assert(kMaintenanceGateConnectionId != 0);
		const auto before = store.LastPostedTicket();
		DeferredFrameQueue queue;
		DeferredFramePolicy gated{ .queue = &queue, .store = &store, .expiresAtMonotonic = 20.0,
			.gateConnectionId = kMaintenanceGateConnectionId, .ticketBefore = before };
		ProtocolHost protocol("Test", "");
		std::vector<OutboundFrame> frames{
			{ 10, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "settlementCaptureState" }, { "state", "captured" } }) }
		};
		assert(store.SaveQuests(Json{ { "stages", 1 } }));
		const auto ticket = store.LastPostedTicket();
		assert(ticket > before);
		sent.clear();
		SendFrames(nullptr, protocol, frames, nullptr, nullptr, 1.0, 1000.0, nullptr, gated);
		assert(sent.empty() && queue.Entries().size() == 1);
		assert(queue.Entries().front().frames.front().ticket == ticket && queue.Entries().front().frames.front().ticketBefore == before);
		store.WaitForCommits();
		assert(SendDeferredFrames(nullptr, protocol, nullptr, nullptr, 2.0, 1000.0, nullptr, gated).empty());
		assert(sent.size() == 1 && queue.Empty());
		DeferredFramePolicy ended{ .queue = &queue, .store = &store, .expiresAtMonotonic = 20.0, .ticketBefore = ticket };
		SendFrames(nullptr, protocol, frames, nullptr, nullptr, 3.0, 1000.0, nullptr, ended);
		assert(sent.size() == 2 && queue.Empty());
		DeferredFramePolicy noStore{ .expiresAtMonotonic = 20.0, .gateConnectionId = kMaintenanceGateConnectionId };
		SendFrames(nullptr, protocol, frames, nullptr, nullptr, 4.0, 1000.0, nullptr, noStore);
		assert(sent.size() == 3 && queue.Empty());
		sent.clear();
	}

	void TestSessionEndedPacketShape()
	{
		const std::string reason = "Your modpack does not match this server.";
		Json manifest{ { "version", 1 }, { "entries", Json::array() } };
		while (JsonBytes(manifest).size() + 160 <= F4T::Identity::kMaximumAdmissionManifestBytes) {
			manifest["entries"].push_back(Json{
				{ "kind", "native" },
				{ "name", "F4SE/Plugins/mod-" + std::to_string(manifest["entries"].size()) + ".dll" },
				{ "sha256", std::string(64, 'e') }
			});
		}
		assert(JsonBytes(manifest).size() <= F4T::Identity::kMaximumAdmissionManifestBytes);

		sent.clear();
		SendSessionEnded(nullptr, 7, "modpack_mismatch", reason, 1000.0, manifest, std::string_view{}, Json{}, std::string_view{});
		assert(sent.size() == 1);
		assert(lastSendBytes <= 64 * 1024);
		const auto& packet = sent.back();
		assert(packet.at("type") == "sessionEnded");
		assert(packet.at("code") == "modpack_mismatch");
		assert(packet.at("reason") == reason);
		assert(packet.at("serverTime") == 1000.0);
		assert(packet.at("requiredModpack") == manifest);

		SendSessionEnded(nullptr, 7, "banned", reason, 1001.0, Json{}, std::string_view{}, Json{}, std::string_view{});
		assert(sent.size() == 2);
		assert(!sent.back().contains("requiredModpack"));
		const auto fingerprint = std::string(64, 'b');
		const auto chunkInfo = Json{ { "totalBytes", 60000 }, { "chunkCount", 2 }, { "sha256", std::string(64, 'a') } };
		SendSessionEnded(nullptr, 7, "modpack_mismatch", reason, 1002.0, Json{}, fingerprint, chunkInfo, "Main List");
		assert(sent.size() == 3);
		assert(!sent.back().contains("requiredModpack"));
		assert(sent.back().at("requiredModpackFingerprint") == fingerprint);
		assert(sent.back().at("requiredModpackChunks") == chunkInfo);
		assert(sent.back().at("requiredModpackName") == "Main List");
		assert(lastSendBytes <= 64 * 1024);
		sent.clear();
	}

	void TestDeferredReadySurvivalPolicy()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-ready-survival-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		DurableStateFileStore store(directory / "state.json", [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		PersistenceGate persistence(store);
		ProtocolHost protocol("Test", "");
		assert(protocol.EnableInventoryTransactions(persistence));
		protocol.SetSurvivalMode(false);
			protocol.SetSurvivalNeeds(false);
		DeferredFrameQueue queue;
		queue.Park(7, OutboundFrame{ 7, Delivery::ReliableOrdered, JsonBytes(Json{
			{ "type", "sessionReady" }, { "playerId", 1u }, { "survivalMode", false }, { "survivalNeeds", false },
			{ "serverInventory", Json{ { "initialized", true }, { "itemCount", 0 }, { "chunkCount", 0 }, { "transferId", "test" } } }
		}) }, false, 0, 10.0);
		protocol.SetSurvivalMode(true);
			protocol.SetSurvivalNeeds(true);
		DeferredFramePolicy policy{ .queue = &queue };
		std::vector<std::uint32_t> retire;
		sent.clear();
		const auto abandoned = SendDeferredFrames(nullptr, protocol, nullptr, nullptr, 1.0, 1.0, &retire, policy);
		assert(abandoned.empty() && retire.empty() && queue.Empty());
		assert(sent.size() == 1 && sent.front().at("survivalMode") == true && sent.front().at("survivalNeeds") == true);
		sent.clear();
		std::filesystem::remove_all(directory);
	}
}

extern "C" std::size_t co_gns_maximum_message_bytes(void)
{
	return 512 * 1024;
}

extern "C" int co_gns_server_send(co_gns_server_handle, std::uint32_t,
	const void* a_payload, std::uint32_t a_size, std::uint32_t)
{
	const auto* bytes = static_cast<const std::uint8_t*>(a_payload);
	const auto decoded = F4T::PayloadCodec::Decode(std::span<const std::uint8_t>{ bytes, a_size }, co_gns_maximum_message_bytes());
	const auto packet = decoded ? Json::parse(decoded->begin(), decoded->end(), nullptr, false) : Json{};
	assert(a_size <= 64 * 1024);
	lastSendBytes = a_size;
	if (!packet.is_object()) {
		return sendResult;
	}
	if (packet.at("type") == "sessionReady") {
		const auto& header = packet.at("serverInventory");
		assert(header.size() == 4);
		assert(header.at("initialized") == true);
		assert(header.contains("itemCount") && header.contains("chunkCount"));
		assert(!header.contains("items"));
	} else if (packet.at("type") == "inventoryTransfer") {
		assert(a_size <= F4T::InventoryContract::kMaximumInventoryTransferChunkBytes);
	}
	sent.push_back(packet);
	const auto result = sendCount == failSendIndex ? CO_GNS_SEND_BACKPRESSURE : sendResult;
	++sendCount;
	return result;
}

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::filesystem::temp_directory_path() / ("co-inventory-ready-" + std::to_string(stamp));
	const auto path = directory / "state.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	try {
		DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		inspectedStore = &store;
		OpenSslTokenService tokens;
		IdentityGate identity({}, tokens, &store);
		PersistenceGate persistence(store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		ProtocolHost protocol("Test", "");
		const auto hello = JsonBytes(Json{
			{ "type", "hello" }, { "protocolVersion", 2 }, { "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" }, { "profileId", "profile-001" },
			{ "contentManifestFingerprint", std::string(64, 'a') }, { "capabilities", Json::array({ "save-attestation-v1" }) }
		});
		assert(identity.InspectInbound(7, hello, 1000.0).forward);
		std::vector<OutboundFrame> frames{ { 7, Delivery::ReliableOrdered,
			JsonBytes(Json{ { "type", "sessionReady" }, { "playerId", 1u }, { "profileId", "forged-victim" } }) } };
		std::vector<std::uint32_t> retire;
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 1.0, 1000.0, &retire, CommonwealthOnline::Server::DeferredFramePolicy{});
		assert(sent.empty());
		assert(retire == std::vector<std::uint32_t>{ 7 });
		assert(!store.GetInventory("profile-001") && !store.GetInventory("forged-victim"));
		assert(!persistence.ProfileForConnection(7));
		std::filesystem::remove_all(backup);
		retire.clear();
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 2.0, 1000.0, &retire, CommonwealthOnline::Server::DeferredFramePolicy{});
		assert(sent.size() == 1 && retire == std::vector<std::uint32_t>{ 7 });
		assert(!persistence.ProfileForConnection(7));
		const auto granted = store.GetInventory("profile-001");
		assert(granted && !store.GetInventory("forged-victim"));
		retire.clear();
		sendResult = CO_GNS_SEND_SENT;
		failSendIndex = 2;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 3.0, 1000.0, &retire, CommonwealthOnline::Server::DeferredFramePolicy{});
		assert(sent.size() == 3);
		assert(sent.back().at("type") == "inventoryTransfer");
		assert(retire == std::vector<std::uint32_t>{ 7 });
		assert(!persistence.ProfileForConnection(7));
		retire.clear();
		failSendIndex = -1;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 4.0, 1000.0, &retire, CommonwealthOnline::Server::DeferredFramePolicy{});
		assert(sent.size() == 6 && retire.empty());
		assert(sent[3].at("type") == "sessionReady");
		assert(sent[4].at("type") == "inventoryTransfer");
		assert(sent[4].at("items").size() == 1);
		assert(sent[5].at("type") == "startingInventoryRecovery" && sent[5].at("state") == "unstarted");
		assert(store.GetInventory("profile-001") == granted);
		assert(persistence.ProfileForConnection(7) == "profile-001");
	} catch (...) {
		std::filesystem::remove_all(directory);
		throw;
	}
	std::filesystem::remove_all(directory);
	TestAllTradeRecipientsWaitForDurability(directory / "trade-state.json");
	std::filesystem::remove_all(directory);
	TestMaintenanceCaptureWaitsForDurability(directory / "maintenance-capture.json");
	std::filesystem::remove_all(directory);
	TestComponentSendRecovery();
	TestCompatibilitySendRecovery();
	TestOwnedInventoryProtocol();
	TestSessionEndedPacketShape();
	TestDeferredReadySurvivalPolicy();
}
