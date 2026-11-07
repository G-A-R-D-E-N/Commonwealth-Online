#include "ServerDowned.h"
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
		assert(host.EnableFactions(nullptr));
		const auto friendPlayer = Join(host, 1, "profile-friend");
		const auto victim = Join(host, 2, "profile-victim");
		const auto stranger = Join(host, 3, "profile-stranger");
		assert(friendPlayer != victim);
		assert(stranger != friendPlayer && stranger != victim);

		const auto created = FramesOfType(host.HandleMessage(1, EncodeJson(Json{
			{ "type", "factionCreate" },
			{ "name", "Alpha Team" } }), 1.5, 100.5), 1, "factionResult");
		assert(created.size() == 1);
		assert(created.front().at("ok").get<bool>());
		const auto invited = host.HandleMessage(1, EncodeJson(Json{
			{ "type", "factionInvite" },
			{ "targetPlayerId", victim } }), 1.6, 100.6);
		const auto invites = FramesOfType(invited, 2, "factionInviteReceived");
		assert(invites.size() == 1);
		const auto joined = FramesOfType(host.HandleMessage(2, EncodeJson(Json{
			{ "type", "factionInviteResponse" },
			{ "inviteId", invites.front().at("inviteId").get<std::string>() },
			{ "accept", true } }), 1.7, 100.7), 2, "factionResult");
		assert(joined.size() == 1);
		assert(joined.front().at("ok").get<bool>());

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

		const auto strangerGrant = SendRespawn(host, 1, "friend", stranger, 122.5);
		assert(FramesOfType(strangerGrant, 1, "respawnGrant").empty());
		assert(host.Stats().respawnRequestsRejected == 3);

		const auto friendGrant = SendRespawn(host, 1, "friend", victim, 122.5);
		const auto friendPacket = FramesOfType(friendGrant, 1, "respawnGrant");
		assert(friendPacket.size() == 1);
		assert(friendPacket.front().at("point").get<std::string>() == "friend");
		assert(friendPacket.front().at("friendId").get<std::uint32_t>() == victim);

		assert(!host.AdminKillPlayer(victim).empty());
		assert(SendRespawn(host, 2, "moon", std::nullopt, 122.6).empty());
		assert(host.Stats().respawnRequestsRejected == 4);
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

	void TestInstantReviveAndAdminCancellation()
	{
		ProtocolHost host;
		F4T::DownedState::Config config;
		config.reviveChannelSeconds = 0;
		config.stimpaksPerRevive = 2;
		host.SetDownedConfig(config);
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
		const auto frames = SendReviveClaim(host, 1, victim, 2, 2.5);
		const auto started = FramesOfType(frames, 1, "reviveStarted");
		assert(started.size() == 1);
		assert(started.front().at("channelMs") == 0);
		assert(started.front().at("stimpaksSpent") == 2);
		assert(started.front().at("byPlayerId") == rescuer);
		assert(FramesOfType(frames, 2, "revived").size() == 1);

		ProtocolHost adminHost;
		Join(adminHost, 1, "profile-rescuer");
		const auto adminVictim = Join(adminHost, 2, "profile-victim");
		Move(adminHost, 1, 2.0, 0.0);
		Move(adminHost, 2, 2.1, 100.0);
		assert(!SendVitals(adminHost, 2, 2.2, true, false, 0).empty());
		assert(FramesOfType(SendReviveClaim(adminHost, 1, adminVictim, 1, 2.5), 1, "reviveStarted").size() == 1);
		const auto adminFrames = adminHost.AdminRevivePlayer(adminVictim, 2.6);
		const auto cancelled = FramesOfType(adminFrames, 1, "reviveCancelled");
		assert(cancelled.size() == 1);
		assert(cancelled.front().at("reason") == "adminRevive");
		assert(FramesOfType(adminFrames, 2, "revived").size() == 1);
	}

	void TestReviveStartedCarriesConfiguredCost()
	{
		for (const std::uint32_t cost : { 0U, 1U, 2U }) {
			ProtocolHost host;
			F4T::DownedState::Config config;
			config.stimpaksPerRevive = cost;
			host.SetDownedConfig(config);
			const auto rescuer = Join(host, 1, "profile-rescuer");
			const auto victim = Join(host, 2, "profile-victim");
			assert(rescuer != victim);
			Move(host, 1, 2.0, 0.0);
			Move(host, 2, 2.1, 100.0);
			assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

			const auto started = FramesOfType(SendReviveClaim(host, 1, victim, 2, 2.5), 1, "reviveStarted");
			assert(started.size() == 1);
			assert(started.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
			assert(started.front().at("stimpaksSpent").get<std::uint32_t>() == cost);

			const auto revived = FramesOfType(host.PollDownedState(5.6), 2, "revived");
			assert(revived.size() == 1);
			assert(revived.front().at("stimpaksSpent").get<std::uint32_t>() == cost);
		}
	}

	void TestHiddenReviverReceivesOwnIdentity()
	{
		using namespace CommonwealthOnline::Server;
		for (const double channelSeconds : { 0.0, 3.0 }) {
			ProtocolStats stats;
			DownedService service{ stats };
			F4T::DownedState::Config config;
			config.reviveChannelSeconds = channelSeconds;
			config.stimpaksPerRevive = 2;
			service.SetConfig(config);
			DownedService::Peers peers{
				DownedPeer{ 1, 1, DownedPosition{ 0, 0, 0, "cell", "world", 2.0 }, "rescuer", "", true },
				DownedPeer{ 2, 2, DownedPosition{ 100, 0, 0, "cell", "world", 2.0 }, "victim" },
				DownedPeer{ 3, 3, std::nullopt, "observer" }
			};
			service.Join(peers, 1, 1, "rescuer", 1.0);
			service.Join(peers, 2, 2, "victim", 1.0);
			service.ObserveVitals([&peers] { return peers; }, 2, true, false, 2.2, true);
			DownedSessionState session;
			const Json request{ { "type", "reviveRequest" }, { "target", 2 }, { "stimpaks", 2 } };
			const auto frames = service.Dispatch("reviveRequest", peers, 1, session, request, 2.5, 5.0);
			assert(frames.has_value());
			const auto own = FramesOfType(*frames, 1, "reviveStarted");
			const auto observer = FramesOfType(*frames, 3, "reviveStarted");
			assert(own.size() == 1 && own.front().at("byPlayerId") == 1);
			assert(own.front().at("stimpaksSpent") == 2);
			assert(observer.size() == 1 && observer.front().at("byPlayerId") == 0);
		}
	}

	void TestHiddenReviverStillClearsVisibleTarget()
	{
		ProtocolHost host;
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		Join(host, 3, "profile-watcher");
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
		assert(!SendReviveClaim(host, 1, victim, 1, 2.5).empty());
		host.HandleMessage(1, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 1 } }), 2.6, 102.6);
		const auto completed = host.PollDownedState(5.6);
		const auto visibleUpdate = FramesOfType(completed, 3, "downedState");
		assert(visibleUpdate.size() == 1);
		assert(visibleUpdate.front().at("playerId") == victim);
		assert(visibleUpdate.front().at("downed") == false);
		const auto revived = FramesOfType(completed, 2, "revived");
		assert(revived.size() == 1 && revived.front().at("byPlayerId") == 0);
		ProtocolHost cancelledHost;
		Join(cancelledHost, 1, "profile-canceller");
		const auto cancelledVictim = Join(cancelledHost, 2, "profile-cancelled");
		Join(cancelledHost, 3, "profile-observer");
		Move(cancelledHost, 1, 2.0, 0.0);
		Move(cancelledHost, 2, 2.1, 100.0);
		SendVitals(cancelledHost, 2, 2.2, true, false, 0);
		SendReviveClaim(cancelledHost, 1, cancelledVictim, 1, 2.5);
		cancelledHost.HandleMessage(1, EncodeJson(Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", 1 } }), 2.6, 102.6);
		const auto cancelled = SendReviveCancel(cancelledHost, 1, cancelledVictim, "released", 2.7);
		const auto visibleCancel = FramesOfType(cancelled, 3, "reviveCancelled");
		assert(visibleCancel.size() == 1 && visibleCancel.front().at("byPlayerId") == 0);
		(void)rescuer;
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

	void TestOmittedStimpakCountFollowsCapability()
	{
		ProtocolHost host;
		UseInstantRevive(host);
		const auto reportingRescuer = JoinWithCapabilities(host, 1, "profile-reporting", Json::array({ std::string{ F4T::DownedState::StimpakCountCapability } }));
		const auto victim = Join(host, 2, "profile-victim");
		const auto legacyRescuer = Join(host, 3, "profile-legacy");
		assert(reportingRescuer != victim);
		assert(legacyRescuer != victim);
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 100.0);
		Move(host, 3, 2.0, 0.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

		const auto omitted = [&](std::uint32_t a_connection, double a_monotonic) {
			return host.HandleMessage(a_connection, EncodeJson(Json{
				{ "type", "reviveRequest" },
				{ "target", victim } }), a_monotonic, 100.0);
		};
		assert(omitted(1, 2.5).empty());
		assert(host.Stats().reviveRequestsRejected == 1);
		assert(CountOfType(host.PollDownedState(2.6), "revived") == 0);

		const auto accepted = FramesOfType(omitted(3, 2.7), 2, "revived");
		assert(accepted.size() == 1);
		assert(accepted.front().at("stimpaksSpent").get<std::uint32_t>() == 0);
		assert(host.Stats().reviveRequestsRejected == 1);
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
	TestReviveStartedCarriesConfiguredCost();
	TestInstantReviveAndAdminCancellation();
	TestHiddenReviverReceivesOwnIdentity();
	TestHiddenReviverStillClearsVisibleTarget();
	TestReviveNeedsStimpakAndHonoursCancel();
	TestOmittedStimpakCountFollowsCapability();
	TestReviveChannelEndToEndBroadcasts();
	TestCheckpointRespawn();
	TestReconnectKeepsDownedState();
}

