#include "ServerProtocol.h"
#include "ServerConfiguration.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TObjectState.h"
#include "F4TObjectWire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(
			std::span<std::uint8_t>{ output }, F4T::GnsSnapshotEnvelope::Family::Transform, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	std::vector<Json> FramesOfType(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) continue;
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) packets.push_back(std::move(packet));
		}
		return packets;
	}

	Json RequirePacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		const auto packets = FramesOfType(a_frames, a_connectionId, a_type);
		assert(!packets.empty());
		return packets.front();
	}

	std::vector<OutboundFrame> JoinFrames(ProtocolHost& a_host, std::uint32_t a_connection, bool a_objectState)
	{
		(void)a_host.HandleConnected(a_connection, 100.0);
		Json capabilities = Json::array();
		if (a_objectState) capabilities.push_back("object-state-v1");
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "hello" },
			{ "protocolVersion", 2u },
			{ "capabilities", capabilities } }), 1.0, 100.0);
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connection, bool a_objectState)
	{
		assert(!JoinFrames(a_host, a_connection, a_objectState).empty());
	}

	std::uint32_t transformSequence{ 1 };

	void Move(ProtocolHost& a_host, std::uint32_t a_connection, double a_monotonic, double a_x,
		const char* a_cell = "100", const char* a_worldspace = "F00D", const char* a_movementType = "spawn")
	{
		(void)a_host.HandleMessage(a_connection, EncodeSnapshot(Json{
			{ "type", "transform" },
			{ "cellId", a_cell },
			{ "worldspaceId", a_worldspace },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", a_movementType } }, transformSequence++), a_monotonic, 100.0);
	}

	std::string Hex8(std::uint32_t a_value)
	{
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
		return buffer;
	}

	Json Position(double a_x = 0.0, const char* a_cell = "00000100", const char* a_worldspace = "0000F00D")
	{
		return Json{ { "cellId", a_cell }, { "worldspaceId", a_worldspace }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 } };
	}

	Json Activate(const char* a_operationId, const char* a_action = "open", double a_x = 0.0,
		const char* a_cell = "00000100", const char* a_worldspace = "0000F00D")
	{
		return Json{ { "type", "objectActivateRequest" }, { "operationId", a_operationId },
			{ "object", Json{ { "plugin", "Test.esm" }, { "localFormId", "00001234" } } },
			{ "kind", "door" }, { "action", a_action }, { "position", Position(a_x, a_cell, a_worldspace) } };
	}

	Json ActivateObject(const char* a_operationId, const char* a_plugin, const char* a_formId, double a_x = 10.0,
		const char* a_cell = "00000100", const char* a_worldspace = "0000F00D")
	{
		return Json{ { "type", "objectActivateRequest" }, { "operationId", a_operationId },
			{ "object", Json{ { "plugin", a_plugin }, { "localFormId", a_formId } } },
			{ "kind", "door" }, { "action", "open" }, { "position", Position(a_x, a_cell, a_worldspace) } };
	}

	Json Lock(const char* a_operationId, bool a_locked, std::optional<std::uint32_t> a_lockLevel = std::nullopt)
	{
		Json packet{ { "type", "objectLockRequest" }, { "operationId", a_operationId },
			{ "object", Json{ { "plugin", "Test.esm" }, { "localFormId", "00001234" } } },
			{ "locked", a_locked }, { "position", Position() } };
		if (a_lockLevel) packet["lockLevel"] = *a_lockLevel;
		return packet;
	}

	std::vector<OutboundFrame> Request(ProtocolHost& a_host, std::uint32_t a_connection, const Json& a_packet, double a_monotonic)
	{
		return a_host.HandleMessage(a_connection, EncodeJson(a_packet), a_monotonic, 100.0);
	}

	void TestConfigurationFlag()
	{
		using CommonwealthOnline::Server::Configuration::Parse;
		const auto byDefault = Parse(Json::object());
		assert(byDefault.enableObjectState);
		const auto snake = Parse(Json{ { "enable_object_state", true } });
		assert(snake.enableObjectState);
		const auto camel = Parse(Json{ { "enableObjectState", true } });
		assert(camel.enableObjectState);
		const auto explicitOff = Parse(Json{ { "enable_object_state", false } });
		assert(!explicitOff.enableObjectState);
	}

	void TestDisabledByDefault()
	{
		ProtocolHost host{ "Object State Test" };
		const auto welcome = host.HandleConnected(10, 100.0);
		const auto capabilities = DecodePacket(welcome.front()).at("capabilities").get<std::vector<std::string>>();
		assert(std::none_of(capabilities.begin(), capabilities.end(),
			[](const std::string& a_value) { return a_value == "object-state-v1"; }));
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		const auto frames = Request(host, 10, Activate("disabled-1"), 2.5);
		const auto rejected = RequirePacket(frames, 10, "objectStateRejected");
		assert(rejected.at("reason") == "objectStateUnavailable" && rejected.at("operationId") == "disabled-1");
		assert(host.Stats().objectStateRouted == 0 && host.Stats().objectStateReceived == 1);
		const auto late = JoinFrames(host, 20, true);
		assert(FramesOfType(late, 20, "objectStateSnapshot").empty());
	}

	void TestCapabilityGateAndBroadcast()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		assert(!host.EnableObjectState());
		const auto welcome = host.HandleConnected(10, 100.0);
		const auto capabilities = DecodePacket(welcome.front()).at("capabilities").get<std::vector<std::string>>();
		assert(std::find(capabilities.begin(), capabilities.end(), "object-state-v1") != capabilities.end());
		Join(host, 10, true);
		Join(host, 20, true);
		Join(host, 30, false);
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 20.0);
		Move(host, 30, 2.0, 0.0);

		const auto gated = Request(host, 30, Activate("gated-1"), 2.4);
		assert(RequirePacket(gated, 30, "objectStateRejected").at("reason") == "objectStateUnavailable");
		assert(FramesOfType(gated, 10, "objectState").empty() && FramesOfType(gated, 20, "objectState").empty());

		const auto frames = Request(host, 10, Activate("broadcast-1"), 2.5);
		const auto forTen = RequirePacket(frames, 10, "objectState");
		const auto forTwenty = RequirePacket(frames, 20, "objectState");
		assert(FramesOfType(frames, 30, "objectState").empty());
		assert(forTen == forTwenty);
		assert(forTen.at("kind") == "door" && forTen.at("open") == true && forTen.at("revision") == 1u);
		assert(forTen.at("object").at("plugin") == "Test.esm" && forTen.at("object").at("localFormId") == "00001234");
		assert(!forTen.contains("locked"));
		assert(F4T::ObjectWire::NormalizeObjectState(forTen).has_value());
		assert(host.Stats().objectStateRouted == 1);

		const auto other = Request(host, 20, Activate("broadcast-2", "toggle", 20.0), 2.6);
		const auto toggled = RequirePacket(other, 10, "objectState");
		assert(toggled.at("open") == false && toggled.at("revision") == 2u);
	}

	void TestMovementGates()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);

		assert(RequirePacket(Request(host, 10, Activate("stale-1"), 7.5), 10, "objectStateRejected").at("reason") == "staleTransform");
		assert(RequirePacket(Request(host, 10, Activate("scope-1", "open", 0.0, "00000200", "0000DEAD"), 2.5), 10, "objectStateRejected").at("reason") == "outOfScope");
		assert(RequirePacket(Request(host, 10, Activate("reach-1", "open", 513.0), 2.6), 10, "objectStateRejected").at("reason") == "outOfReach");
		const auto edge = Request(host, 10, Activate("reach-2", "open", 512.0), 2.7);
		assert(RequirePacket(edge, 10, "objectState").at("revision") == 1u);
		const auto driftEdge = Request(host, 10, Activate("drift-1", "toggle", 448.0), 2.8);
		assert(RequirePacket(driftEdge, 10, "objectState").at("revision") == 2u);
		assert(RequirePacket(Request(host, 10, Activate("drift-2", "toggle", 447.5), 2.9), 10, "objectStateRejected").at("reason") == "positionMismatch");
		assert(RequirePacket(Request(host, 10, Activate("drift-3", "close", 100.0), 3.0), 10, "objectStateRejected").at("reason") == "positionMismatch");
		assert(RequirePacket(Request(host, 10, Lock("lock-drift", true, 1u), 3.1), 10, "objectStateRejected").at("reason") == "positionMismatch");
		assert(host.Stats().objectStateRouted == 2 && host.Stats().objectStateRejected == 6);
	}

	void TestReplayAndConflict()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 20, true);
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 0.0);

		const auto first = Request(host, 10, Activate("replay-1"), 2.5);
		assert(RequirePacket(first, 10, "objectState").at("revision") == 1u);
		const auto replay = Request(host, 10, Activate("replay-1"), 2.6);
		assert(FramesOfType(replay, 20, "objectState").empty());
		const auto repeated = RequirePacket(replay, 10, "objectState");
		assert(repeated.at("revision") == 1u && host.Stats().objectStateReplayed == 1);
		const auto conflict = Request(host, 10, Activate("replay-1", "close"), 2.7);
		assert(RequirePacket(conflict, 10, "objectStateRejected").at("reason") == "operationConflict");
		const auto otherPlayer = Request(host, 20, Activate("replay-1"), 2.8);
		assert(RequirePacket(otherPlayer, 20, "objectState").at("revision") == 1u);
	}

	void TestLockFlow()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		assert(RequirePacket(Request(host, 10, Lock("lock-0", true, 1u), 2.1), 10, "objectStateRejected").at("reason") == "unknownObject");
		assert(RequirePacket(Request(host, 10, Activate("lock-open"), 2.2), 10, "objectState").at("revision") == 1u);
		const auto locked = RequirePacket(Request(host, 10, Lock("lock-1", true, 2u), 2.3), 10, "objectState");
		assert(locked.at("locked") == true && locked.at("lockLevel") == 2u && locked.at("revision") == 2u);
		assert(F4T::ObjectWire::NormalizeObjectState(locked).has_value());
		assert(RequirePacket(Request(host, 10, Lock("lock-2", true, 2u), 2.4), 10, "objectState").at("revision") == 2u);
		const auto unlocked = RequirePacket(Request(host, 10, Lock("lock-3", false), 2.5), 10, "objectState");
		assert(unlocked.at("locked") == false && unlocked.at("revision") == 3u && !unlocked.contains("lockLevel"));
	}

	void TestRateLimit()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		for (std::uint32_t index = 0; index < 8; ++index) {
			const auto frames = Request(host, 10, Activate(("burst-" + std::to_string(index)).c_str()), 2.5);
			assert(!FramesOfType(frames, 10, "objectState").empty());
		}
		assert(RequirePacket(Request(host, 10, Activate("burst-limited"), 2.5), 10, "objectStateRejected").at("reason") == "rateLimited");
		assert(!FramesOfType(Request(host, 10, Activate("burst-refilled"), 2.75), 10, "objectState").empty());
	}

	void TestSnapshotOnJoin()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		assert(RequirePacket(Request(host, 10, Activate("snapshot-1"), 2.5), 10, "objectState").at("revision") == 1u);
		assert(RequirePacket(Request(host, 10, Lock("snapshot-lock", true, 3u), 2.6), 10, "objectState").at("revision") == 2u);

		const auto late = JoinFrames(host, 20, true);
		const auto snapshot = RequirePacket(late, 20, "objectStateSnapshot");
		assert(snapshot.at("objects").size() == 1);
		const auto& item = snapshot.at("objects").at(0);
		assert(item.at("revision") == 2u && item.at("locked") == true && item.at("lockLevel") == 3u);
		assert(item.at("kind") == "door" && item.at("open") == true);
		assert(F4T::ObjectWire::NormalizeObjectStateSnapshot(snapshot).has_value());

		const auto gated = JoinFrames(host, 30, false);
		assert(FramesOfType(gated, 30, "objectStateSnapshot").empty());
	}

	void TestSnapshotChunking()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 11, true);
		Move(host, 10, 2.0, 0.0);
		Move(host, 11, 2.0, 0.0, "200", "BEEF");
		const std::uint32_t objectCount = 300;
		double monotonic = 2.0;
		for (std::uint32_t index = 1; index <= objectCount; ++index) {
			monotonic += 0.3;
			if (index % 8 == 1) {
				Move(host, 10, monotonic, 0.0, "100", "F00D", "normal");
				Move(host, 11, monotonic, 0.0, "200", "BEEF", "normal");
			}
			const bool hidden = index % 2 == 1;
			const std::uint32_t sender = hidden ? 11 : 10;
			const auto frames = Request(host, sender,
				ActivateObject(("chunk-" + std::to_string(index)).c_str(), "Bulk.esm", Hex8(index).c_str(), 10.0,
					hidden ? "00000200" : "00000100", hidden ? "0000BEEF" : "0000F00D"),
				monotonic);
			assert(!FramesOfType(frames, sender, "objectState").empty());
		}

		const auto joiner = JoinFrames(host, 20, true);
		const auto snapshots = FramesOfType(joiner, 20, "objectStateSnapshot");
		const auto expectedFrames =
			(objectCount + F4T::ObjectWire::kMaximumSnapshotObjects - 1) / F4T::ObjectWire::kMaximumSnapshotObjects;
		assert(expectedFrames > 2 && snapshots.size() == expectedFrames);
		std::vector<std::string> delivered;
		for (const auto& snapshot : snapshots) {
			const auto& objects = snapshot.at("objects");
			assert(!objects.empty() && objects.size() <= F4T::ObjectWire::kMaximumSnapshotObjects);
			assert(F4T::ObjectWire::NormalizeObjectStateSnapshot(snapshot).has_value());
			for (const auto& item : objects) {
				delivered.push_back(item.at("object").at("localFormId").get<std::string>());
			}
		}
		assert(delivered.size() == objectCount);
		std::sort(delivered.begin(), delivered.end());
		assert(std::adjacent_find(delivered.begin(), delivered.end()) == delivered.end());
		for (std::uint32_t index = 1; index <= objectCount; ++index) {
			assert(std::binary_search(delivered.begin(), delivered.end(), Hex8(index)));
		}

		Join(host, 30, true);
		Move(host, 30, monotonic, 0.0, "200", "BEEF");
		monotonic += 0.3;
		const auto scoped = Request(host, 11,
			ActivateObject("chunk-scoped", "Bulk.esm", Hex8(400).c_str(), 10.0, "00000200", "0000BEEF"), monotonic);
		assert(!FramesOfType(scoped, 11, "objectState").empty());
		assert(!FramesOfType(scoped, 30, "objectState").empty());
		assert(FramesOfType(scoped, 10, "objectState").empty());
	}

	void TestPerPlayerCap()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		const auto cap = static_cast<std::uint32_t>(F4T::ObjectState::kMaximumObjectsPerPlayer);
		double monotonic = 2.0;
		for (std::uint32_t index = 1; index <= cap; ++index) {
			monotonic += 0.3;
			if (index % 8 == 1) Move(host, 10, monotonic, 0.0, "100", "F00D", "normal");
			const auto frames = Request(host, 10,
				ActivateObject(("cap-" + std::to_string(index)).c_str(), "Cap.esm", Hex8(index).c_str()), monotonic);
			assert(!FramesOfType(frames, 10, "objectState").empty());
		}
		monotonic += 0.3;
		const auto over = Request(host, 10,
			ActivateObject("cap-over", "Cap.esm", Hex8(cap + 1).c_str()), monotonic);
		const auto rejectedFrame = RequirePacket(over, 10, "objectStateRejected");
		assert(rejectedFrame.at("reason") == "objectCapacityExceeded" && rejectedFrame.at("operationId") == "cap-over");
		assert(FramesOfType(over, 10, "objectState").empty());

		Join(host, 20, true);
		Move(host, 20, monotonic, 0.0);
		monotonic += 0.3;
		const auto other = Request(host, 20, ActivateObject("cap-other", "Cap.esm", Hex8(700).c_str()), monotonic);
		assert(!FramesOfType(other, 20, "objectState").empty());
	}

	void TestMalformedRequests()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Move(host, 10, 2.0, 0.0);
		const auto rejectedBefore = host.Stats().packetsRejected;
		auto padded = Activate("malformed-1");
		padded["unexpected"] = 1u;
		assert(Request(host, 10, padded, 2.5).empty());
		auto runtime = Activate("malformed-2");
		runtime["object"] = Json{ { "plugin", "Test.esm" }, { "localFormId", "FF001234" } };
		assert(Request(host, 10, runtime, 2.6).empty());
		auto badLevel = Lock("malformed-3", true, 9u);
		assert(Request(host, 10, badLevel, 2.7).empty());
		assert(host.Stats().packetsRejected == rejectedBefore + 3);
		assert(host.Stats().objectStateRouted == 0);
	}
}

int main()
{
	TestConfigurationFlag();
	TestDisabledByDefault();
	TestCapabilityGateAndBroadcast();
	TestMovementGates();
	TestReplayAndConflict();
	TestLockFlow();
	TestRateLimit();
	TestSnapshotOnJoin();
	TestSnapshotChunking();
	TestPerPlayerCap();
	TestMalformedRequests();
	return 0;
}
