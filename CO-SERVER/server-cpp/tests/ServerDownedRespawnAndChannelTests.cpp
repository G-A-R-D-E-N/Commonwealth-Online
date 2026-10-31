#include "ServerProtocol.h"
#include "ServerConfiguration.h"
#include "ServerDownedTestSupport.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace ServerDownedTestSupport;

	void TestRespawnGrantFlow()
	{
		ProtocolHost host;
		const auto friendPlayer = Join(host, 1, "profile-friend");
		const auto victim = Join(host, 2, "profile-victim");
		assert(friendPlayer != victim);
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		assert(SendRespawn(host, 2, "sanctuary", std::nullopt, 2.1).empty());
		assert(host.Stats().respawnRequestsRejected == 1);

		assert(!host.PollDownedState(62.0).empty());

		const auto granted = SendRespawn(host, 2, "sanctuary", std::nullopt, 62.1);
		const auto grant = FramesOfType(granted, 2, "respawnGrant");
		assert(grant.size() == 1);
		assert(grant.front().at("point").get<std::string>() == "sanctuary");
		assert(!grant.front().contains("friendId"));
		assert(FramesOfType(granted, 1, "respawnGrant").empty());
		assert(host.Stats().respawnRequestsReceived == 2);
		assert(host.Stats().respawnRequestsRejected == 1);

		assert(SendRespawn(host, 2, "sanctuary", std::nullopt, 62.2).empty());
		assert(host.Stats().respawnRequestsRejected == 2);

		assert(!SendVitals(host, 1, 62.3, true, false, 0).empty());
		assert(!host.PollDownedState(122.4).empty());
		const auto friendGrant = SendRespawn(host, 1, "friend", victim, 122.5);
		const auto friendPacket = FramesOfType(friendGrant, 1, "respawnGrant");
		assert(friendPacket.size() == 1);
		assert(friendPacket.front().at("point").get<std::string>() == "friend");
		assert(friendPacket.front().at("friendId").get<std::uint32_t>() == victim);

		assert(!host.AdminKillPlayer(victim).empty());
		assert(SendRespawn(host, 2, "moon", std::nullopt, 122.6).empty());
		assert(host.Stats().respawnRequestsRejected == 3);
	}

	void TestAdminReviveAndKill()
	{
		ProtocolHost host;
		const auto watcher = Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(watcher != victim);

		assert(host.AdminRevivePlayer(victim, 1.0).empty());
		assert(host.Stats().downedEventsEmitted == 0);

		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());
		assert(host.Stats().downedEventsEmitted == 1);

		const auto revived = host.AdminRevivePlayer(victim, 2.1);
		const auto revivedPacket = FramesOfType(revived, 2, "revived");
		assert(revivedPacket.size() == 1);
		assert(revivedPacket.front().at("byPlayerId").get<std::uint32_t>() == 0);
		assert(FramesOfType(revived, 1, "downedState").size() == 1);
		assert(host.Stats().downedEventsEmitted == 2);

		const auto killed = host.AdminKillPlayer(victim);
		assert(FramesOfType(killed, 2, "died").size() == 1);
		assert(FramesOfType(killed, 1, "playerDied").size() == 1);
		assert(host.Stats().downedEventsEmitted == 3);

		assert(host.AdminKillPlayer(victim).empty());
		assert(host.AdminKillPlayer(999).empty());
		assert(host.AdminRevivePlayer(999, 2.2).empty());
		assert(host.Stats().downedEventsEmitted == 3);
	}

	void TestAdminReviveClearsDeadClientState()
	{
		ProtocolHost host;
		Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());
		assert(!host.PollDownedState(62.0).empty());
		assert(host.Stats().downedEventsEmitted == 3);

		const auto frames = host.AdminRevivePlayer(victim, 62.1);
		const auto grant = FramesOfType(frames, 2, "respawnGrant");
		assert(grant.size() == 1);
		assert(grant.front().at("point").get<std::string>() == "lastbed");
		assert(!grant.front().contains("friendId"));
		assert(FramesOfType(frames, 1, "respawnGrant").empty());
		assert(FramesOfType(frames, 2, "revived").size() == 1);
		const auto cleared = FramesOfType(frames, 1, "downedState");
		assert(cleared.size() == 1);
		assert(!cleared.front().at("downed").get<bool>());
		assert(host.Stats().downedEventsEmitted == 5);

		(void)SendVitals(host, 2, 62.5, false, false, 100);
		assert(host.AdminRevivePlayer(victim, 62.6).empty());
		assert(!host.AdminKillPlayer(victim).empty());

		const auto killedRevive = host.AdminRevivePlayer(victim, 62.7);
		assert(FramesOfType(killedRevive, 2, "respawnGrant").size() == 1);
		assert(FramesOfType(killedRevive, 2, "revived").size() == 1);
		assert(host.Stats().downedEventsEmitted == 8);
	}

	void TestRespawnRequestRateLimit()
	{
		ProtocolHost host;
		Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(victim != 0);
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		for (std::size_t i = 0; i < 64; ++i) {
			assert(SendRespawn(host, 2, "sanctuary", std::nullopt, 2.1).empty());
		}
		assert(host.Stats().respawnRequestsReceived == 64);
		assert(host.Stats().respawnRequestsRejected == 64);
		assert(host.Stats().downedEventsEmitted == 1);
		assert(host.Stats().packetsRejected == 0);

		const auto limitedMalformed = host.HandleMessage(2, EncodeJson(Json{ { "type", "respawnRequest" } }), 2.2, 100.0);
		assert(limitedMalformed.empty());
		assert(host.Stats().respawnRequestsReceived == 65);
		assert(host.Stats().respawnRequestsRejected == 65);
		assert(host.Stats().packetsRejected == 0);

		const auto refilledMalformed = host.HandleMessage(2, EncodeJson(Json{ { "type", "respawnRequest" } }), 10.0, 100.0);
		assert(refilledMalformed.empty());
		assert(host.Stats().respawnRequestsReceived == 66);
		assert(host.Stats().respawnRequestsRejected == 66);
		assert(host.Stats().packetsRejected == 1);

		assert(!host.PollDownedState(62.0).empty());
		const auto granted = SendRespawn(host, 2, "sanctuary", std::nullopt, 62.1);
		assert(FramesOfType(granted, 2, "respawnGrant").size() == 1);
		assert(host.Stats().respawnRequestsRejected == 66);

		assert(SendRespawn(host, 2, "sanctuary", std::nullopt, 62.2).empty());
		assert(host.Stats().respawnRequestsRejected == 67);
	}

	void TestRespawningGraceExpires()
	{
		ProtocolHost host;
		Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		const auto revived = host.AdminRevivePlayer(victim, 10.0);
		assert(FramesOfType(revived, 2, "revived").size() == 1);
		const auto emitted = host.Stats().downedEventsEmitted;

		const auto stale = SendVitals(host, 2, 10.5, false, true, 0);
		assert(CountOfType(stale, "died") == 0);
		assert(host.Stats().downedEventsEmitted == emitted);

		const auto afterGrace = SendVitals(host, 2, 18.0, false, true, 0);
		assert(FramesOfType(afterGrace, 2, "died").size() == 1);
		assert(FramesOfType(afterGrace, 1, "playerDied").size() == 1);
		assert(host.Stats().downedEventsEmitted == emitted + 1);
	}

	void TestReviveChannelHoldsAndCompletes()
	{
		ProtocolHost host;
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		const auto third = Join(host, 3, "profile-third");
		assert(rescuer != victim);
		assert(third != victim);
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		Move(host, 3, 2.1, 140.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

		const auto claim = SendReviveClaim(host, 1, victim, 1, 2.5);
		const auto started = FramesOfType(claim, 1, "reviveStarted");
		assert(started.size() == 1);
		assert(started.front().at("playerId").get<std::uint32_t>() == victim);
		assert(started.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
		assert(started.front().at("channelMs").get<std::uint32_t>() == 3000);
		assert(FramesOfType(claim, 2, "revived").empty());
		assert(host.Stats().reviveRequestsRejected == 0);

		assert(SendReviveClaim(host, 3, victim, 1, 2.6).empty());
		assert(host.Stats().reviveRequestsRejected == 1);

		assert(CountOfType(host.PollDownedState(5.0), "revived") == 0);

		const auto completed = host.PollDownedState(5.6);
		const auto revived = FramesOfType(completed, 2, "revived");
		assert(revived.size() == 1);
		assert(revived.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
		assert(revived.front().at("stimpaksSpent").get<std::uint32_t>() == 1);
		assert(FramesOfType(completed, 1, "revived").empty());
		const auto cleared = FramesOfType(completed, 1, "downedState");
		assert(cleared.size() == 1);
		assert(!cleared.front().at("downed").get<bool>());
	}

	void TestReviveNeedsStimpakAndHonoursCancel()
	{
		ProtocolHost host;
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		const auto bystander = Join(host, 3, "profile-bystander");
		assert(rescuer != victim);
		assert(bystander != victim);
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		Move(host, 3, 2.1, 120.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

		assert(SendReviveClaim(host, 1, victim, 0, 2.5).empty());
		assert(host.Stats().reviveRequestsRejected == 1);
		assert(CountOfType(host.PollDownedState(6.0), "revived") == 0);

		assert(FramesOfType(SendReviveClaim(host, 1, victim, 1, 6.5), 1, "reviveStarted").size() == 1);

		assert(SendReviveCancel(host, 3, victim, "movedAway", 6.6).empty());
		const auto cancelled = SendReviveCancel(host, 1, victim, "movedAway", 6.7);
		const auto cancelPacket = FramesOfType(cancelled, 1, "reviveCancelled");
		assert(cancelPacket.size() == 1);
		assert(cancelPacket.front().at("playerId").get<std::uint32_t>() == victim);
		assert(cancelPacket.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
		assert(cancelPacket.front().at("reason").get<std::string>() == "movedAway");
		assert(host.Stats().reviveCancelsReceived == 2);

		assert(host.Stats().packetsRejected == 0);
		assert(FramesOfType(SendReviveClaim(host, 1, victim, 1, 6.8), 1, "reviveStarted").size() == 1);
		assert(FramesOfType(host.PollDownedState(9.9), 2, "revived").size() == 1);
	}

	void TestReviveChannelEndToEndBroadcasts()
	{
		ProtocolHost host;
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		const auto watcher = Join(host, 3, "profile-watcher");
		assert(rescuer != victim);
		assert(watcher != victim);
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		Move(host, 3, 2.1, 120.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

		const auto claim = SendReviveClaim(host, 1, victim, 2, 2.5);
		for (const auto connection : { 1u, 2u, 3u }) {
			const auto started = FramesOfType(claim, connection, "reviveStarted");
			assert(started.size() == 1);
			assert(started.front().at("playerId").get<std::uint32_t>() == victim);
			assert(started.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
			assert(started.front().at("channelMs").get<std::uint32_t>() == 3000);
		}
		assert(CountOfType(claim, "revived") == 0);
		assert(CountOfType(claim, "reviveCancelled") == 0);
		assert(CountOfType(claim, "downedState") == 0);
		assert(host.Stats().reviveRequestsReceived == 1);
		assert(host.Stats().reviveRequestsRejected == 0);

		const auto cancel = SendReviveCancel(host, 1, victim, "released", 3.1);
		for (const auto connection : { 1u, 2u, 3u }) {
			const auto cancelled = FramesOfType(cancel, connection, "reviveCancelled");
			assert(cancelled.size() == 1);
			assert(cancelled.front().at("playerId").get<std::uint32_t>() == victim);
			assert(cancelled.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
			assert(cancelled.front().at("reason").get<std::string>() == "released");
		}
		assert(CountOfType(cancel, "revived") == 0);
		assert(CountOfType(cancel, "downedState") == 0);
		assert(host.Stats().reviveCancelsReceived == 1);
		assert(host.Stats().packetsRejected == 0);

		const auto secondCancel = SendReviveCancel(host, 1, victim, "released", 3.5);
		assert(CountOfType(secondCancel, "reviveCancelled") == 0);
		assert(host.Stats().reviveCancelsReceived == 2);
		assert(CountOfType(host.PollDownedState(3.6), "revived") == 0);
		assert(CountOfType(host.PollDownedState(3.6), "died") == 0);

		const auto reclaim = SendReviveClaim(host, 1, victim, 1, 4.0);
		for (const auto connection : { 1u, 2u, 3u }) {
			const auto started = FramesOfType(reclaim, connection, "reviveStarted");
			assert(started.size() == 1);
			assert(started.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
			assert(started.front().at("channelMs").get<std::uint32_t>() == 3000);
		}
		assert(host.Stats().reviveRequestsRejected == 0);
		assert(CountOfType(host.PollDownedState(6.9), "revived") == 0);

		const auto completed = host.PollDownedState(7.1);
		const auto revived = FramesOfType(completed, 2, "revived");
		assert(revived.size() == 1);
		assert(revived.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
		assert(revived.front().at("stimpaksSpent").get<std::uint32_t>() == 1);
		assert(revived.front().at("healthFraction").get<double>() == 0.5);
		assert(FramesOfType(completed, 1, "revived").empty());
		assert(FramesOfType(completed, 3, "revived").empty());
		for (const auto connection : { 1u, 2u, 3u }) {
			const auto cleared = FramesOfType(completed, connection, "downedState");
			assert(cleared.size() == 1);
			assert(cleared.front().at("playerId").get<std::uint32_t>() == victim);
			assert(!cleared.front().at("downed").get<bool>());
		}
		assert(CountOfType(completed, "reviveStarted") == 0);
		assert(CountOfType(completed, "reviveCancelled") == 0);
		assert(CountOfType(completed, "died") == 0);
	}

	void TestCheckpointRespawn()
	{
		ProtocolHost host;
		const auto victim = Join(host, 2, "profile-victim");
		assert(victim != 0);
		Move(host, 2, 2.0, 0.0, "spawn", "00000100", "0000F00D");
		assert(!SendVitals(host, 2, 2.1, true, false, 0).empty());

		assert(SendCheckpoint(host, 2, true, 0x200, 2.2).empty());
		assert(host.Stats().checkpointsReceived == 1);
		assert(host.Stats().checkpointsRejected == 1);

		assert(SendCheckpoint(host, 2, true, 0x100, 2.3).empty());
		assert(host.Stats().checkpointsReceived == 2);
		assert(host.Stats().checkpointsRejected == 1);

		assert(!host.PollDownedState(63.0).empty());
		const auto granted = SendRespawn(host, 2, "checkpoint", std::nullopt, 63.1);
		const auto grant = FramesOfType(granted, 2, "respawnGrant");
		assert(grant.size() == 1);
		assert(grant.front().at("point").get<std::string>() == "checkpoint");
		assert(grant.front().at("checkpoint").at("interior").get<bool>());
		assert(grant.front().at("checkpoint").at("space").get<std::uint32_t>() == 0x100);
		assert(grant.front().at("checkpoint").contains("rotZ"));
	}

	void TestReconnectKeepsDownedState()
	{
		ProtocolHost host;
		Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		(void)host.HandleDisconnected(2, 100.0);
		const auto frames = JoinFrames(host, 3, "profile-victim");
		const auto replayed = PlayerIdOf(frames, 3);
		assert(replayed != 0);

		const auto snapshot = FramesOfType(frames, 3, "downedState");
		assert(!snapshot.empty());
		assert(snapshot.front().at("playerId").get<std::uint32_t>() == replayed);
		assert(snapshot.front().at("downed").get<bool>());
		assert(FramesOfType(frames, 1, "downedState").size() == 1);

		(void)victim;
	}
}

void RunServerDownedRespawnAndChannelTests()
{
	TestRespawnGrantFlow();
	TestAdminReviveAndKill();
	TestAdminReviveClearsDeadClientState();
	TestRespawnRequestRateLimit();
	TestRespawningGraceExpires();
	TestReviveChannelHoldsAndCompletes();
	TestReviveNeedsStimpakAndHonoursCancel();
	TestReviveChannelEndToEndBroadcasts();
	TestCheckpointRespawn();
	TestReconnectKeepsDownedState();
}

