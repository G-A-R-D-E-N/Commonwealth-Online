#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
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

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}
}

int main()
{
#if defined(_WIN32) && defined(_DEBUG)
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "Interest Test" };
	assert(host.EnableSocial(nullptr));
	const auto welcomeA = host.HandleConnected(10, 100.0);
	host.BindSessionProfile(10, "interest-a");
	const auto playerA = DecodePacket(welcomeA.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 1.0, 100.1).empty());

	host.HandleConnected(20, 100.2);
	host.BindSessionProfile(20, "interest-b");
	assert(!host.HandleMessage(20, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 1.1, 100.3).empty());

	const Json transformA{
		{ "type", "transform" },
		{ "cellId", "100" },
		{ "worldspaceId", "F00D" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 0.0 },
		{ "movementType", "spawn" }
	};
	const auto firstA = host.HandleMessage(10, EncodeSnapshot(transformA, Family::Transform, 1), 2.0, 101.0);
	// Connection 20 has not reported a position yet, so it is inside nobody's
	// interest and receives nothing.
	assert(FindFrame(firstA, 20, "transform") == nullptr);

	const Json transformB{
		{ "type", "transform" },
		{ "cellId", "200" },
		{ "worldspaceId", "DEAD" },
		{ "x", 0.0 },
		{ "y", 0.0 },
		{ "z", 0.0 },
		{ "angleZ", 0.0 },
		{ "movementType", "spawn" }
	};
	const auto firstB = host.HandleMessage(20, EncodeSnapshot(transformB, Family::Transform, 1), 2.1, 101.1);
	assert(FindFrame(firstB, 10, "transform") == nullptr);
	bool receivedExistingAuthority = false;
	for (const auto& frame : firstB) {
		if (frame.connectionId != 20 || frame.payload.empty()) continue;
		const auto packet = DecodePacket(frame);
		if (packet.value("type", std::string{}) == "npcAuthority" &&
			packet.value("authorityCellId", std::string{}) == "00000100" &&
			packet.value("authorityPlayerId", 0u) == playerA) receivedExistingAuthority = true;
	}
	assert(receivedExistingAuthority);

	Json movedA = transformA;
	movedA["movementType"] = "normal";
	movedA["x"] = 50.0;
	const auto filteredMove = host.HandleMessage(10, EncodeSnapshot(movedA, Family::Transform, 2), 2.2, 101.2);
	assert(FindFrame(filteredMove, 20, "transform") == nullptr);

	const Json playerState{
		{ "type", "playerState" },
		{ "characterName", "Nomad" },
		{ "actionEvents", Json::array({ Json{
			{ "sequence", 1 },
			{ "type", 3 },
			{ "eventName", "fireSingle" }
		} }) }
	};
	const auto stateFrames = host.HandleMessage(10, EncodeJson(playerState), 2.3, 101.3);
	const auto* stateFrame = FindFrame(stateFrames, 20, "playerState");
	assert(stateFrame != nullptr);
	const auto relayedState = DecodePacket(*stateFrame);
	assert(relayedState.at("displayName") == "Stranger");
	assert(!relayedState.contains("characterName"));
	assert(!relayedState.contains("actionEvents"));

	const Json npcState{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "100" },
		{ "authorityWorldspaceId", "F00D" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "100" },
			{ "worldspaceId", "F00D" },
			{ "x", 10.0 },
			{ "y", 10.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 }
		} }) }
	};
	const auto npcFrames = host.HandleMessage(10, EncodeSnapshot(npcState, Family::NpcState, 1), 2.4, 101.4);
	const auto* npcFrame = FindFrame(npcFrames, 20, "npcState");
	assert(npcFrame != nullptr);
	const auto relayedNpc = DecodePacket(*npcFrame);
	assert(relayedNpc.at("playerId") == playerA);
	assert(relayedNpc.at("npcs").empty());

	const Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } };
	const auto welcomeC = host.HandleConnected(30, 102.0);
	host.BindSessionProfile(30, "interest-c");
	const auto playerC = DecodePacket(welcomeC.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(30, EncodeJson(hello), 3.0, 102.1).empty());

	const auto welcomeD = host.HandleConnected(40, 102.2);
	host.BindSessionProfile(40, "interest-d");
	const auto playerD = DecodePacket(welcomeD.front()).at("playerId").get<std::uint32_t>();
	assert(!host.HandleMessage(40, EncodeJson(hello), 3.1, 102.3).empty());

	Json spawnD = transformA;
	spawnD["cellId"] = "300";
	spawnD["worldspaceId"] = "F00E";
	Json spawnC = spawnD;
	host.HandleMessage(30, EncodeSnapshot(spawnC, Family::Transform, 1), 3.2, 102.4);
	const auto sharedD = host.HandleMessage(40, EncodeSnapshot(spawnD, Family::Transform, 1), 3.3, 102.5);
	assert(FindFrame(sharedD, 40, "transform") != nullptr);

	Json leftD = spawnD;
	leftD["cellId"] = "301";
	leftD["x"] = 9000.0;
	leftD["movementType"] = "cell_change";
	const auto hiddenD = host.HandleMessage(40, EncodeSnapshot(leftD, Family::Transform, 2), 20.0, 120.0);
	const auto* hiddenForD = FindFrame(hiddenD, 40, "peerHidden");
	assert(hiddenForD != nullptr);
	assert(DecodePacket(*hiddenForD).at("playerId") == playerC);
	const auto hiddenSequence = DecodePacket(*hiddenForD).at("visibilitySequence").get<std::uint64_t>();
	const auto* hiddenForC = FindFrame(hiddenD, 30, "peerHidden");
	assert(hiddenForC != nullptr);
	assert(DecodePacket(*hiddenForC).at("playerId") == playerD);

	Json returnedD = spawnD;
	returnedD["x"] = 100.0;
	returnedD["movementType"] = "cell_change";
	const auto reshownD = host.HandleMessage(40, EncodeSnapshot(returnedD, Family::Transform, 3), 40.0, 140.0);
	assert(FindFrame(reshownD, 40, "peerHidden") == nullptr);
	const auto* reshownForD = FindFrame(reshownD, 40, "transform");
	assert(reshownForD != nullptr);
	assert(DecodePacket(*reshownForD).at("visibilitySequence").get<std::uint64_t>() > hiddenSequence);
	assert(FindFrame(reshownD, 30, "transform") != nullptr);

	const Json downedState{ { "type", "playerState" }, { "characterName", "BeforeSolo" },
		{ "vitals", Json{ { "healthPercent", 10 }, { "isDowned", true }, { "isDead", false } } } };
	host.HandleMessage(40, EncodeJson(downedState), 40.1, 140.1);
	const auto paused = host.HandleMessage(40, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 1 } }), 41.0, 141.0);
	assert(FindFrame(paused, 30, "peerHidden") != nullptr);
	const auto downedWhileHidden = host.PollDownedState(41.1);
	assert(FindFrame(downedWhileHidden, 30, "downedState") == nullptr);
	assert(FindFrame(downedWhileHidden, 40, "downedState") != nullptr);
	const auto revivedWhileHidden = host.AdminRevivePlayer(playerD, 41.15);
	assert(FindFrame(revivedWhileHidden, 30, "downedState") == nullptr);
	const Json soloState{ { "type", "playerState" }, { "characterName", "SoloSecret" },
		{ "vitals", Json{ { "healthPercent", 20 }, { "isDowned", false }, { "isDead", false } } } };
	assert(FindFrame(host.HandleMessage(40, EncodeJson(soloState), 41.1, 141.1), 30, "playerState") == nullptr);
	assert(FindFrame(host.PollDurablePlayerState(41.2, 141.2), 30, "playerState") == nullptr);
	Json stale = returnedD;
	stale["movementEpoch"] = 1;
	assert(FindFrame(host.HandleMessage(40, EncodeSnapshot(stale, Family::Transform, 4), 41.1, 141.1), 30, "transform") == nullptr);
	host.HandleMessage(40, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", false }, { "movementEpoch", 2 } }), 41.2, 141.2);
	assert(FindFrame(host.HandleMessage(40, EncodeSnapshot(stale, Family::Transform, 5), 41.3, 141.3), 30, "transform") == nullptr);
	Json invalid = returnedD;
	invalid["movementEpoch"] = 2;
	invalid["movementSequence"] = 1;
	invalid["movementType"] = "normal";
	invalid["x"] = 100000.0;
	assert(FindFrame(host.HandleMessage(40, EncodeSnapshot(invalid, Family::Transform, 6), 41.4, 141.4), 30, "transform") == nullptr);
	Json resumed = returnedD;
	resumed["movementEpoch"] = 2;
	resumed["movementSequence"] = 2;
	resumed["resumeTransitionId"] = 1;
	resumed["movementType"] = "normal";
	resumed["cellId"] = "301";
	const auto resumedFrames = host.HandleMessage(40, EncodeJson(resumed), 41.5, 141.5);
	const auto* acknowledged = FindFrame(resumedFrames, 40, "visibilityResumed");
	assert(acknowledged != nullptr);
	assert(DecodePacket(*acknowledged).at("movementEpoch") == 2);
	assert(DecodePacket(*acknowledged).at("resumeTransitionId") == 1);
	assert(FindFrame(resumedFrames, 30, "transform") != nullptr);
	assert(FindFrame(resumedFrames, 40, "transform") != nullptr);
	const auto* resumedState = FindFrame(resumedFrames, 30, "playerState");
	assert(resumedState != nullptr);
	assert(DecodePacket(*resumedState).at("vitals").at("healthPercent") == 20);
	const auto* resumedDowned = FindFrame(resumedFrames, 30, "downedState");
	assert(resumedDowned != nullptr);
	assert(DecodePacket(*resumedDowned).at("downed") == false);
	Json retry = resumed;
	retry["movementSequence"] = 3;
	retry["movementType"] = "cell_change";
	retry["resumeRetry"] = true;
	const auto retryFrames = host.HandleMessage(40, EncodeJson(retry), 41.6, 141.6);
	assert(FindFrame(retryFrames, 30, "transform") != nullptr);
	assert(FindFrame(retryFrames, 40, "positionCorrection") == nullptr);
	Json changedAgain = resumed;
	changedAgain["movementSequence"] = 4;
	changedAgain["resumeTransitionId"] = 2;
	changedAgain["movementType"] = "cell_change";
	changedAgain["cellId"] = "302";
	const auto changedFrames = host.HandleMessage(40, EncodeJson(changedAgain), 43.0, 143.0);
	assert(FindFrame(changedFrames, 30, "transform") != nullptr);
	const auto* changedAcknowledged = FindFrame(changedFrames, 40, "visibilityResumed");
	assert(changedAcknowledged != nullptr);
	assert(DecodePacket(*changedAcknowledged).at("resumeTransitionId") == 2);
	Json newer = changedAgain;
	newer["movementSequence"] = 6;
	newer["movementType"] = "normal";
	newer.erase("resumeTransitionId");
	newer["x"] = 101.0;
	assert(FindFrame(host.HandleMessage(40, EncodeSnapshot(newer, Family::Transform, 7), 43.1, 143.1), 30, "transform") != nullptr);
	Json delayed = retry;
	delayed["movementSequence"] = 5;
	assert(FindFrame(host.HandleMessage(40, EncodeJson(delayed), 43.2, 143.2), 30, "transform") == nullptr);
	Json teleport = newer;
	teleport["movementSequence"] = 7;
	teleport["movementType"] = "teleport";
	teleport["resumeTransitionId"] = 3;
	teleport["x"] = 9000.0;
	const auto beforeTeleport = host.Stats().movementRejected;
	host.HandleMessage(40, EncodeJson(teleport), 44.0, 144.0);
	assert(host.Stats().movementRejected == beforeTeleport);
	teleport["movementSequence"] = 8;
	teleport["resumeRetry"] = true;
	host.HandleMessage(40, EncodeJson(teleport), 44.1, 144.1);
	assert(host.Stats().movementRejected == beforeTeleport);
	teleport["movementSequence"] = 9;
	teleport["resumeTransitionId"] = 4;
	teleport["x"] = 18000.0;
	host.HandleMessage(40, EncodeJson(teleport), 44.2, 144.2);
	assert(host.Stats().movementRejected == beforeTeleport + 1);
	teleport["movementSequence"] = 10;
	host.HandleMessage(40, EncodeJson(teleport), 45.1, 145.1);
	assert(host.Stats().movementRejected == beforeTeleport + 1);
	const auto pausedAgain = host.HandleMessage(40, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 3 } }), 50.0, 150.0);
	assert(FindFrame(pausedAgain, 30, "playerDied") == nullptr);
	const auto diedWhileHidden = host.AdminKillPlayer(playerD);
	assert(FindFrame(diedWhileHidden, 30, "playerDied") == nullptr);
	host.HandleMessage(40, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", false }, { "movementEpoch", 4 } }), 50.2, 150.2);
	Json deadResume = teleport;
	deadResume["movementEpoch"] = 4;
	deadResume["movementSequence"] = 1;
	deadResume["resumeTransitionId"] = 1;
	deadResume["movementType"] = "teleport";
	deadResume["x"] = 100.0;
	deadResume.erase("resumeRetry");
	const auto deadResumeFrames = host.HandleMessage(40, EncodeJson(deadResume), 50.3, 150.3);
	const auto* deadState = FindFrame(deadResumeFrames, 30, "playerState");
	assert(deadState != nullptr);
	assert(DecodePacket(*deadState).at("vitals").at("isDead") == true);
	assert(FindFrame(deadResumeFrames, 30, "playerDied") != nullptr);
	const auto* deadTransform = FindFrame(deadResumeFrames, 30, "transform");
	assert(deadTransform != nullptr);
	assert(DecodePacket(*deadTransform).at("serverDowned").at("dead") == true);
	ProtocolHost sleepHost{ "Sleep Interest Test" };
	for (const auto id : { 30u, 40u, 50u }) {
		sleepHost.HandleConnected(id, 200.0);
		sleepHost.BindSessionProfile(id, "sleep-" + std::to_string(id));
		assert(!sleepHost.HandleMessage(id, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 },
			{ "capabilities", Json::array({ "sleep-bed-v1" }) } }), 3.0, 200.1).empty());
	}
	const auto firstSleeper = sleepHost.HandleMessage(30, EncodeSnapshot(transformA, Family::Transform, 1), 4.0, 201.0);
	(void)firstSleeper;
	const Json sleepEnter{ { "type", "sleepUse" }, { "action", "enter" },
		{ "bed", { { "kind", "form" }, { "key", "001234:fallout4.esm" } } }, { "marker", 0 } };
	sleepHost.HandleMessage(30, EncodeJson(sleepEnter), 4.1, 201.1);
	const auto laterPeer = sleepHost.HandleMessage(40, EncodeSnapshot(transformA, Family::Transform, 1), 4.2, 201.2);
	const auto* sleepSnapshot = FindFrame(laterPeer, 40, "sleepUse");
	assert(sleepSnapshot && DecodePacket(*sleepSnapshot).at("action") == "enter");
	const auto sleeperId = DecodePacket(*sleepSnapshot).at("playerId");
	auto distant = transformB;
	distant["movementType"] = "teleport";
	const auto leave = sleepHost.HandleMessage(40, EncodeSnapshot(distant, Family::Transform, 2), 4.25, 201.25);
	const auto* forgetSleep = FindFrame(leave, 40, "sleepUse");
	assert(forgetSleep && DecodePacket(*forgetSleep).at("action") == "exit");
	assert(DecodePacket(*forgetSleep).at("playerId") == sleeperId);
	const auto exitFrames = sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "sleepUse" }, { "action", "exit" } }), 4.3, 201.3);
	assert(FindFrame(exitFrames, 40, "sleepUse") == nullptr);
	auto nearby = transformA;
	nearby["movementType"] = "teleport";
	const auto returnPeer = sleepHost.HandleMessage(40, EncodeSnapshot(nearby, Family::Transform, 3), 5.35, 202.35);
	assert(FindFrame(returnPeer, 30, "transform") != nullptr);
	assert(FindFrame(returnPeer, 40, "sleepUse") == nullptr);
	const auto afterExit = sleepHost.HandleMessage(50, EncodeSnapshot(transformA, Family::Transform, 1), 5.4, 202.4);
	assert(FindFrame(afterExit, 50, "transform") != nullptr);
	assert(FindFrame(afterExit, 50, "sleepUse") == nullptr);
	sleepHost.HandleMessage(30, EncodeJson(sleepEnter), 6.0, 203.0);
	const auto sleeperTeleport = sleepHost.HandleMessage(30, EncodeSnapshot(distant, Family::Transform, 2), 7.1, 204.1);
	assert(FindFrame(sleeperTeleport, 40, "peerHidden") != nullptr);
	const auto followSleeper = sleepHost.HandleMessage(40, EncodeSnapshot(distant, Family::Transform, 4), 8.2, 205.2);
	assert(std::none_of(followSleeper.begin(), followSleeper.end(), [&](const auto& frame) {
		if (frame.connectionId != 40 || frame.payload.empty()) return false;
		const auto packet = DecodePacket(frame);
		return packet.value("type", std::string{}) == "sleepUse" && packet.value("action", std::string{}) == "enter" && packet["playerId"] == sleeperId;
	}));
	sleepHost.HandleMessage(30, EncodeJson(sleepEnter), 9.5, 206.5);
	auto sameCellTeleport = distant;
	sameCellTeleport["x"] = distant["x"].get<double>() + 10.0;
	const auto localTeleport = sleepHost.HandleMessage(30, EncodeSnapshot(sameCellTeleport, Family::Transform, 3), 10.7, 207.7);
	const auto* wakeOnTeleport = FindFrame(localTeleport, 40, "sleepUse");
	assert(wakeOnTeleport && DecodePacket(*wakeOnTeleport).at("action") == "exit");
	sleepHost.HandleMessage(30, EncodeJson(sleepEnter), 11.5, 208.5);
	const auto pauseSleeper = sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 1 } }), 12.0, 209.0);
	assert(FindFrame(pauseSleeper, 40, "peerHidden") != nullptr);
	sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", false }, { "movementEpoch", 2 } }), 12.1, 209.1);
	auto resumeSleeper = transformA;
	resumeSleeper["movementEpoch"] = 2;
	resumeSleeper["movementSequence"] = 1;
	resumeSleeper["resumeTransitionId"] = 1;
	resumeSleeper["movementType"] = "normal";
	const auto resumedSleeper = sleepHost.HandleMessage(30, EncodeJson(resumeSleeper), 13.5, 210.5);
	assert(FindFrame(resumedSleeper, 50, "transform") != nullptr);
	assert(FindFrame(resumedSleeper, 50, "sleepUse") == nullptr);
	sleepHost.HandleMessage(30, EncodeJson(sleepEnter), 14.5, 211.5);
	const auto sameScopePause = sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 3 } }), 15.0, 212.0);
	assert(FindFrame(sameScopePause, 50, "peerHidden") != nullptr);
	sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", false }, { "movementEpoch", 4 } }), 15.1, 212.1);
	resumeSleeper["movementEpoch"] = 4;
	const auto sameScopeResume = sleepHost.HandleMessage(30, EncodeJson(resumeSleeper), 16.5, 213.5);
	const auto* retainedSleep = FindFrame(sameScopeResume, 50, "sleepUse");
	assert(retainedSleep && DecodePacket(*retainedSleep).at("action") == "enter");
	const auto pauseBeforeWake = sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 5 } }), 17.0, 214.0);
	assert(FindFrame(pauseBeforeWake, 50, "peerHidden") != nullptr);
	const auto pausedWake = sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "sleepUse" }, { "action", "exit" } }), 17.1, 214.1);
	assert(FindFrame(pausedWake, 50, "sleepUse") == nullptr);
	sleepHost.HandleMessage(30, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", false }, { "movementEpoch", 6 } }), 17.2, 214.2);
	resumeSleeper["movementEpoch"] = 6;
	const auto resumeAfterWake = sleepHost.HandleMessage(30, EncodeJson(resumeSleeper), 18.5, 215.5);
	assert(FindFrame(resumeAfterWake, 50, "transform") != nullptr);
	assert(FindFrame(resumeAfterWake, 50, "sleepUse") == nullptr);

	ProtocolHost adjacentHost{ "Adjacent Interest Test" };
	assert(adjacentHost.EnableSocial(nullptr));
	const auto welcomeE = adjacentHost.HandleConnected(60, 300.0);
	adjacentHost.BindSessionProfile(60, "adjacent-e");
	const auto playerE = DecodePacket(welcomeE.front()).at("playerId").get<std::uint32_t>();
	assert(!adjacentHost.HandleMessage(60, EncodeJson(hello), 1.0, 300.1).empty());

	const auto welcomeF = adjacentHost.HandleConnected(70, 300.2);
	adjacentHost.BindSessionProfile(70, "adjacent-f");
	const auto playerF = DecodePacket(welcomeF.front()).at("playerId").get<std::uint32_t>();
	assert(!adjacentHost.HandleMessage(70, EncodeJson(hello), 1.1, 300.3).empty());

	Json adjacentE = transformA;
	adjacentE["cellId"] = "400";
	adjacentE["worldspaceId"] = "F00F";
	adjacentE["x"] = 4000.0;
	Json adjacentF = adjacentE;
	adjacentF["cellId"] = "401";
	adjacentF["x"] = 4100.0;
	adjacentHost.HandleMessage(60, EncodeSnapshot(adjacentE, Family::Transform, 1), 2.0, 301.0);
	const auto adjacentTransforms = adjacentHost.HandleMessage(70, EncodeSnapshot(adjacentF, Family::Transform, 1), 2.1, 301.1);
	assert(FindFrame(adjacentTransforms, 60, "transform") != nullptr);
	assert(FindFrame(adjacentTransforms, 70, "transform") != nullptr);

	const Json npcE{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "400" },
		{ "authorityWorldspaceId", "F00F" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "1234" },
			{ "cellId", "400" },
			{ "worldspaceId", "F00F" },
			{ "x", 4010.0 },
			{ "y", 10.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 }
		} }) }
	};
	const auto npcToF = adjacentHost.HandleMessage(60, EncodeSnapshot(npcE, Family::NpcState, 1), 2.2, 301.2);
	const auto* npcAtF = FindFrame(npcToF, 70, "npcState");
	assert(npcAtF != nullptr);
	const auto relayedNpcAtF = DecodePacket(*npcAtF);
	assert(relayedNpcAtF.at("playerId") == playerE);
	assert(!relayedNpcAtF.at("npcs").empty());

	const Json npcF{
		{ "type", "npcState" },
		{ "authorityEpoch", 1 },
		{ "authorityCellId", "401" },
		{ "authorityWorldspaceId", "F00F" },
		{ "npcs", Json::array({ Json{
			{ "sourceFormId", "5678" },
			{ "cellId", "401" },
			{ "worldspaceId", "F00F" },
			{ "x", 4110.0 },
			{ "y", 10.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 }
		} }) }
	};
	const auto npcToE = adjacentHost.HandleMessage(70, EncodeSnapshot(npcF, Family::NpcState, 1), 2.3, 301.3);
	const auto* npcAtE = FindFrame(npcToE, 60, "npcState");
	assert(npcAtE != nullptr);
	const auto relayedNpcAtE = DecodePacket(*npcAtE);
	assert(relayedNpcAtE.at("playerId") == playerF);
	assert(!relayedNpcAtE.at("npcs").empty());

	return 0;
}
