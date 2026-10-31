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

	void TestDownedSecondsOption()
	{
		const auto defaults = CommonwealthOnline::Server::Configuration::Parse(Json::object());
		assert(defaults.downedSeconds == 60);
		assert(CommonwealthOnline::Server::Configuration::Validate(defaults).empty());

		assert(CommonwealthOnline::Server::Configuration::Parse(Json{ { "downed_seconds", 15 } }).downedSeconds == 15);
		assert(CommonwealthOnline::Server::Configuration::Parse(Json{ { "downedSeconds", 90 } }).downedSeconds == 90);
		assert(!CommonwealthOnline::Server::Configuration::Validate(CommonwealthOnline::Server::Configuration::Parse(Json{ { "downed_seconds", 4294967356LL } })).empty());

		auto tooLow = defaults;
		tooLow.downedSeconds = 4;
		assert(!CommonwealthOnline::Server::Configuration::Validate(tooLow).empty());
		auto tooHigh = defaults;
		tooHigh.downedSeconds = 601;
		assert(!CommonwealthOnline::Server::Configuration::Validate(tooHigh).empty());

		assert(defaults.reviveChannelSeconds == 3);
		assert(CommonwealthOnline::Server::Configuration::Parse(Json{ { "revive_channel_seconds", 0 } }).reviveChannelSeconds == 0);
		assert(CommonwealthOnline::Server::Configuration::Parse(Json{ { "reviveChannelSeconds", 15 } }).reviveChannelSeconds == 15);

		auto tooLong = defaults;
		tooLong.reviveChannelSeconds = 16;
		assert(!CommonwealthOnline::Server::Configuration::Validate(tooLong).empty());
		auto noChannel = defaults;
		noChannel.reviveChannelSeconds = 0;
		assert(CommonwealthOnline::Server::Configuration::Validate(noChannel).empty());
	}

	void TestPluginVitalsShapeStartsDownedTimer()
	{
		ProtocolHost host;
		const auto reviver = Join(host, 1, "profile-reviver");
		const auto downed = Join(host, 2, "profile-downed");
		assert(reviver != downed);

		const auto frames = SendVitals(host, 2, 2.0, true, false, 0);
		const auto broadcast = FramesOfType(frames, 1, "downedState");
		assert(broadcast.size() == 1);
		assert(broadcast.front().at("playerId").get<std::uint32_t>() == downed);
		assert(broadcast.front().at("downed").get<bool>());
		assert(broadcast.front().at("remainingMs").get<std::uint32_t>() == 60000);
		assert(broadcast.front().at("remainingMs").is_number_unsigned());
		assert(FramesOfType(frames, 2, "downedState").size() == 1);
		assert(host.Stats().downedEventsEmitted == 1);

		const auto healthy = SendVitals(host, 1, 2.1, false, false, 100);
		assert(CountOfType(healthy, "downedState") == 0);
		assert(host.Stats().downedEventsEmitted == 1);
	}

	void TestDownedStateBroadcastCadence()
	{
		ProtocolHost host;
		Join(host, 1, "profile-a");
		const auto downed = Join(host, 2, "profile-b");
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		const auto first = host.PollDownedState(2.1);
		assert(FramesOfType(first, 1, "downedState").size() == 1);

		const auto immediate = host.PollDownedState(2.6);
		assert(CountOfType(immediate, "downedState") == 0);

		const auto later = host.PollDownedState(3.2);
		const auto snapshots = FramesOfType(later, 1, "downedState");
		assert(snapshots.size() == 1);
		assert(snapshots.front().at("playerId").get<std::uint32_t>() == downed);
		assert(snapshots.front().at("remainingMs").get<std::uint32_t>() < 60000);
	}

	void TestExpiryEmitsDiedAndPlayerDied()
	{
		ProtocolHost host;
		const auto watcher = Join(host, 1, "profile-watcher");
		const auto victim = Join(host, 2, "profile-victim");
		assert(watcher != victim);
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());

		const auto frames = host.PollDownedState(62.0);
		const auto died = FramesOfType(frames, 2, "died");
		assert(died.size() == 1);
		assert(died.front().at("playerId").get<std::uint32_t>() == victim);
		assert(FramesOfType(frames, 1, "died").empty());

		const auto playerDied = FramesOfType(frames, 1, "playerDied");
		assert(playerDied.size() == 1);
		assert(playerDied.front().at("playerId").get<std::uint32_t>() == victim);
		assert(FramesOfType(frames, 2, "playerDied").size() == 1);
		assert(FramesOfType(frames, 2, "downedState").empty());
	}

	void TestReviveAcceptedAndRangeEnforced()
	{
		ProtocolHost host;
		UseInstantRevive(host);
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 280.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());

		const auto tooFar = SendRevive(host, 1, victim, 2.3);
		assert(tooFar.empty());
		assert(host.Stats().reviveRequestsReceived == 1);
		assert(host.Stats().reviveRequestsRejected == 1);

		Move(host, 1, 2.4, 200.0, "normal");
		const auto frames = SendRevive(host, 1, victim, 2.5);
		const auto revived = FramesOfType(frames, 2, "revived");
		assert(revived.size() == 1);
		assert(revived.front().at("byPlayerId").get<std::uint32_t>() == rescuer);
		assert(revived.front().at("byPlayerId").is_number_unsigned());
		assert(revived.front().at("healthFraction").get<double>() == 0.5);
		assert(FramesOfType(frames, 1, "revived").empty());

		const auto cleared = FramesOfType(frames, 1, "downedState");
		assert(cleared.size() == 1);
		assert(!cleared.front().at("downed").get<bool>());
		assert(cleared.front().at("playerId").get<std::uint32_t>() == victim);
		assert(host.Stats().reviveRequestsRejected == 1);
	}

	void TestReviveRejectsUntrustedClientState()
	{
		ProtocolHost host;
		const auto rescuer = Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 0.0);
		assert(rescuer != victim);

		assert(SendRevive(host, 1, victim, 2.2).empty());
		assert(SendRevive(host, 1, victim, 2.3).empty());
		assert(SendRevive(host, 1, victim, 2.4).empty());
		assert(SendRevive(host, 1, victim, 2.5).empty());
		assert(host.Stats().reviveRequestsReceived == 4);
		assert(host.Stats().reviveRequestsRejected == 4);
		assert(host.Stats().downedEventsEmitted == 0);

		assert(SendRevive(host, 2, victim, 2.6).empty());
		assert(host.Stats().reviveRequestsRejected == 5);

		const auto malformed = host.HandleMessage(1, EncodeJson(Json{ { "type", "reviveRequest" } }), 2.7, 100.0);
		assert(malformed.empty());
		assert(host.Stats().reviveRequestsRejected == 6);
		assert(host.Stats().reviveRequestsReceived == 6);
	}

	void TestReviveRequiresFreshSharedTransform()
	{
		{
			ProtocolHost host;
			UseInstantRevive(host);
			Join(host, 1, "profile-rescuer");
			const auto victim = Join(host, 2, "profile-victim");
			assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());
			assert(SendRevive(host, 1, victim, 2.1).empty());
			Move(host, 1, 2.2, 0.0);
			assert(SendRevive(host, 1, victim, 2.3).empty());
			assert(host.Stats().reviveRequestsRejected == 2);
			assert(host.Stats().downedEventsEmitted == 1);
			Move(host, 2, 2.4, 0.0);
			assert(FramesOfType(SendRevive(host, 1, victim, 4.5), 2, "revived").size() == 1);
		}

		{
			ProtocolHost host;
			Join(host, 1, "profile-rescuer");
			const auto victim = Join(host, 2, "profile-victim");
			Move(host, 1, 2.0, 0.0);
			Move(host, 2, 2.1, 0.0, "spawn", "200");
			assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
			assert(SendRevive(host, 1, victim, 2.3).empty());
			assert(host.Stats().reviveRequestsRejected == 1);
			assert(host.Stats().downedEventsEmitted == 1);
		}

		{
			ProtocolHost host;
			Join(host, 1, "profile-rescuer");
			const auto victim = Join(host, 2, "profile-victim");
			Move(host, 1, 2.0, 0.0);
			Move(host, 2, 2.1, 0.0, "spawn", "100", "BEEF");
			assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
			assert(SendRevive(host, 1, victim, 2.3).empty());
			assert(host.Stats().reviveRequestsRejected == 1);
			assert(host.Stats().downedEventsEmitted == 1);
		}

		{
			ProtocolHost host;
			UseInstantRevive(host);
			Join(host, 1, "profile-rescuer");
			const auto victim = Join(host, 2, "profile-victim");
			Move(host, 1, 2.0, 0.0);
			Move(host, 2, 2.1, 0.0);
			assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
			assert(SendRevive(host, 1, victim, 8.0).empty());
			Move(host, 1, 8.1, 0.0, "normal");
			assert(SendRevive(host, 1, victim, 9.2).empty());
			assert(host.Stats().reviveRequestsRejected == 2);
			assert(host.Stats().downedEventsEmitted == 1);
			Move(host, 2, 9.3, 0.0, "normal");
			assert(FramesOfType(SendRevive(host, 1, victim, 10.5), 2, "revived").size() == 1);
		}
	}

	void TestStaleVitalsDoNotUndoRevive()
	{
		ProtocolHost host;
		UseInstantRevive(host);
		Join(host, 1, "profile-rescuer");
		const auto victim = Join(host, 2, "profile-victim");
		Move(host, 1, 2.0, 0.0);
		Move(host, 2, 2.1, 0.0);
		assert(!SendVitals(host, 2, 2.2, true, false, 0).empty());
		assert(FramesOfType(SendRevive(host, 1, victim, 2.3), 2, "revived").size() == 1);
		const auto emitted = host.Stats().downedEventsEmitted;

		const auto stale = SendVitals(host, 2, 2.4, true, false, 0);
		assert(CountOfType(stale, "downedState") == 0);
		assert(host.Stats().downedEventsEmitted == emitted);
		assert(CountOfType(host.PollDownedState(80.0), "died") == 0);
		assert(host.Stats().downedEventsEmitted == emitted);

		(void)SendVitals(host, 2, 4.4, false, false, 50);
		const auto downedAgain = SendVitals(host, 2, 6.4, true, false, 0);
		assert(FramesOfType(downedAgain, 1, "downedState").size() == 1);
		assert(host.Stats().downedEventsEmitted == emitted + 1);
	}

	void TestStaleVitalsDoNotUndoRespawn()
	{
		ProtocolHost host;
		Join(host, 1, "profile-watcher");
		Join(host, 2, "profile-victim");
		assert(!SendVitals(host, 2, 2.0, true, false, 0).empty());
		assert(!host.PollDownedState(62.0).empty());
		assert(FramesOfType(SendRespawn(host, 2, "sanctuary", std::nullopt, 62.1), 2, "respawnGrant").size() == 1);
		const auto emitted = host.Stats().downedEventsEmitted;

		const auto stale = SendVitals(host, 2, 62.2, false, true, 0);
		assert(CountOfType(stale, "died") == 0);
		assert(CountOfType(stale, "playerDied") == 0);
		assert(host.Stats().downedEventsEmitted == emitted);
		assert(SendRespawn(host, 2, "sanctuary", std::nullopt, 62.3).empty());

		(void)SendVitals(host, 2, 64.2, false, false, 100);
		const auto died = SendVitals(host, 2, 66.2, false, true, 0);
		assert(FramesOfType(died, 2, "died").size() == 1);
		assert(host.Stats().downedEventsEmitted == emitted + 1);
	}
}

void RunServerDownedRespawnAndChannelTests();

int main()
{
	TestDownedSecondsOption();
	TestPluginVitalsShapeStartsDownedTimer();
	TestDownedStateBroadcastCadence();
	TestExpiryEmitsDiedAndPlayerDied();
	TestReviveAcceptedAndRangeEnforced();
	TestReviveRejectsUntrustedClientState();
	TestReviveRequiresFreshSharedTransform();
	TestStaleVitalsDoNotUndoRevive();
	TestStaleVitalsDoNotUndoRespawn();
	RunServerDownedRespawnAndChannelTests();
	return 0;
}
