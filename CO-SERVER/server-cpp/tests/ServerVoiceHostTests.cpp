#include "ServerVoiceHost.h"
#include "ServerVoiceTestSupport.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::VoicePeerEntry;
	using CommonwealthOnline::Server::VoicePeers;
	using CommonwealthOnline::Server::VoiceSessionMap;
	using CommonwealthOnline::Server::VoiceStatsJson;
	using CommonwealthOnline::Server::SessionTypes::Session;
	using CommonwealthOnline::Server::SessionTypes::StoredMovementState;

	void StatsJsonWritesEveryCounter()
	{
		const std::pair<const char*, std::uint64_t ProtocolStats::*> counters[] = {
			{ "received", &ProtocolStats::voiceReceived },
			{ "accepted", &ProtocolStats::voiceAccepted },
			{ "relayed", &ProtocolStats::voiceRelayed },
			{ "dropped_inactive", &ProtocolStats::voiceDroppedInactive },
			{ "dropped_unnegotiated", &ProtocolStats::voiceDroppedUnnegotiated },
			{ "dropped_not_sendable", &ProtocolStats::voiceDroppedNotSendable },
			{ "dropped_no_state", &ProtocolStats::voiceDroppedNoState },
			{ "dropped_malformed", &ProtocolStats::voiceDroppedMalformed },
			{ "dropped_no_movement", &ProtocolStats::voiceDroppedNoMovement },
			{ "dropped_muted", &ProtocolStats::voiceDroppedMuted },
			{ "dropped_duplicate", &ProtocolStats::voiceDroppedDuplicate },
			{ "dropped_stale", &ProtocolStats::voiceDroppedStale },
			{ "dropped_soft_limit", &ProtocolStats::voiceDroppedSoftLimit },
			{ "dropped_egress", &ProtocolStats::voiceDroppedEgress },
			{ "auto_mutes", &ProtocolStats::voiceAutoMutes },
			{ "sent", &ProtocolStats::voiceSent },
			{ "send_backpressure", &ProtocolStats::voiceSendBackpressure },
			{ "send_dropped", &ProtocolStats::voiceSendDropped },
			{ "send_failed", &ProtocolStats::voiceSendFailed },
			{ "states_applied", &ProtocolStats::voiceStatesApplied },
			{ "states_rejected", &ProtocolStats::voiceStatesRejected },
			{ "states_limited", &ProtocolStats::voiceStatesLimited }
		};
		ProtocolStats stats;
		std::uint64_t value = 1;
		for (const auto& counter : counters) {
			stats.*counter.second = value++;
		}
		const Json json = VoiceStatsJson(stats);
		assert(json.size() == std::size(counters));
		value = 1;
		for (const auto& counter : counters) {
			assert(json.contains(counter.first));
			assert(json.at(counter.first).get<std::uint64_t>() == value++);
		}
	}

	Session MakeSession(std::uint32_t a_playerId, bool a_voiceCapability)
	{
		Session session;
		session.playerId = a_playerId;
		session.profileId = "profile-" + std::to_string(a_playerId);
		session.active = true;
		session.voice.stateApplied = true;
		session.voice.canPlay = true;
		if (a_voiceCapability) {
			session.compatibilityCapabilities.values = { "voice-v1" };
		}
		return session;
	}

	void VoicePeersCarryCapabilityLimitAndScope()
	{
		VoiceSessionMap sessions;
		sessions.emplace(1, MakeSession(1, true));
		sessions.emplace(2, MakeSession(2, false));
		sessions.emplace(3, MakeSession(65536U, true));
		auto& moved = sessions.emplace(4, MakeSession(4, true)).first->second;
		moved.lastMovement = StoredMovementState{ 10.0, 20.0, 30.0, 0.0, "cell-a", "wrld-a" };
		auto inactive = MakeSession(5, true);
		inactive.active = false;
		sessions.emplace(5, std::move(inactive));

		const auto peers = VoicePeers(sessions);
		assert(peers.size() == 4);
		const VoicePeerEntry* byConnection[6]{};
		for (const auto& entry : peers) {
			byConnection[entry.connectionId] = &entry;
		}
		assert(byConnection[5] == nullptr);
		assert(byConnection[1] && byConnection[1]->peer.voiceV1);
		assert(byConnection[2] && !byConnection[2]->peer.voiceV1);
		assert(byConnection[3] && !byConnection[3]->peer.voiceV1);
		assert(byConnection[4] && byConnection[4]->peer.voiceV1);
		assert(byConnection[1]->profileId == "profile-1");
		assert(!byConnection[1]->peer.scope);
		const auto& scope = byConnection[4]->peer.scope;
		assert(scope && scope->cellId == "cell-a" && scope->worldspaceId == "wrld-a");
		assert(scope->x == 10.0 && scope->y == 20.0);
		assert(byConnection[4]->peer.z == 30.0);
	}

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json VoiceState(std::uint64_t a_revision)
	{
		return Json{
			{ "type", "voiceState" },
			{ "revision", a_revision },
			{ "profile", 0 },
			{ "rate", 48000 },
			{ "channels", 1 },
			{ "frameMs", 20 },
			{ "canSend", true },
			{ "canPlay", true },
			{ "deafened", false },
			{ "blockedPlayerIds", Json::array() }
		};
	}

	Json TransformAt(double a_x)
	{
		return Json{ { "type", "transform" }, { "cellId", "100" }, { "worldspaceId", "F00D" }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } };
	}

	bool IsAck(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return a_frames.size() == 1 && a_frames.front().connectionId == a_connectionId &&
			ServerVoiceTestSupport::ParseFrame(a_frames.front()).at("type") == "voiceStateAck";
	}

	std::uint32_t JoinVoicePlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_now, double a_x, std::string_view a_profileId)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, 100.0);
		const Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "voice-v1" }) } };
		assert(!a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_now, 100.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(TransformAt(a_x)), a_now + 0.1, 100.2);
		return ServerVoiceTestSupport::ParseFrame(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	void PlayerIdsStayVoiceEligibleAfterManyConnections()
	{
		ProtocolHost host{ "Voice Test" };
		for (std::uint32_t cycle = 0; cycle <= 65536U; cycle++) {
			(void)host.HandleConnected(100, 100.0);
			host.HandleDisconnected(100, 100.5);
		}
		const auto playerA = JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		const auto playerB = JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		assert(playerA <= 65535U && playerB <= 65535U && playerA != playerB);
		assert(IsAck(host.HandleMessage(10, EncodeJson(VoiceState(1)), 2.0, 101.0), 10));
		assert(IsAck(host.HandleMessage(20, EncodeJson(VoiceState(1)), 2.1, 101.1), 20));
		const auto relayed = host.HandleMessage(10, ServerVoiceTestSupport::MakeDatagram(1, false), 3.0, 102.0);
		assert(ServerVoiceTestSupport::RelayCount(relayed, 20) == 1);
	}

	void CollidingPlayerIdsAfterWrapStayDistinct()
	{
		ProtocolHost host{ "Voice Test" };
		const auto playerA = JoinVoicePlayer(host, 10, 1.0, 0.0, "profile-a");
		for (std::uint32_t cycle = 1; cycle < 65535U; cycle++) {
			(void)host.HandleConnected(100, 100.0);
			host.HandleDisconnected(100, 100.5);
		}
		const auto playerB = JoinVoicePlayer(host, 20, 1.5, 10.0, "profile-b");
		assert(playerA != playerB && playerA <= 65535U && playerB <= 65535U);
		assert(IsAck(host.HandleMessage(10, EncodeJson(VoiceState(1)), 2.0, 101.0), 10));
		assert(IsAck(host.HandleMessage(20, EncodeJson(VoiceState(1)), 2.1, 101.1), 20));
		const auto relayed = host.HandleMessage(10, ServerVoiceTestSupport::MakeDatagram(1, false), 3.0, 102.0);
		assert(ServerVoiceTestSupport::RelayCount(relayed, 20) == 1);
	}

	void StartBurstThenMoveListenerOutOfRange(ServerVoiceTestSupport::Harness& a_harness)
	{
		ServerVoiceTestSupport::AddPeer(a_harness, 1, 1, 0.0);
		ServerVoiceTestSupport::AddPeer(a_harness, 2, 2, 100.0);
		a_harness.peers.back().peer.canPlay = false;
		const auto started = a_harness.service.HandleFrame(1, ServerVoiceTestSupport::MakeDatagram(1, true), a_harness.peers, 1.0);
		assert(ServerVoiceTestSupport::TalkCount(started, 2) == 1);
		a_harness.peers.back().peer.scope->x = 1500.0;
	}

	void BothBurstFlagsStartOncePerListenerAndEndOnSilence()
	{
		ServerVoiceTestSupport::Harness harness;
		ServerVoiceTestSupport::AddPeer(harness, 1, 1, 0.0);
		ServerVoiceTestSupport::AddPeer(harness, 2, 2, 100.0);
		ServerVoiceTestSupport::AddPeer(harness, 3, 3, 100.0);
		ServerVoiceTestSupport::AddPeer(harness, 4, 4, 100.0);
		harness.peers[1].peer.canPlay = false;
		harness.peers[2].peer.canPlay = false;
		std::size_t startsToListenerTwo = 0;
		std::size_t startsToListenerThree = 0;
		std::size_t startsToPlayingListener = 0;
		for (std::uint32_t sequence = 1; sequence <= 20; ++sequence) {
			const auto frames = harness.service.HandleFrame(1, ServerVoiceTestSupport::MakeDatagram(sequence, true, ServerVoiceTestSupport::Tier::Whisper, true), harness.peers, 1.0 + sequence * 0.05);
			startsToListenerTwo += ServerVoiceTestSupport::TalkCount(frames, 2);
			startsToListenerThree += ServerVoiceTestSupport::TalkCount(frames, 3);
			startsToPlayingListener += ServerVoiceTestSupport::TalkCount(frames, 4);
		}
		assert(startsToListenerTwo == 1 && startsToListenerThree == 1 && startsToPlayingListener == 0);
		assert(harness.service.Poll(harness.peers, 2.3).empty());
		const auto ended = harness.service.Poll(harness.peers, 2.5);
		assert(ended.size() == 2);
		assert(ServerVoiceTestSupport::TalkCount(ended, 2) == 1 && ServerVoiceTestSupport::TalkCount(ended, 3) == 1);
		for (const auto& frame : ended) {
			assert(ServerVoiceTestSupport::ParseFrame(frame).at("talking") == false);
		}
		assert(harness.service.Poll(harness.peers, 3.0).empty());
	}

	void TalkEndReachesListenerMovedOutOfRangeOnBurstEnd()
	{
		ServerVoiceTestSupport::Harness harness;
		StartBurstThenMoveListenerOutOfRange(harness);
		const auto ended = harness.service.HandleFrame(1, ServerVoiceTestSupport::MakeDatagram(2, false, ServerVoiceTestSupport::Tier::Whisper, true), harness.peers, 1.1);
		assert(ended.size() == 1 && ended.front().connectionId == 2);
		assert(ServerVoiceTestSupport::ParseFrame(ended.front()).at("talking") == false);
	}

	void TalkEndReachesListenerMovedOutOfRangeOnDepart()
	{
		ServerVoiceTestSupport::Harness harness;
		StartBurstThenMoveListenerOutOfRange(harness);
		const auto ended = harness.service.Depart(harness.peers, 1);
		assert(ended.size() == 1 && ended.front().connectionId == 2);
		assert(ServerVoiceTestSupport::ParseFrame(ended.front()).at("talking") == false);
	}
}

int main()
{
	StatsJsonWritesEveryCounter();
	VoicePeersCarryCapabilityLimitAndScope();
	PlayerIdsStayVoiceEligibleAfterManyConnections();
	CollidingPlayerIdsAfterWrapStayDistinct();
	BothBurstFlagsStartOncePerListenerAndEndOnSilence();
	TalkEndReachesListenerMovedOutOfRangeOnBurstEnd();
	TalkEndReachesListenerMovedOutOfRangeOnDepart();
	return 0;
}
