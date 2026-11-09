#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TSteamId.h"
#include "F4TWorldPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	Json DecodeSnapshot(const OutboundFrame& a_frame, Family a_expectedFamily, std::uint32_t a_expectedSequence)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		assert(F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == DecodeResult::Valid);
		assert(header.family == a_expectedFamily);
		assert(header.sequence == a_expectedSequence);
		return DecodePacket(a_frame);
	}

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			if (DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	const OutboundFrame& RequireFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) {
				return frame;
			}
		}
		assert(false && "required outbound frame was not found");
		return a_frames.front();
	}

	void AssertNoAuthorityTransfer(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_expectedOwner,
		std::uint32_t a_expectedEpoch)
	{
		for (const auto& frame : a_frames) {
			if (frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) != "npcAuthority") {
				continue;
			}
			assert(packet.at("authorityPlayerId").get<std::uint32_t>() == a_expectedOwner);
			assert(packet.at("authorityEpoch").get<std::uint32_t>() == a_expectedEpoch);
		}
	}

	void RequireIncumbentAnnouncement(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_expectedOwner,
		std::uint32_t a_expectedEpoch)
	{
		bool sawAnnouncement = false;
		for (const auto& frame : a_frames) {
			if (frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) != "npcAuthority") {
				continue;
			}
			sawAnnouncement = true;
			assert(packet.at("authorityPlayerId").get<std::uint32_t>() == a_expectedOwner);
			assert(packet.at("authorityEpoch").get<std::uint32_t>() == a_expectedEpoch);
		}
		assert(sawAnnouncement && "expected an incumbent owner-announcement frame on scope entry (no transfer)");
	}

	std::vector<Json> LifecyclePackets(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == "sessionState") {
				packets.push_back(packet);
			}
		}
		return packets;
	}

	void JoinReady(CommonwealthOnline::Server::ProtocolHost& a_host, std::uint32_t a_connectionId, double a_time)
	{
		a_host.HandleConnected(a_connectionId, a_time);
		a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_time, a_time);
	}

	std::vector<Json> PlayerStatesOf(const std::vector<OutboundFrame>& a_frames)
	{
		std::vector<Json> states;
		for (const auto& frame : a_frames) {
			if (!frame.payload.empty() && DecodePacket(frame).value("type", std::string{}) == "playerState") {
				states.push_back(DecodePacket(frame));
			}
		}
		return states;
	}

	void TestDurableStateTrailingFlush()
	{
		CommonwealthOnline::Server::ProtocolHost host{ "Durable Flush", "relay" };
		JoinReady(host, 100, 1.0);
		JoinReady(host, 200, 1.1);
		const auto sendName = [&](double a_time, std::string_view a_name, double a_height) {
			return PlayerStatesOf(host.HandleMessage(100, EncodeJson(Json{ { "type", "playerState" }, { "characterName", std::string{ a_name } }, { "appearance", Json{ { "raceFormId", "14" }, { "height", a_height } } } }), a_time, a_time));
		};

		const auto first = sendName(1.0, "Alpha", 1.0);
		assert(first.size() == 1);
		assert(first.front().at("displayName") == "Stranger");
		assert(first.front().value("characterName", std::string{ "Stranger" }) == "Stranger");
		assert(sendName(1.1, "Bravo", 1.25).empty());
		assert(sendName(1.2, "Charlie", 1.5).empty());
		assert(host.PollDurablePlayerState(1.3, 1000.3).empty());
		const auto flushFrames = host.PollDurablePlayerState(1.7, 1000.7);
		assert(flushFrames.size() == 1);
		assert(flushFrames.front().connectionId == 200);
		const auto flushed = DecodePacket(flushFrames.front());
		assert(flushed.at("type") == "playerState");
		assert(flushed.at("appearance").at("height") == 1.5);
		assert(flushed.at("displayName") == "Stranger");
		assert(flushed.value("characterName", std::string{ "Stranger" }) == "Stranger");
		assert(flushed.at("playerId") == first.front().at("playerId"));
		assert(!flushed.contains("vitals") && !flushed.contains("actionEvents"));
		assert(host.PollDurablePlayerState(1.8, 1000.8).empty());

		assert(sendName(2.5, "Delta", 1.0).size() == 1);
		assert(sendName(2.6, "Echo", 1.25).empty());
		assert(sendName(2.7, "Delta", 1.0).empty());
		assert(host.PollDurablePlayerState(3.2, 1001.2).empty());
	}

	void TestFailedPlayerStateRelayIsResent()
	{
		CommonwealthOnline::Server::ProtocolHost host{ "Relay Resend", "relay" };
		JoinReady(host, 100, 1.0);
		JoinReady(host, 200, 1.1);
		JoinReady(host, 300, 1.2);
		const auto first = host.HandleMessage(100, EncodeJson(Json{ { "type", "playerState" }, { "characterName", "Alpha" }, { "appearance", Json{ { "raceFormId", "14" }, { "height", 1.0 } } } }), 1.0, 1.0);
		const auto* refused = FindFrame(first, 200, "playerState");
		const auto* delivered = FindFrame(first, 300, "playerState");
		assert(refused != nullptr && delivered != nullptr);
		host.FrameDeliveryCompleted(*refused, false);
		host.FrameDeliveryCompleted(*delivered, true);

		const auto poll = host.PollDurablePlayerState(1.6, 1000.6);
		const auto* resent = FindFrame(poll, 200, "playerState");
		assert(resent != nullptr);
		assert(DecodePacket(*resent).at("appearance").at("height") == 1.0);
		assert(FindFrame(poll, 300, "playerState") != nullptr);
		host.FrameDeliveryCompleted(*resent, true);
		assert(host.PollDurablePlayerState(1.8, 1000.8).empty());
	}

	void TestProgressionIsNotRelayed()
	{
		CommonwealthOnline::Server::ProtocolHost host{ "Progression Relay", "relay" };
		JoinReady(host, 100, 1.0);
		JoinReady(host, 200, 1.1);
		const Json progression{
			{ "special", Json{ { "strength", 5 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", 2 },
			{ "experience", 200 },
			{ "perks", Json::array() }
		};
		const auto sendState = [&](double a_time, const Json& a_packet) {
			return PlayerStatesOf(host.HandleMessage(100, EncodeJson(a_packet), a_time, a_time));
		};

		assert(sendState(1.2, Json{ { "type", "playerState" }, { "progression", progression } }).empty());

		const auto named = sendState(2.0, Json{ { "type", "playerState" }, { "characterName", "Alpha" }, { "progression", progression } });
		assert(named.size() == 1);
		assert(named.front().at("displayName") == "Stranger");
		assert(!named.front().contains("progression"));

		assert(sendState(2.1, Json{ { "type", "playerState" }, { "characterName", "Bravo" }, { "progression", progression } }).empty());
		const auto flushFrames = host.PollDurablePlayerState(2.6, 2.6);
		assert(flushFrames.size() == 1);
		assert(!DecodePacket(flushFrames.front()).contains("progression"));
	}

	void TestInvalidProgressionIsReported()
	{
		CommonwealthOnline::Server::ProtocolHost host{ "Progression Reject", "reject" };
		JoinReady(host, 100, 1.0);
		const Json invalid{
			{ "special", Json{ { "strength", 5 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", 0 },
			{ "experience", 200 },
			{ "perks", Json::array() }
		};
		const auto frames = host.HandleMessage(100, EncodeJson(Json{ { "type", "playerState" }, { "progression", invalid } }), 1.2, 1.2);
		assert(frames.size() == 1);
		const auto rejected = DecodePacket(frames.front());
		assert(rejected.at("type") == "progressionRejected");
		assert(rejected.at("reason") == "invalid");
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	{
		ProtocolHost ticketHost{ "Ticket", "Hello size" };
		ticketHost.HandleConnected(1, 1000.0);
		ticketHost.BindSessionProfile(1, "ticket-profile");
		const Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) },
			{ "steamTicket", std::string(F4T::SteamId::kMaximumTicketBytes * 2, 'a') },
			{ "profileSecret", std::string(512, 'b') }, { "serverPassword", std::string(256, 'c') },
			{ "modpackFingerprint", std::string(512, 'd') }, { "resumeToken", std::string(256, 'e') } };
		assert(hello.dump().size() < 8 * 1024);
		const auto ready = ticketHost.HandleMessage(1, EncodeJson(hello), 1.0, 1000.1);
		assert(!ready.empty() && RequireFrame(ready, 1, "sessionReady").payload.size() > 0);
	}

	ProtocolHost host{ "Core Test Server", "C++ protocol proof" };
	assert(host.EnableSocial(nullptr));

	const auto welcomeA = host.HandleConnected(100, 1000.0);
	host.BindSessionProfile(100, "proto-a");
	assert(welcomeA.size() == 1);
	assert(LifecyclePackets(welcomeA, 100).empty());
	assert(welcomeA[0].connectionId == 100);
	assert(welcomeA[0].delivery == Delivery::ReliableOrdered);
	const auto welcomePacketA = DecodePacket(welcomeA[0]);
	assert(welcomePacketA.at("type") == "welcome");
	assert(welcomePacketA.at("protocolVersion") == 2);
	assert(welcomePacketA.at("serverName") == "Core Test Server");
	assert(!welcomePacketA.contains("serverInstanceId"));
	{
		ProtocolHost durableHost{ "Durable", "World id" };
		durableHost.SetServerInstanceId("0123456789abcdef0123456789abcdef");
		const auto durableWelcome = DecodePacket(durableHost.HandleConnected(1, 1000.0).at(0));
		assert(durableWelcome.at("serverInstanceId") == "0123456789abcdef0123456789abcdef");
	}
	const auto playerA = welcomePacketA.at("playerId").get<std::uint32_t>();
	assert(playerA != 0);
	const auto readyA = host.HandleMessage(
		100,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 },
			{ "capabilities", Json::array({ "session-lifecycle-v1", std::string{ F4T::WorldPolicy::kCapabilityName }, "social-names-v1" }) } }),
		1.0,
		1000.1);
	const auto& readyFrameA = RequireFrame(readyA, 100, "sessionReady");
	assert(DecodePacket(readyFrameA).at("playerId") == playerA);
	assert(DecodePacket(readyFrameA).at("consoleAllowed") == false);
	const auto lifecycleA = LifecyclePackets(readyA, 100);
	assert(lifecycleA.size() == 2);
	assert(lifecycleA[0].at("state") == "connected");
	assert(lifecycleA[0].at("spawnable") == false);
	assert(lifecycleA[0].at("playerId") == playerA);
	assert(lifecycleA[1].at("state") == "ready");
	assert(lifecycleA[1].at("spawnable") == true);
	const auto& hostFrameA = RequireFrame(readyA, 100, "worldStateHost");
	assert(DecodePacket(hostFrameA).at("worldStateHostPlayerId") == playerA);
	assert(host.ActiveCount() == 1);

	const auto welcomeB = host.HandleConnected(200, 1000.2);
	host.BindSessionProfile(200, "proto-b");
	const auto welcomePacketB = DecodePacket(welcomeB[0]);
	const auto playerB = welcomePacketB.at("playerId").get<std::uint32_t>();
	assert(playerB != 0 && playerB != playerA);
	const auto readyB = host.HandleMessage(
		200,
		EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 },
			{ "capabilities", Json::array({ std::string{ F4T::WorldPolicy::kCapabilityName }, "social-names-v1" }) } }),
		1.1,
		1000.3);
	assert(RequireFrame(readyB, 200, "sessionReady").payload.size() > 0);
	assert(LifecyclePackets(readyB, 200).empty());
	assert(readyB.size() == 3);
	assert(DecodePacket(RequireFrame(readyB, 200, "campList")).at("camps").empty());
	assert(DecodePacket(RequireFrame(readyB, 200, "worldPolicy")).at("mode") == "coop-story");
	assert(host.ActiveCount() == 2);

	const Json spawnTransform{
		{ "type", "transform" },
		{ "cellId", "CAFE" },
		{ "worldspaceId", "F00D" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 25.0 },
		{ "movementType", "spawn" },
		{ "movementSpeed", -5.0 },
		{ "animationDirection", 720.0 },
		{ "isSprinting", "invalid" },
		{ "actorStateFlags1", -1 },
		{ "pipboyLight", Json{ { "on", true }, { "color", 0x80FF20 } } },
		{ "unknownTelemetry", "preserve-me" },
		{ "characterName", "must-not-leak-on-transform" }
	};
	const auto spawnRelay = host.HandleMessage(
		100,
		EncodeSnapshot(spawnTransform, Family::Transform, 50),
		2.0,
		1001.0);
	// Connection 200 has not reported a position, so the transform is withheld
	// from it; the authority broadcast is not interest-scoped.
	assert(FindFrame(spawnRelay, 200, "transform") == nullptr);

	const auto firstAuthority = DecodePacket(RequireFrame(spawnRelay, 200, "npcAuthority"));
	assert(firstAuthority.at("authorityPlayerId") == playerA);
	assert(firstAuthority.at("authorityEpoch") == 1);
	assert(firstAuthority.at("authorityCellId") == "0000CAFE");
	assert(firstAuthority.at("authorityWorldspaceId") == "0000F00D");

	Json spawnTransformB = spawnTransform;
	spawnTransformB["x"] = 10.0;
	const auto scopeRelayB = host.HandleMessage(
		200,
		EncodeSnapshot(spawnTransformB, Family::Transform, 1),
		2.05,
		1000.5);
	const auto authorityForB = DecodePacket(RequireFrame(scopeRelayB, 200, "npcAuthority"));
	assert(authorityForB.at("authorityPlayerId") == playerA);
	assert(authorityForB.at("authorityEpoch") == 1);

	// Now that 200 has a position it receives the replay of peers in range.
	const auto& spawnFrame = RequireFrame(scopeRelayB, 200, "transform");
	assert(spawnFrame.delivery == Delivery::UnreliableSequenced);
	const auto relayedSpawn = DecodeSnapshot(spawnFrame, Family::Transform, 1);
	assert(relayedSpawn.at("playerId") == playerA);
	assert(relayedSpawn.at("cellId") == "0000CAFE");
	assert(relayedSpawn.at("worldspaceId") == "0000F00D");
	assert(relayedSpawn.at("pipboyLight").at("on") == true);
	assert(relayedSpawn.at("pipboyLight").at("color") == 0x80FF20);
	assert(!relayedSpawn.contains("movementSpeed"));
	assert(!relayedSpawn.contains("animationDirection"));
	assert(!relayedSpawn.contains("isSprinting"));
	assert(!relayedSpawn.contains("actorStateFlags1"));
	assert(!relayedSpawn.contains("unknownTelemetry"));
	assert(!relayedSpawn.contains("characterName"));

	const auto beforeStale = host.Stats();
	const auto staleRelay = host.HandleMessage(
		100,
		EncodeSnapshot(spawnTransform, Family::Transform, 49),
		2.1,
		1001.1);
	assert(staleRelay.empty());
	assert(host.Stats().snapshotsRejected == beforeStale.snapshotsRejected + 1);

	Json impossibleTransform = spawnTransform;
	impossibleTransform["movementType"] = "normal";
	impossibleTransform["x"] = 10'000.0;
	const auto beforeMovementReject = host.Stats();
	const auto impossibleRelay = host.HandleMessage(
		100,
		EncodeSnapshot(impossibleTransform, Family::Transform, 51),
		2.1,
		1001.2);
	assert(impossibleRelay.size() == 1);
	const auto& correctionFrame = RequireFrame(impossibleRelay, 100, "positionCorrection");
	assert(correctionFrame.delivery == Delivery::ReliableOrdered);
	const auto correction = DecodePacket(correctionFrame);
	assert(correction.at("x") == 0.0);
	assert(correction.at("y") == 0.0);
	assert(correction.at("z") == 0.0);
	assert(correction.at("angleZ") == 25.0);
	assert(correction.at("cellId") == "0000CAFE");
	assert(correction.at("worldspaceId") == "0000F00D");
	assert(correction.at("reason").get<std::string>().find("exceeded server envelope") != std::string::npos);
	assert(host.Stats().movementRejected == beforeMovementReject.movementRejected + 1);
	assert(host.Stats().movementCorrectionsSent == beforeMovementReject.movementCorrectionsSent + 1);

	Json validTransform = spawnTransform;
	validTransform["movementType"] = "normal";
	validTransform["x"] = 100.0;
	const auto validRelay = host.HandleMessage(
		100,
		EncodeSnapshot(validTransform, Family::Transform, 52),
		2.2,
		1001.3);
	const auto& validFrame = RequireFrame(validRelay, 200, "transform");
	const auto relayedMove = DecodeSnapshot(validFrame, Family::Transform, 2);
	assert(relayedMove.at("x") == 100.0);

	Json backwardsTransform = spawnTransform;
	backwardsTransform["movementType"] = "normal";
	backwardsTransform["x"] = 100.0;
	const auto backwardsRelay = host.HandleMessage(
		100,
		EncodeSnapshot(backwardsTransform, Family::Transform, 53),
		1.5,
		1001.32);
	assert(FindFrame(backwardsRelay, 200, "transform") == nullptr);
	const auto backwardsCorrection = DecodePacket(RequireFrame(backwardsRelay, 100, "positionCorrection"));
	assert(backwardsCorrection.at("reason").get<std::string>() == "movement timestamp is invalid or moved backwards");

	// A packet that fails semantic validation must not spend its snapshot
	// sequence, or a client could poison the sender's stream with one bad
	// high-sequence packet and have every later valid snapshot dropped as stale.
	Json poisonTransform = spawnTransform;
	poisonTransform["movementType"] = "normal";
	poisonTransform["x"] = 9'000'000.0;
	const auto poisoned = host.HandleMessage(
		100,
		EncodeSnapshot(poisonTransform, Family::Transform, 200),
		2.25,
		1001.35);
	assert(FindFrame(poisoned, 200, "transform") == nullptr);

	Json afterPoison = spawnTransform;
	afterPoison["movementType"] = "normal";
	afterPoison["x"] = 150.0;
	const auto acceptedAfterPoison = host.HandleMessage(
		100,
		EncodeSnapshot(afterPoison, Family::Transform, 53),
		2.26,
		1001.36);
	const auto* afterPoisonFrame = FindFrame(acceptedAfterPoison, 200, "transform");
	assert(afterPoisonFrame != nullptr);
	assert(DecodeSnapshot(*afterPoisonFrame, Family::Transform, 3).at("x") == 150.0);

	const auto stateRelay = host.HandleMessage(
		100,
		EncodeJson(Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad" },
			{ "appearance", Json{
				{ "raceFormId", "14" },
				{ "height", 1.0 },
				{ "serverOnlyAppearanceInjection", "must-not-relay" }
			} },
			{ "serverOnlyInjection", "must-not-relay" }
		}),
		2.3,
		1001.4);
	const auto& stateFrame = RequireFrame(stateRelay, 200, "playerState");
	assert(stateFrame.delivery == Delivery::ReliableOrdered);
	const auto relayedState = DecodePacket(stateFrame);
	assert(relayedState.at("playerId") == playerA);
	assert(relayedState.at("displayName") == "Stranger");
	assert(!relayedState.contains("characterName"));
	assert(relayedState.at("appearance").at("version") == 4);
	assert(relayedState.at("appearance").at("raceFormId") == "00000014");
	assert(!relayedState.at("appearance").contains("serverOnlyAppearanceInjection"));
	assert(!relayedState.contains("serverOnlyInjection"));

	const auto beforeInvalidAppearance = host.Stats();
	const auto invalidAppearanceRelay = host.HandleMessage(
		100,
		EncodeJson(Json{
			{ "type", "playerState" },
			{ "appearance", Json{ { "height", 4.1 } } }
		}),
		2.31,
		1001.41);
	assert(invalidAppearanceRelay.empty());
	assert(host.Stats().packetsRejected == beforeInvalidAppearance.packetsRejected + 1);

	const auto actionStateRelay = host.HandleMessage(
		100,
		EncodeJson(Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad" },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", 1 },
				{ "type", 3 },
				{ "eventName", "fireSingle" },
				{ "actorStateFlags1", "invalid-optional" }
			} }) }
		}),
		2.45,
		1001.55);
	const auto actionState = DecodePacket(RequireFrame(actionStateRelay, 200, "playerState"));
	assert(actionState.at("actionEvents").size() == 1);
	assert(!actionState.at("actionEvents")[0].contains("actorStateFlags1"));

	const Json npcState{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "CAFE" },
		{ "authorityWorldspaceId", "F00D" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", 15.0 },
			{ "y", 25.0 },
			{ "z", 35.0 },
			{ "angleZ", 45.0 }
		} }) }
	};

	const auto beforeUnauthorized = host.Stats();
	const auto unauthorizedNpc = host.HandleMessage(
		200,
		EncodeSnapshot(npcState, Family::NpcState, 1),
		2.5,
		1001.6);
	assert(unauthorizedNpc.empty());
	assert(host.Stats().npcAuthorityRejected == beforeUnauthorized.npcAuthorityRejected + 1);

	const auto authorizedNpc = host.HandleMessage(
		100,
		EncodeSnapshot(npcState, Family::NpcState, 1),
		2.6,
		1001.7);
	const auto& npcFrame = RequireFrame(authorizedNpc, 200, "npcState");
	assert(npcFrame.delivery == Delivery::UnreliableSequenced);
	const auto relayedNpc = DecodeSnapshot(npcFrame, Family::NpcState, 1);
	assert(relayedNpc.at("playerId") == playerA);
	assert(relayedNpc.at("authorityEpoch") == 1);
	assert(relayedNpc.at("fullReplace") == true);
	assert(relayedNpc.at("npcs")[0].at("sourceFormId") == "00001234");
	assert(relayedNpc.at("npcs")[0].at("cellId") == "0000CAFE");

	Json outsideScopeNpc = npcState;
	outsideScopeNpc["npcs"][0]["cellId"] = "BEEF";
	const auto beforeOutsideScope = host.Stats();
	const auto outsideScopeRelay = host.HandleMessage(
		100,
		EncodeSnapshot(outsideScopeNpc, Family::NpcState, 2),
		2.7,
		1001.8);
	assert(outsideScopeRelay.empty());
	assert(host.Stats().npcAuthorityRejected == beforeOutsideScope.npcAuthorityRejected + 1);

	const auto disconnectFrames = host.HandleDisconnected(100, 1002.0);
	assert(host.ConnectedCount() == 1);
	assert(host.ActiveCount() == 1);
	const auto handoff = DecodePacket(RequireFrame(disconnectFrames, 200, "npcAuthority"));
	assert(handoff.at("authorityPlayerId") == playerB);
	assert(handoff.at("authorityEpoch") == 2);
	assert(handoff.at("authorityCellId") == "0000CAFE");
	assert(DecodePacket(RequireFrame(disconnectFrames, 200, "disconnect")).at("playerId") == playerA);
	assert(DecodePacket(RequireFrame(disconnectFrames, 200, "worldStateHost")).at("worldStateHostPlayerId") == playerB);

	const auto beforeOldEpoch = host.Stats();
	const auto staleOwnerNpc = host.HandleMessage(
		200,
		EncodeSnapshot(npcState, Family::NpcState, 2),
		2.8,
		1002.1);
	assert(staleOwnerNpc.empty());
	assert(host.Stats().npcAuthorityRejected == beforeOldEpoch.npcAuthorityRejected + 1);

	Json newOwnerNpc = npcState;
	newOwnerNpc["authorityEpoch"] = 2;
	const auto beforeNewOwner = host.Stats();
	const auto acceptedNewOwner = host.HandleMessage(
		200,
		EncodeSnapshot(newOwnerNpc, Family::NpcState, 3),
		2.9,
		1002.2);
	assert(acceptedNewOwner.empty());
	assert(host.Stats().npcStateRejected == beforeNewOwner.npcStateRejected);
	assert(host.Stats().packetsRejected == beforeNewOwner.packetsRejected);

	{
		ProtocolHost sticky{ "Hysteresis Regression", "authority stickiness" };

		const auto stickyLowWelcome = sticky.HandleConnected(100, 2000.0);
		const auto stickyLowId = DecodePacket(stickyLowWelcome[0]).at("playerId").get<std::uint32_t>();
		sticky.HandleMessage(100, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.1, 2000.1);

		const auto stickyHighWelcome = sticky.HandleConnected(200, 2000.2);
		const auto stickyHighId = DecodePacket(stickyHighWelcome[0]).at("playerId").get<std::uint32_t>();
		sticky.HandleMessage(200, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.2, 2000.3);
		assert(stickyLowId < stickyHighId);

		const Json scopeSTransform{
			{ "type", "transform" }, { "cellId", "D00D" }, { "worldspaceId", "F00D" },
			{ "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 },
			{ "movementType", "spawn" }, { "movementSpeed", 0.0 }
		};

		const auto highOwnsScope = sticky.HandleMessage(200, EncodeSnapshot(scopeSTransform, Family::Transform, 1), 1.0, 2001.0);
		const auto ownershipAuthority = DecodePacket(RequireFrame(highOwnsScope, 100, "npcAuthority"));
		assert(ownershipAuthority.at("authorityPlayerId") == stickyHighId);
		assert(ownershipAuthority.at("authorityEpoch") == 1);
		assert(ownershipAuthority.at("authorityCellId") == "0000D00D");

		const Json scopeSNpc{
			{ "type", "npcState" }, { "authorityEpoch", 1 }, { "authorityCellId", "D00D" }, { "authorityWorldspaceId", "F00D" },
			{ "npcs", Json::array({ Json{
				{ "sourceFormId", "1234" }, { "cellId", "D00D" }, { "worldspaceId", "F00D" },
				{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 4.0 }
			} }) }
		};
		(void)sticky.HandleMessage(200, EncodeSnapshot(scopeSNpc, Family::NpcState, 1), 1.1, 2001.1);

		Json lowerJoinsScope = scopeSTransform;
		lowerJoinsScope["x"] = 5.0;
		const auto lowerJoinRelay = sticky.HandleMessage(100, EncodeSnapshot(lowerJoinsScope, Family::Transform, 1), 1.2, 2001.2);
		RequireIncumbentAnnouncement(lowerJoinRelay, stickyHighId, 1);

		const auto observerWelcome = sticky.HandleConnected(300, 2001.3);
		(void)DecodePacket(observerWelcome[0]).at("playerId");
		sticky.HandleMessage(300, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.3, 2001.35);
		Json observerEntersScope = scopeSTransform;
		observerEntersScope["x"] = 7.0;
		const auto observerEnterRelay = sticky.HandleMessage(300, EncodeSnapshot(observerEntersScope, Family::Transform, 1), 1.4, 2001.4);
		AssertNoAuthorityTransfer(observerEnterRelay, stickyHighId, 1);
		const auto cachedNpcForObserver = DecodePacket(RequireFrame(observerEnterRelay, 300, "npcState"));
		assert(cachedNpcForObserver.at("npcs")[0].at("cellId") == "0000D00D");

		const auto ownerLeaveRelay = sticky.HandleDisconnected(200, 2002.0);
		const auto handoffAuthority = DecodePacket(RequireFrame(ownerLeaveRelay, 100, "npcAuthority"));
		assert(handoffAuthority.at("authorityPlayerId") == stickyLowId);
		assert(handoffAuthority.at("authorityEpoch") == 2);
	}

	{
		ProtocolHost session{ "Four Player Session", "4-player representation verification" };
		assert(session.EnableSocial(nullptr));
		const auto welcome1 = session.HandleConnected(10, 3000.0);
		session.BindSessionProfile(10, "proto-c1");
		const auto player1 = DecodePacket(welcome1.front()).at("playerId").get<std::uint32_t>();
		session.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.1, 3000.1);

		const auto welcome2 = session.HandleConnected(20, 3000.2);
		session.BindSessionProfile(20, "proto-c2");
		const auto player2 = DecodePacket(welcome2.front()).at("playerId").get<std::uint32_t>();
		session.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.2, 3000.3);

		const auto welcome3 = session.HandleConnected(30, 3000.4);
		session.BindSessionProfile(30, "proto-c3");
		const auto player3 = DecodePacket(welcome3.front()).at("playerId").get<std::uint32_t>();
		session.HandleMessage(30, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.3, 3000.5);

		const auto welcome4 = session.HandleConnected(40, 3000.6);
		session.BindSessionProfile(40, "proto-c4");
		const auto player4 = DecodePacket(welcome4.front()).at("playerId").get<std::uint32_t>();
		session.HandleMessage(40, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 0.4, 3000.7);

		assert(player1 != player2 && player1 != player3 && player1 != player4);
		assert(player2 != player3 && player2 != player4);
		assert(player3 != player4);
		assert(session.ActiveCount() == 4);

		const Json spawnBase{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};

		Json spawn1 = spawnBase; spawn1["x"] = 0.0; spawn1["y"] = 0.0;
		Json spawn2 = spawnBase; spawn2["x"] = 100.0; spawn2["y"] = 0.0;
		Json spawn3 = spawnBase; spawn3["x"] = 0.0; spawn3["y"] = 100.0;
		Json spawn4 = spawnBase; spawn4["x"] = 100.0; spawn4["y"] = 100.0;

		session.HandleMessage(10, EncodeSnapshot(spawn1, Family::Transform, 1), 1.0, 3001.0);
		session.HandleMessage(20, EncodeSnapshot(spawn2, Family::Transform, 1), 1.1, 3001.1);
		session.HandleMessage(30, EncodeSnapshot(spawn3, Family::Transform, 1), 1.2, 3001.2);
		session.HandleMessage(40, EncodeSnapshot(spawn4, Family::Transform, 1), 1.3, 3001.3);

		Json move1 = spawn1;
		move1["movementType"] = "normal";
		move1["x"] = 10.0;
		const auto moveRelay1 = session.HandleMessage(10, EncodeSnapshot(move1, Family::Transform, 2), 2.0, 3002.0);
		const auto* frame2For1 = FindFrame(moveRelay1, 20, "transform");
		const auto* frame3For1 = FindFrame(moveRelay1, 30, "transform");
		const auto* frame4For1 = FindFrame(moveRelay1, 40, "transform");
		assert(frame2For1 != nullptr && frame3For1 != nullptr && frame4For1 != nullptr);
		assert(DecodePacket(*frame2For1).at("playerId") == player1);
		assert(DecodePacket(*frame3For1).at("playerId") == player1);
		assert(DecodePacket(*frame4For1).at("playerId") == player1);
		assert(DecodePacket(*frame2For1).at("x") == 10.0);

		Json move4 = spawn4;
		move4["movementType"] = "normal";
		move4["y"] = 120.0;
		const auto moveRelay4 = session.HandleMessage(40, EncodeSnapshot(move4, Family::Transform, 2), 2.1, 3002.1);
		const auto* frame1For4 = FindFrame(moveRelay4, 10, "transform");
		const auto* frame2For4 = FindFrame(moveRelay4, 20, "transform");
		const auto* frame3For4 = FindFrame(moveRelay4, 30, "transform");
		assert(frame1For4 != nullptr && frame2For4 != nullptr && frame3For4 != nullptr);
		assert(DecodePacket(*frame1For4).at("playerId") == player4);
		assert(DecodePacket(*frame2For4).at("playerId") == player4);
		assert(DecodePacket(*frame3For4).at("playerId") == player4);
		assert(DecodePacket(*frame1For4).at("y") == 120.0);

		const Json state3{
			{ "type", "playerState" },
			{ "characterName", "SurvivorThree" },
			{ "actionEvents", Json::array() }
		};
		const auto stateRelay3 = session.HandleMessage(30, EncodeJson(state3), 2.2, 3002.2);
		const auto* state1For3 = FindFrame(stateRelay3, 10, "playerState");
		const auto* state2For3 = FindFrame(stateRelay3, 20, "playerState");
		const auto* state4For3 = FindFrame(stateRelay3, 40, "playerState");
		assert(state1For3 != nullptr && state2For3 != nullptr && state4For3 != nullptr);
		assert(DecodePacket(*state1For3).at("playerId") == player3);
		assert(DecodePacket(*state1For3).at("displayName") == "Stranger");
		assert(!DecodePacket(*state1For3).contains("characterName"));
		assert(DecodePacket(*state2For3).at("playerId") == player3);
		assert(DecodePacket(*state4For3).at("playerId") == player3);
	}

	{
		ProtocolHost admin{ "Admin Item Test Server", "admin item proof" };
		const auto welcomeC = admin.HandleConnected(700, 4000.0);
		const auto playerC = DecodePacket(welcomeC[0]).at("playerId").get<std::uint32_t>();
		const auto readyC = admin.HandleMessage(
			700,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "admin-items-v1" }) } }),
			1.0,
			4000.1);
		assert(FindFrame(readyC, 700, "sessionReady") != nullptr);
		assert(admin.HasSessionCapability(700, "admin-items-v1"));

		const auto welcomeD = admin.HandleConnected(701, 4000.2);
		const auto playerD = DecodePacket(welcomeD[0]).at("playerId").get<std::uint32_t>();
		admin.HandleMessage(701, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.1, 4000.3);
		assert(!admin.HasSessionCapability(701, "admin-items-v1"));

		const Json report{
			{ "type", "inventoryReport" },
			{ "opId", 13 },
			{ "truncated", false },
			{ "items", Json::array({ Json{ { "itemId", "0001f4a6" }, { "name", "Stimpak" }, { "count", 3 } } }) }
		};
		assert(admin.TrackAdminOp(700, 13));
		assert(admin.HandleMessage(700, EncodeJson(report), 2.0, 4001.0).empty());
		const auto stored = admin.LatestInventoryReport(playerC);
		assert(stored && stored->opId == 13 && !stored->truncated);
		assert(stored->items.size() == 1 && stored->items[0].at("itemId") == "0001F4A6");
		assert(stored->receivedAtUnixSeconds == 4001.0);

		Json badReport = report;
		badReport["items"][0]["count"] = 0;
		assert(admin.HandleMessage(700, EncodeJson(badReport), 2.1, 4002.0).empty());
		assert(admin.LatestInventoryReport(playerC)->opId == 13);
		assert(admin.Stats().inventoryReportsReceived == 2);
		assert(admin.Stats().inventoryReportsRejected == 1);

		Json untrackedReport = report;
		untrackedReport["opId"] = 99;
		assert(admin.HandleMessage(700, EncodeJson(untrackedReport), 2.15, 4002.1).empty());
		assert(admin.LatestInventoryReport(playerC)->opId == 13);
		assert(admin.Stats().inventoryReportsReceived == 3);
		assert(admin.Stats().inventoryReportsRejected == 2);

		const Json result{ { "type", "adminItemResult" }, { "opId", 12 }, { "ok", true }, { "applied", 5 }, { "error", "" } };
		assert(admin.TrackAdminOp(701, 12));
		assert(admin.HandleMessage(701, EncodeJson(result), 2.2, 4001.5).empty());
		const auto lastResult = admin.LatestAdminItemResult(playerD);
		assert(lastResult && lastResult->opId == 12 && lastResult->ok && lastResult->applied == 5 && lastResult->error.empty());

		Json badResult = result;
		badResult["ok"] = false;
		assert(admin.TrackAdminOp(701, 12));
		assert(admin.HandleMessage(701, EncodeJson(badResult), 2.3, 4001.6).empty());
		assert(admin.LatestAdminItemResult(playerD)->ok);
		assert(admin.TrackAdminOp(701, 12));

		Json untrackedResult = result;
		untrackedResult["opId"] = 77;
		assert(admin.HandleMessage(701, EncodeJson(untrackedResult), 2.4, 4001.7).empty());
		assert(admin.Stats().adminItemResultsReceived == 3);
		assert(admin.Stats().adminItemResultsRejected == 2);

		for (std::uint32_t opId = 1000; opId < 1015; ++opId) {
			assert(admin.TrackAdminOp(701, opId));
		}
		assert(!admin.TrackAdminOp(701, 1015));
		Json oldestResult = result;
		oldestResult["applied"] = 9;
		assert(admin.HandleMessage(701, EncodeJson(oldestResult), 2.5, 4001.8).empty());
		assert(admin.LatestAdminItemResult(playerD)->opId == 12 && admin.LatestAdminItemResult(playerD)->applied == 9);
		assert(admin.Stats().adminItemResultsRejected == 2);
		assert(admin.TrackAdminOp(701, 1015));
		assert(!admin.TrackAdminOp(701, 1016));
		Json refusedResult = result;
		refusedResult["opId"] = 1016;
		assert(admin.HandleMessage(701, EncodeJson(refusedResult), 2.6, 4001.9).empty());
		assert(admin.Stats().adminItemResultsRejected == 3);
		admin.UntrackAdminOp(701, 1015);
		assert(admin.TrackAdminOp(701, 1016));
		Json releasedResult = result;
		releasedResult["opId"] = 1015;
		assert(admin.HandleMessage(701, EncodeJson(releasedResult), 2.7, 4002.2).empty());
		assert(admin.Stats().adminItemResultsRejected == 4);

		admin.HandleDisconnected(700, 4003.0);
		assert(!admin.LatestInventoryReport(playerC).has_value());
	}

	{
		ProtocolHost names{ "Name Test Server", "player name proof" };
		const auto welcomeN = names.HandleConnected(800, 5000.0);
		const auto playerN = DecodePacket(welcomeN[0]).at("playerId").get<std::uint32_t>();
		names.HandleMessage(800, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 5000.1);
		assert(names.PlayerCharacterName(playerN).empty());

		names.HandleMessage(800, EncodeJson(Json{ { "type", "playerState" }, { "characterName", "Nomad\tPrime" } }), 2.0, 5000.2);
		assert(names.PlayerCharacterName(playerN) == "NomadPrime");

		names.HandleMessage(800, EncodeJson(Json{ { "type", "playerState" }, { "characterName", std::string(100, 'x') } }), 2.1, 5000.3);
		assert(names.PlayerCharacterName(playerN) == std::string(64, 'x'));

		names.HandleMessage(800, EncodeJson(Json{ { "type", "playerState" }, { "characterName", std::string(63, 'x') + "\xC3\xA9" } }), 2.2, 5000.4);
		assert(names.PlayerCharacterName(playerN) == std::string(63, 'x'));
		const Json nameJson{ { "n", names.PlayerCharacterName(playerN) } };
		assert(!nameJson.dump().empty());

		names.HandleDisconnected(800, 5001.0);
		assert(names.PlayerCharacterName(playerN).empty());
	}

	{
		ProtocolHost slots{ "Slot Test Server", "admin slot proof" };
		const auto welcomeS = slots.HandleConnected(810, 5100.0);
		const auto playerS = DecodePacket(welcomeS[0]).at("playerId").get<std::uint32_t>();
		slots.HandleMessage(
			810,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "admin-items-v1" }) } }),
			1.0,
			5100.1);
		for (std::uint32_t opId = 1; opId <= 16; ++opId) {
			assert(slots.TrackAdminOp(810, opId));
		}
		assert(!slots.TrackAdminOp(810, 17));

		const Json malformedResult{ { "type", "adminItemResult" }, { "opId", 1 }, { "ok", false }, { "applied", 0 }, { "error", "" } };
		assert(slots.HandleMessage(810, EncodeJson(malformedResult), 2.0, 5100.2).empty());
		assert(slots.Stats().adminItemResultsRejected == 1);
		assert(!slots.LatestAdminItemResult(playerS).has_value());
		assert(slots.TrackAdminOp(810, 17));
		assert(!slots.TrackAdminOp(810, 18));

		const Json malformedReport{ { "type", "inventoryReport" }, { "opId", 2 }, { "truncated", "no" }, { "items", Json::array() } };
		assert(slots.HandleMessage(810, EncodeJson(malformedReport), 2.1, 5100.3).empty());
		assert(slots.Stats().inventoryReportsRejected == 1);
		assert(slots.TrackAdminOp(810, 18));
		assert(!slots.TrackAdminOp(810, 19));

		const Json unknownResult{ { "type", "adminItemResult" }, { "opId", 999 }, { "ok", false }, { "applied", 0 }, { "error", "" } };
		assert(slots.HandleMessage(810, EncodeJson(unknownResult), 2.2, 5100.4).empty());
		assert(slots.Stats().adminItemResultsRejected == 2);
		assert(!slots.TrackAdminOp(810, 19));
	}

	{
		ProtocolHost expiry{ "Expiry Test Server", "admin op expiry proof" };
		const auto welcomeE = expiry.HandleConnected(820, 5200.0);
		const auto playerE = DecodePacket(welcomeE[0]).at("playerId").get<std::uint32_t>();
		expiry.HandleMessage(
			820,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "admin-items-v1" }) } }),
			1.0,
			5200.1);
		const auto t0 = std::chrono::steady_clock::now();
		for (std::uint32_t opId = 1; opId <= 16; ++opId) {
			assert(expiry.TrackAdminOp(820, opId, t0));
		}
		assert(!expiry.TrackAdminOp(820, 17, t0 + std::chrono::seconds{ 59 }));
		assert(!expiry.LatestAdminItemResult(playerE).has_value());
		assert(expiry.TrackAdminOp(820, 17, t0 + std::chrono::seconds{ 61 }));

		const auto unknownLast = expiry.LatestAdminItemResult(playerE);
		assert(unknownLast && unknownLast->unknown && unknownLast->opId == 16 && !unknownLast->ok);

		const Json lateResult{ { "type", "adminItemResult" }, { "opId", 1 }, { "ok", true }, { "applied", 3 }, { "error", "" } };
		assert(expiry.HandleMessage(820, EncodeJson(lateResult), 2.0, 5200.2).empty());
		assert(expiry.Stats().adminItemResultsRejected == 1);
		assert(expiry.LatestAdminItemResult(playerE)->unknown && expiry.LatestAdminItemResult(playerE)->opId == 16);

		expiry.ExpireAdminOpsFor(playerE, t0 + std::chrono::seconds{ 122 });
		const auto getExpired = expiry.LatestAdminItemResult(playerE);
		assert(getExpired && getExpired->unknown && getExpired->opId == 17 && !getExpired->ok);

		const auto overdue = std::chrono::steady_clock::now() - std::chrono::seconds{ 61 };
		assert(expiry.TrackAdminOp(820, 18, overdue));
		const Json overdueResult{ { "type", "adminItemResult" }, { "opId", 18 }, { "ok", true }, { "applied", 1 }, { "error", "" } };
		assert(expiry.HandleMessage(820, EncodeJson(overdueResult), 2.1, 5200.3).empty());
		assert(expiry.Stats().adminItemResultsRejected == 2);
		assert(expiry.LatestAdminItemResult(playerE)->unknown && expiry.LatestAdminItemResult(playerE)->opId == 18);

		assert(expiry.TrackAdminOp(820, 19, overdue));
		const Json overdueReport{ { "type", "inventoryReport" }, { "opId", 19 }, { "truncated", false }, { "items", Json::array() } };
		assert(expiry.HandleMessage(820, EncodeJson(overdueReport), 2.2, 5200.4).empty());
		assert(expiry.Stats().inventoryReportsRejected == 1);
		assert(expiry.LatestAdminItemResult(playerE)->unknown && expiry.LatestAdminItemResult(playerE)->opId == 19);
	}

	{
		ProtocolHost ordering{ "Expiry Order Test Server", "admin op order proof" };
		const auto welcomeO = ordering.HandleConnected(830, 5300.0);
		const auto playerO = DecodePacket(welcomeO[0]).at("playerId").get<std::uint32_t>();
		ordering.HandleMessage(
			830,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "admin-items-v1" }) } }),
			1.0,
			5300.1);
		const auto t1 = std::chrono::steady_clock::now();
		assert(ordering.TrackAdminOp(830, 1, t1));
		assert(ordering.TrackAdminOp(830, 2, t1 + std::chrono::seconds{ 30 }));
		const Json realTwo{ { "type", "adminItemResult" }, { "opId", 2 }, { "ok", true }, { "applied", 4 }, { "error", "" } };
		assert(ordering.HandleMessage(830, EncodeJson(realTwo), 2.0, 5300.2).empty());

		assert(ordering.TrackAdminOp(830, 3, t1 + std::chrono::seconds{ 61 }));
		const auto kept = ordering.LatestAdminItemResult(playerO);
		assert(kept && !kept->unknown && kept->opId == 2 && kept->ok && kept->applied == 4);

		assert(ordering.TrackAdminOp(830, 4, t1 + std::chrono::seconds{ 61 }));
		assert(ordering.TrackAdminOp(830, 5, t1 + std::chrono::seconds{ 122 }));
		const auto overwritten = ordering.LatestAdminItemResult(playerO);
		assert(overwritten && overwritten->unknown && overwritten->opId == 4);
	}
	TestDurableStateTrailingFlush();
	TestFailedPlayerStateRelayIsResent();
	TestProgressionIsNotRelayed();
	TestInvalidProgressionIsReported();

	return 0;
}
