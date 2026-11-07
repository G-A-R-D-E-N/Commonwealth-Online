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

	std::vector<OutboundFrame> JoinWithCapabilities(ProtocolHost& a_host, std::uint32_t a_connection,
		const std::vector<std::string>& a_capabilities)
	{
		(void)a_host.HandleConnected(a_connection, 100.0);
		Json capabilities = Json::array();
		for (const auto& capability : a_capabilities) capabilities.push_back(capability);
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "hello" },
			{ "protocolVersion", 2u },
			{ "capabilities", capabilities } }), 1.0, 100.0);
	}

	std::vector<OutboundFrame> JoinFrames(ProtocolHost& a_host, std::uint32_t a_connection, bool a_objectState)
	{
		return JoinWithCapabilities(a_host, a_connection, a_objectState ? std::vector<std::string>{ "object-state-v1" } : std::vector<std::string>{});
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connection, bool a_objectState)
	{
		assert(!JoinFrames(a_host, a_connection, a_objectState).empty());
	}

	std::uint32_t transformSequence{ 1 };

	std::vector<OutboundFrame> Move(ProtocolHost& a_host, std::uint32_t a_connection, double a_monotonic, double a_x,
		const char* a_cell = "100", const char* a_worldspace = "F00D", const char* a_movementType = "spawn")
	{
		return a_host.HandleMessage(a_connection, EncodeSnapshot(Json{
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

	Json TerminalLock(const char* a_operationId, bool a_locked, std::optional<std::uint32_t> a_lockLevel = std::nullopt)
	{
		Json packet = Lock(a_operationId, a_locked, a_lockLevel);
		packet["kind"] = "terminal";
		packet["object"] = Json{ { "plugin", "Test.esm" }, { "localFormId", "00005678" } };
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
		assert(std::find(capabilities.begin(), capabilities.end(), "object-state-v2") != capabilities.end());
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

	void TestTerminalCapabilityGate()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		JoinWithCapabilities(host, 10, { "object-state-v1" });
		JoinWithCapabilities(host, 20, { "object-state-v1", "object-state-v2" });
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 0.0);

		assert(RequirePacket(Request(host, 10, TerminalLock("term-v1", true, 2u), 2.5), 10, "objectStateRejected").at("reason") == "objectStateUnavailable");

		const auto frames = Request(host, 20, TerminalLock("term-v2", true, 2u), 2.6);
		const auto state = RequirePacket(frames, 20, "objectState");
		assert(state.at("kind") == "terminal" && state.at("locked") == true && state.at("lockLevel") == 2u && state.at("revision") == 1u);
		assert(FramesOfType(frames, 10, "objectState").empty());

		const auto v1Late = JoinWithCapabilities(host, 30, { "object-state-v1" });
		assert(FramesOfType(v1Late, 30, "objectStateSnapshot").empty());
		const auto v2Late = JoinWithCapabilities(host, 40, { "object-state-v1", "object-state-v2" });
		const auto snapshot = RequirePacket(v2Late, 40, "objectStateSnapshot");
		assert(snapshot.at("objects").size() == 1 && snapshot.at("objects").at(0).at("kind") == "terminal");
		assert(F4T::ObjectWire::NormalizeObjectStateSnapshot(snapshot).has_value());

		Json kindless = TerminalLock("term-plain", true, 2u);
		kindless.erase("kind");
		assert(RequirePacket(Request(host, 10, kindless, 2.7), 10, "objectStateRejected").at("reason") == "kindMismatch");
		assert(RequirePacket(Request(host, 10, kindless, 2.8), 10, "objectStateRejected").at("reason") == "kindMismatch");
		Json reused = TerminalLock("term-v2", true, 2u);
		reused.erase("kind");
		assert(RequirePacket(Request(host, 20, reused, 2.9), 20, "objectStateRejected").at("reason") == "operationConflict");
		assert(FramesOfType(Request(host, 10, kindless, 3.0), 10, "objectState").empty());
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

	void TestSnapshotOnExteriorInterestEntry()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 20, true);
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 9000.0, "200", "F00D");
		const auto change = Request(host, 10, Activate("range-entry"), 2.5);
		assert(FramesOfType(change, 20, "objectState").empty());
		const auto enter = Move(host, 20, 12.0, 8000.0, "200", "F00D", "normal");
		const auto snapshot = RequirePacket(enter, 20, "objectStateSnapshot");
		assert(snapshot.at("objects").size() == 1 && snapshot.at("objects").at(0).at("open") == true);
		const auto stillInside = Move(host, 20, 13.0, 7990.0, "200", "F00D", "normal");
		assert(FramesOfType(stillInside, 20, "objectStateSnapshot").empty());
	}

	void TestNoScanBelowStepAndEntryAfterStep()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 20, true);
		Move(host, 10, 2.0, 800.0, "100", "F00D");
		Move(host, 20, 2.0, 9000.0, "200", "F00D");
		const auto appear = Request(host, 10, Activate("step-entry", "open", 800.0, "00000100", "0000F00D"), 2.5);
		assert(FramesOfType(appear, 20, "objectState").empty());
		const auto smallStep = Move(host, 20, 12.0, 8900.0, "200", "F00D", "normal");
		assert(FramesOfType(smallStep, 20, "objectStateSnapshot").empty());
		const auto enter = Move(host, 20, 22.0, 8000.0, "200", "F00D", "normal");
		const auto snapshot = RequirePacket(enter, 20, "objectStateSnapshot");
		assert(snapshot.at("objects").size() == 1 && snapshot.at("objects").at(0).at("open") == true);
		const auto stillInside = Move(host, 20, 23.0, 7990.0, "200", "F00D", "normal");
		assert(FramesOfType(stillInside, 20, "objectStateSnapshot").empty());
	}

	void TestScanPointMarginResendsBandObject()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 20, true);
		Move(host, 10, 2.0, 8190.0, "100", "F00D");
		Move(host, 20, 2.0, 0.0, "200", "F00D");
		Move(host, 10, 12.0, 8190.0, "100", "F00D", "normal");
		Move(host, 20, 12.0, -100.0, "200", "F00D", "normal");
		const auto created = Request(host, 10, Activate("band-object", "open", 8190.0, "00000100", "0000F00D"), 12.5);
		assert(RequirePacket(created, 10, "objectState").at("revision") == 1u);
		assert(FramesOfType(created, 20, "objectState").empty());
		const auto returned = Move(host, 20, 22.0, 300.0, "200", "F00D", "normal");
		const auto snapshot = RequirePacket(returned, 20, "objectStateSnapshot");
		assert(snapshot.at("objects").size() == 1 && snapshot.at("objects").at(0).at("open") == true);
	}

	void TestTinyStepsEnterAfterCumulativeStep()
	{
		ProtocolHost host{ "Object State Test" };
		assert(host.EnableObjectState());
		Join(host, 10, true);
		Join(host, 20, true);
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 8200.0, "200", "F00D");
		assert(FramesOfType(Request(host, 10, Activate("tiny-entry"), 2.5), 20, "objectState").empty());
		double monotonic = 2.0;
		for (double x = 8180.0; x >= 7940.0; x -= 20.0) {
			monotonic += 1.0;
			const auto frames = Move(host, 20, monotonic, x, "200", "F00D", "normal");
			if (x > 7940.0) {
				assert(FramesOfType(frames, 20, "objectStateSnapshot").empty());
			} else {
				assert(RequirePacket(frames, 20, "objectStateSnapshot").at("objects").size() == 1);
			}
		}
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

	std::vector<OutboundFrame> ParkedJoin(ProtocolHost& a_host, std::uint32_t a_connection, const char* a_profileId)
	{
		(void)a_host.HandleConnected(a_connection, 100.0);
		a_host.BindSessionProfile(a_connection, a_profileId);
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "hello" },
			{ "protocolVersion", 2u },
			{ "capabilities", Json::array({ "parked-suit-v1" }) } }), 1.0, 100.0);
	}

	Json ParkedPacket(const char* a_localId, const char* a_frame)
	{
		return Json{ { "type", "parkedSuit" }, { "action", "park" }, { "localId", a_localId },
			{ "powerArmor", Json{ { "inFrame", true }, { "frameBaseFormId", a_frame }, { "pieces", Json::array() } } },
			{ "position", Position() }, { "angleZ", 0.0 }, { "nonce", 7u } };
	}

	Json ParkedFrameState(const char* a_frame)
	{
		return Json{ { "type", "playerState" }, { "actionEvents", Json::array() },
			{ "powerArmor", Json{ { "inFrame", true }, { "frameBaseFormId", a_frame }, { "pieces", Json::array() } } } };
	}

	Json ParkedSuitAction(const char* a_action, const char* a_suitId)
	{
		return Json{ { "type", "parkedSuit" }, { "action", a_action }, { "suitId", a_suitId } };
	}

	std::optional<Json> ParkedAck(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connection)
	{
		for (const auto& packet : FramesOfType(a_frames, a_connection, "parkedSuit")) {
			if (packet.value("action", std::string{}) == "ack") return packet;
		}
		return std::nullopt;
	}

	std::size_t ParkedChallengeCount(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connection)
	{
		std::size_t count = 0;
		for (const auto& packet : FramesOfType(a_frames, a_connection, "parkedSuit")) {
			if (packet.value("action", std::string{}) == "breachChallenge") ++count;
		}
		return count;
	}

	void TestParkedSuitFrameGate()
	{
		ProtocolHost host{ "Parked Frame Gate Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		ParkedJoin(host, 20, "profile-b");
		Move(host, 10, 2.0, 0.0);
		const auto refused = ParkedAck(Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 2.5), 10);
		assert(refused && refused->at("ok") == false && refused->at("reason") == "notInFrame");
		(void)Request(host, 10, ParkedFrameState("0010A0FF"), 2.6);
		const auto accepted = ParkedAck(Request(host, 10, ParkedPacket("00000002", "0010A0FF"), 2.7), 10);
		assert(accepted && accepted->at("ok") == true);
		Move(host, 10, 123.5, 0.0, "100", "F00D", "normal");
		const auto stillObserved = ParkedAck(Request(host, 10, ParkedPacket("00000003", "0010A0FF"), 123.7), 10);
		assert(stillObserved && stillObserved->at("ok") == true);
		Move(host, 10, 602.5, 0.0, "100", "F00D", "normal");
		const auto stale = ParkedAck(Request(host, 10, ParkedPacket("00000004", "0010A0FF"), 602.7), 10);
		assert(stale && stale->at("ok") == false && stale->at("reason") == "notInFrame");
	}

	Json InFrameTransform(bool a_inFrame)
	{
		return Json{ { "type", "transform" }, { "cellId", "100" }, { "worldspaceId", "F00D" }, { "x", 2.0 }, { "y", 0.0 }, { "z", 0.0 },
			{ "angleZ", 0.0 }, { "movementType", "normal" },
			{ "powerArmor", Json{ { "inFrame", a_inFrame }, { "frameBaseFormId", "0010A0FF" }, { "pieces", Json::array() } } } };
	}

	void TestParkedSuitFrameObservedFromTransform()
	{
		ProtocolHost host{ "Parked Transform Observation Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		Move(host, 10, 2.0, 0.0);
		(void)host.HandleMessage(10, EncodeSnapshot(InFrameTransform(true), transformSequence++), 2.1, 100.0);
		const auto accepted = ParkedAck(Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 2.2), 10);
		assert(accepted && accepted->at("ok") == true);
		(void)host.HandleMessage(10, EncodeSnapshot(InFrameTransform(false), transformSequence++), 2.3, 100.0);
		const auto exited = ParkedAck(Request(host, 10, ParkedPacket("00000002", "0010A0FF"), 2.4), 10);
		assert(exited && exited->at("ok") == true);
	}

	void TestParkedSuitRejectedTransformDoesNotObserve()
	{
		ProtocolHost host{ "Parked Rejected Transform Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		Move(host, 10, 2.0, 0.0);
		auto rejected = InFrameTransform(true);
		rejected["movementType"] = "spawn";
		(void)host.HandleMessage(10, EncodeSnapshot(rejected, transformSequence++), 2.1, 100.0);
		const auto refused = ParkedAck(Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 2.2), 10);
		assert(refused && refused->at("ok") == false && refused->at("reason") == "notInFrame");
		(void)host.HandleMessage(10, EncodeSnapshot(InFrameTransform(true), transformSequence++), 2.3, 100.0);
		const auto accepted = ParkedAck(Request(host, 10, ParkedPacket("00000002", "0010A0FF"), 2.4), 10);
		assert(accepted && accepted->at("ok") == true);
	}

	void TestParkedSuitFrameObservationSurvivesReconnect()
	{
		ProtocolHost host{ "Parked Reconnect Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		Move(host, 10, 2.0, 0.0);
		(void)Request(host, 10, ParkedFrameState("0010A0FF"), 2.1);
		(void)host.HandleDisconnected(10, 100.5);
		ParkedJoin(host, 11, "profile-a");
		Move(host, 11, 3.0, 0.0);
		const auto reconnected = ParkedAck(Request(host, 11, ParkedPacket("00000001", "0010A0FF"), 3.1), 11);
		assert(reconnected && reconnected->at("ok") == true);
		Move(host, 11, 602.0, 0.0, "100", "F00D", "normal");
		const auto expired = ParkedAck(Request(host, 11, ParkedPacket("00000002", "0010A0FF"), 602.2), 11);
		assert(expired && expired->at("ok") == false && expired->at("reason") == "notInFrame");
	}

	void TestParkedSuitFrameObservationRefreshesAfterSecond()
	{
		ProtocolHost host{ "Parked Observation Refresh Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		Move(host, 10, 2.0, 0.0);
		(void)host.HandleMessage(10, EncodeSnapshot(InFrameTransform(true), transformSequence++), 2.1, 100.0);
		(void)host.HandleMessage(10, EncodeSnapshot(InFrameTransform(true), transformSequence++), 3.2, 100.0);
		Move(host, 10, 602.4, 0.0, "100", "F00D", "normal");
		const auto refreshed = ParkedAck(Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 602.5), 10);
		assert(refreshed && refreshed->at("ok") == true);
	}

	void TestParkedSuitConsoleSpawnNeedsAdmin()
	{
		ProtocolHost host{ "Parked Console Test", {}, { "profile-admin" }, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-player");
		ParkedJoin(host, 20, "profile-admin");
		host.SetSessionAdminVerified(20, true);
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 0.0);
		auto consolePacket = ParkedPacket("00000001", "0010A0FF");
		consolePacket["console"] = true;
		const auto playerRefused = ParkedAck(Request(host, 10, consolePacket, 2.1), 10);
		assert(playerRefused && playerRefused->at("ok") == false && playerRefused->at("reason") == "notInFrame");
		(void)Request(host, 10, ParkedFrameState("0010A0FF"), 2.15);
		const auto playerObserved = ParkedAck(Request(host, 10, consolePacket, 2.2), 10);
		assert(playerObserved && playerObserved->at("ok") == true);
		const auto adminNoFrame = ParkedAck(Request(host, 20, ParkedPacket("00000002", "0010A0FF"), 2.3), 20);
		assert(adminNoFrame && adminNoFrame->at("ok") == false && adminNoFrame->at("reason") == "notInFrame");
		auto adminPacket = ParkedPacket("00000003", "0010A0FF");
		adminPacket["console"] = true;
		const auto adminAccepted = ParkedAck(Request(host, 20, adminPacket, 2.4), 20);
		assert(adminAccepted && adminAccepted->at("ok") == true);
	}

	void TestParkedSuitCapEvictionBroadcastsRemove()
	{
		ProtocolHost host{ "Parked Cap Eviction Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 98, "profile-observer");
		ParkedJoin(host, 99, "profile-live");
		Move(host, 99, 2.0, 0.0);
		(void)Request(host, 99, ParkedFrameState("0010A0FF"), 2.1);
		for (std::uint32_t owner = 0; owner < 64; ++owner) {
			const auto connection = 200 + owner;
			const auto profile = "offline-" + std::to_string(owner);
			ParkedJoin(host, connection, profile.c_str());
			Move(host, connection, 3.0, 0.0);
			(void)Request(host, connection, ParkedFrameState("0010A0FF"), 3.1);
			for (std::uint32_t suit = 1; suit <= 4; ++suit) {
				const auto ack = ParkedAck(Request(host, connection, ParkedPacket(Hex8(owner * 4 + suit).c_str(), "0010A0FF"), 3.2), connection);
				assert(ack && ack->at("ok") == true);
			}
			(void)host.HandleDisconnected(connection, 100.5);
		}
		const auto evicting = Request(host, 99, ParkedPacket("00000001", "0010A0FF"), 2.2);
		const auto ack = ParkedAck(evicting, 99);
		assert(ack && ack->at("ok") == true);
		std::size_t removes = 0;
		for (const auto& packet : FramesOfType(evicting, 98, "parkedSuit")) {
			if (packet.value("action", std::string{}) != "remove") continue;
			assert(packet.size() == 4 && packet.at("ownerKey").get<std::string>().size() == 64 && packet.at("suitId").get<std::string>().size() == 8);
			++removes;
		}
		assert(removes == 1);
	}

	void TestParkedSuitSweepBroadcastsRemove()
	{
		const double retention = 14.0 * 24.0 * 60.0 * 60.0;
		ProtocolHost host{ "Parked Sweep Broadcast Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		ParkedJoin(host, 20, "profile-b");
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 0.0);
		(void)Request(host, 10, ParkedFrameState("0010A0FF"), 2.1);
		const auto parkFrames = Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 2.2);
		const auto ack = ParkedAck(parkFrames, 10);
		assert(ack && ack->at("ok") == true);
		const auto suitId = RequirePacket(parkFrames, 10, "parkedSuit").at("suitId").get<std::string>();
		(void)host.PollDurablePlayerState(3.0, 1000.0);
		(void)host.HandleDisconnected(10, 1001.0);
		const auto swept = host.PollDurablePlayerState(4000.0, 1000.0 + retention + 1.0);
		std::size_t removes = 0;
		for (const auto& packet : FramesOfType(swept, 20, "parkedSuit")) {
			if (packet.value("action", std::string{}) != "remove" || packet.value("suitId", std::string{}) != suitId) continue;
			assert(packet.size() == 4 && packet.at("ownerKey").get<std::string>().size() == 64);
			++removes;
		}
		assert(removes == 1);
	}

	void TestParkedSuitBreachCooldownAndOfflineLength()
	{
		ProtocolHost host{ "Parked Breach Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(host, 10, "profile-a");
		ParkedJoin(host, 20, "profile-b");
		Move(host, 10, 2.0, 0.0);
		Move(host, 20, 2.0, 0.0);
		(void)Request(host, 10, ParkedFrameState("0010A0FF"), 2.1);
		const auto parked = ParkedAck(Request(host, 10, ParkedPacket("00000001", "0010A0FF"), 2.2), 10);
		assert(parked && parked->at("ok") == true);
		(void)host.HandleDisconnected(10, 100.5);
		Move(host, 20, 4.0, 0.0, "100", "F00D", "normal");
		const auto first = FramesOfType(Request(host, 20, ParkedSuitAction("breachStart", "00000001"), 4.1), 20, "parkedSuit");
		assert(first.size() == 1 && first.front().at("action") == "breachChallenge" && first.front().at("length") == 4);
		assert(Request(host, 20, Json{ { "type", "parkedSuit" }, { "action", "hack" }, { "suitId", "00000001" }, { "picks", Json::array() } }, 4.2).empty());
		assert(ParkedChallengeCount(Request(host, 20, ParkedSuitAction("breachStart", "00000001"), 4.3), 20) == 0);
		Move(host, 20, 35.1, 0.0, "100", "F00D", "normal");
		assert(ParkedChallengeCount(Request(host, 20, ParkedSuitAction("breachStart", "00000001"), 35.2), 20) == 1);
	}

	void TestParkedSuitTtlEvictionOnlyOffline()
	{
		const double retention = 14.0 * 24.0 * 60.0 * 60.0;
		ProtocolHost offline{ "Parked TTL Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(offline, 10, "profile-a");
		ParkedJoin(offline, 20, "profile-b");
		Move(offline, 10, 2.0, 0.0);
		Move(offline, 20, 2.0, 0.0);
		(void)Request(offline, 10, ParkedFrameState("0010A0FF"), 2.1);
		const auto offlineParked = ParkedAck(Request(offline, 10, ParkedPacket("00000001", "0010A0FF"), 2.2), 10);
		assert(offlineParked && offlineParked->at("ok") == true);
		(void)offline.PollDurablePlayerState(3.0, 1000.0);
		(void)offline.HandleDisconnected(10, 1001.0);
		(void)offline.PollDurablePlayerState(4000.0, 1000.0 + retention + 1.0);
		Move(offline, 20, 4000.5, 0.0, "100", "F00D", "normal");
		assert(ParkedChallengeCount(Request(offline, 20, ParkedSuitAction("breachStart", "00000001"), 4000.6), 20) == 0);

		ProtocolHost online{ "Parked TTL Online Test", {}, {}, true, false, std::string(64, 'k') };
		ParkedJoin(online, 10, "profile-a");
		ParkedJoin(online, 20, "profile-b");
		Move(online, 10, 2.0, 0.0);
		Move(online, 20, 2.0, 0.0);
		(void)Request(online, 10, ParkedFrameState("0010A0FF"), 2.1);
		const auto onlineParked = ParkedAck(Request(online, 10, ParkedPacket("00000001", "0010A0FF"), 2.2), 10);
		assert(onlineParked && onlineParked->at("ok") == true);
		(void)online.PollDurablePlayerState(3.0, 1000.0);
		(void)online.PollDurablePlayerState(4000.0, 1000.0 + retention + 1.0);
		Move(online, 20, 4000.5, 0.0, "100", "F00D", "normal");
		assert(ParkedChallengeCount(Request(online, 20, ParkedSuitAction("breachStart", "00000001"), 4000.6), 20) == 1);
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
	TestTerminalCapabilityGate();
	TestMovementGates();
	TestReplayAndConflict();
	TestLockFlow();
	TestRateLimit();
	TestSnapshotOnJoin();
	TestSnapshotOnExteriorInterestEntry();
	TestNoScanBelowStepAndEntryAfterStep();
	TestTinyStepsEnterAfterCumulativeStep();
	TestScanPointMarginResendsBandObject();
	TestSnapshotChunking();
	TestPerPlayerCap();
	TestMalformedRequests();
	TestParkedSuitFrameGate();
	TestParkedSuitFrameObservedFromTransform();
	TestParkedSuitRejectedTransformDoesNotObserve();
	TestParkedSuitBreachCooldownAndOfflineLength();
	TestParkedSuitTtlEvictionOnlyOffline();
	TestParkedSuitFrameObservationSurvivesReconnect();
	TestParkedSuitFrameObservationRefreshesAfterSecond();
	TestParkedSuitConsoleSpawnNeedsAdmin();
	TestParkedSuitCapEvictionBroadcastsRemove();
	TestParkedSuitSweepBroadcastsRemove();
	return 0;
}
