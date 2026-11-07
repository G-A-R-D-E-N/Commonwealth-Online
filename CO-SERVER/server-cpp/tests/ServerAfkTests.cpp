#include "ServerProtocol.h"

#include "F4TConnectionPolicy.h"
#include "ServerAfk.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
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

	constexpr std::uint32_t kConnection = 41;
	constexpr double kJoinMonotonic = 10.0;
	constexpr double kJoinUnix = 1'700'000'000.0;

	void JoinPlayer(ProtocolHost& a_host)
	{
		(void)a_host.HandleConnected(kConnection, kJoinUnix);
		const auto frames = a_host.HandleMessage(
			kConnection,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
			kJoinMonotonic,
			kJoinUnix + 0.1);
		assert(!frames.empty());
	}

	Json Transform(double a_x, double a_angleZ, std::string_view a_movementType = "normal")
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "D00D" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", a_angleZ },
			{ "movementType", std::string{ a_movementType } },
			{ "movementSpeed", 100.0 }
		};
	}

	bool Send(ProtocolHost& a_host, const Json& a_packet, double a_monotonicSeconds)
	{
		(void)a_host.HandleMessage(kConnection, EncodeJson(a_packet), a_monotonicSeconds, kJoinUnix + a_monotonicSeconds);
		return a_host.TakePlayerActivity(kConnection);
	}

	void KeepAliveNeverCountsAsActivity(ProtocolHost& a_host)
	{
		assert(!Send(a_host, Json{ { "type", "keepAlive" } }, 20.0));
		assert(!a_host.SessionAfkExemption(kConnection).reported);
		assert(Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", true } }, 21.0) == false);
		assert(a_host.SessionAfkExemption(kConnection).reported);
		assert(a_host.SessionAfkExemption(kConnection).reportedAtUnixSeconds == kJoinUnix + 21.0);
		assert(!Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", false } }, 22.0));
		assert(!a_host.SessionAfkExemption(kConnection).reported);
	}

	void HeartbeatOnlyPlayerIsKickedAt900Seconds()
	{
		using CommonwealthOnline::Server::Afk::Evaluate;
		using CommonwealthOnline::Server::Afk::Exemption;
		using CommonwealthOnline::Server::Afk::Verdict;

		std::optional<double> clock{ kJoinUnix };
		for (double tick = 2.0; tick < 900.0; tick += 2.0) {
			assert(Evaluate(clock, Exemption{}, kJoinUnix + tick, true, false) == Verdict::none);
		}
		assert(Evaluate(clock, Exemption{}, kJoinUnix + 900.0, true, false) == Verdict::none);
		assert(Evaluate(clock, Exemption{}, kJoinUnix + 900.5, true, false) == Verdict::kick);
	}

	void ExemptionExpiresWithTheKeepAliveStream()
	{
		using CommonwealthOnline::Server::Afk::Evaluate;
		using CommonwealthOnline::Server::Afk::Exemption;
		using CommonwealthOnline::Server::Afk::Verdict;

		const Exemption exemption{ true, kJoinUnix, kJoinUnix };
		std::optional<double> clock{ kJoinUnix };
		assert(Evaluate(clock, exemption, kJoinUnix + 10.0, true, false) == Verdict::exempt);
		assert(clock == kJoinUnix + 10.0);
		assert(Evaluate(clock, exemption, kJoinUnix + 45.0, true, false) == Verdict::exempt);
		assert(clock == kJoinUnix + 45.0);
		assert(Evaluate(clock, exemption, kJoinUnix + 46.0, true, false) == Verdict::none);
		assert(clock == kJoinUnix + 45.0);
		assert(Evaluate(clock, exemption, kJoinUnix + 45.0 + 900.0, true, false) == Verdict::none);
		assert(Evaluate(clock, exemption, kJoinUnix + 45.0 + 901.0, true, false) == Verdict::kick);
	}

	void OnlyAnActivePlaySessionCanBeKicked()
	{
		using CommonwealthOnline::Server::Afk::Evaluate;
		using CommonwealthOnline::Server::Afk::Exemption;
		using CommonwealthOnline::Server::Afk::Verdict;

		std::optional<double> clock{ kJoinUnix };
		assert(Evaluate(clock, Exemption{}, kJoinUnix + 5000.0, false, false) == Verdict::none);
		assert(Evaluate(clock, Exemption{}, kJoinUnix + 5000.0, true, true) == Verdict::none);
		assert(Evaluate(clock, Exemption{}, kJoinUnix + 901.0, true, false) == Verdict::kick);
	}

	void StandingHeartbeatNeverCountsAsActivity(ProtocolHost& a_host)
	{
		assert(Send(a_host, Transform(0.0, 0.0, "spawn"), 30.0));
		const auto heartbeat = Transform(0.0, 0.0);
		for (double tick = 32.0; tick < 40.0; tick += 2.0) {
			assert(!Send(a_host, heartbeat, tick));
		}
	}

	void MovedPlayerCountsAsActivity(ProtocolHost& a_host)
	{
		assert(Send(a_host, Transform(40.0, 0.0), 50.0));
		assert(Send(a_host, Transform(40.0, 1.5), 52.0));
	}

	void ChatCountsAsActivity(ProtocolHost& a_host)
	{
		assert(Send(a_host, Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "still here" } }, 60.0));
	}

	void VitalsOnlyPlayerStateDoesNotCount(ProtocolHost& a_host)
	{
		const Json vitals{
			{ "type", "playerState" },
			{ "vitals", Json{ { "health", 90.0 }, { "maxHealth", 100.0 } } }
		};
		assert(!Send(a_host, vitals, 70.0));
	}

	void PlayerCausedStateCounts(ProtocolHost& a_host)
	{
		const Json equipped{
			{ "type", "playerState" },
			{ "equippedItems", Json::array() }
		};
		assert(Send(a_host, equipped, 80.0));
		const Json actions{
			{ "type", "playerState" },
			{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 1 }, { "eventName", "meleeattackStart" } } }) }
		};
		assert(Send(a_host, actions, 82.0));
	}

	void ActivitySignalIsConsumedOnce(ProtocolHost& a_host)
	{
		assert(a_host.TakePlayerActivity(kConnection) == false);
	}

	void ExemptionStreakIsCapped()
	{
		using CommonwealthOnline::Server::Afk::Evaluate;
		using CommonwealthOnline::Server::Afk::Exemption;
		using CommonwealthOnline::Server::Afk::ExemptionLive;
		using CommonwealthOnline::Server::Afk::Verdict;

		const Exemption inside{ true, kJoinUnix + 600.0, kJoinUnix };
		assert(ExemptionLive(inside, kJoinUnix + 600.0));
		std::optional<double> clock{ kJoinUnix + 600.0 };
		assert(Evaluate(clock, inside, kJoinUnix + 600.0, true, false) == Verdict::exempt);

		const Exemption capped{ true, kJoinUnix + 610.0, kJoinUnix };
		assert(!ExemptionLive(capped, kJoinUnix + 610.0));
		assert(Evaluate(clock, capped, kJoinUnix + 610.0, true, false) == Verdict::none);
		assert(clock == kJoinUnix + 600.0);
		assert(Evaluate(clock, capped, kJoinUnix + 1500.0, true, false) == Verdict::none);
		assert(Evaluate(clock, capped, kJoinUnix + 1500.5, true, false) == Verdict::kick);
	}

	void KeepAliveStreakExpiresAfterTheCap(ProtocolHost& a_host)
	{
		using CommonwealthOnline::Server::Afk::ExemptionLive;

		const Json keepAlive{ { "type", "keepAlive" }, { "afkExempt", true } };
		assert(!Send(a_host, keepAlive, 200.0));
		for (double tick = 230.0; tick <= 800.0; tick += 30.0) {
			assert(!Send(a_host, keepAlive, tick));
			assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == kJoinUnix + 200.0);
		}
		assert(ExemptionLive(a_host.SessionAfkExemption(kConnection), kJoinUnix + 800.0));
		assert(!Send(a_host, keepAlive, 830.0));
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == kJoinUnix + 200.0);
		assert(!ExemptionLive(a_host.SessionAfkExemption(kConnection), kJoinUnix + 830.0));
	}

	void GenuineActivityRestartsTheExemptionStreak(ProtocolHost& a_host)
	{
		const Json keepAlive{ { "type", "keepAlive" }, { "afkExempt", true } };
		assert(!Send(a_host, keepAlive, 900.0));
		assert(!Send(a_host, keepAlive, 910.0));
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == kJoinUnix + 900.0);
		assert(Send(a_host, Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "back" } }, 920.0));
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == 0.0);
		assert(!Send(a_host, keepAlive, 930.0));
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == kJoinUnix + 930.0);
	}

	void ExemptFalseOrMissingClearsTheStreak(ProtocolHost& a_host)
	{
		assert(!Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", true } }, 1000.0));
		assert(!Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", false } }, 1002.0));
		assert(!a_host.SessionAfkExemption(kConnection).reported);
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == 0.0);
		assert(!Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", true } }, 1004.0));
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == kJoinUnix + 1004.0);
		assert(!Send(a_host, Json{ { "type", "keepAlive" } }, 1006.0));
		assert(!a_host.SessionAfkExemption(kConnection).reported);
		assert(a_host.SessionAfkExemption(kConnection).streakStartUnixSeconds == 0.0);
	}

	void UnknownAndRejectedMessagesAreNotActivity(ProtocolHost& a_host)
	{
		const auto rejected = a_host.Stats().packetsRejected;
		assert(!Send(a_host, Json{ { "type", "zzNotAMessage" } }, 1100.0));
		assert(a_host.Stats().packetsRejected == rejected + 1);
		assert(!Send(a_host, Json{ { "type", "playerState" }, { "equippedItems", "bad" } }, 1102.0));
		assert(a_host.Stats().packetsRejected == rejected + 2);
	}

	void MutedChatIsNotActivity(ProtocolHost& a_host)
	{
		a_host.BindSessionProfile(kConnection, "profile-afk");
		a_host.SetChatMuted("profile-afk", true);
		const auto rejected = a_host.Stats().chatRejected;
		assert(!Send(a_host, Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "muted" } }, 1200.0));
		assert(a_host.Stats().chatRejected == rejected + 1);
		a_host.SetChatMuted("profile-afk", false);
		assert(Send(a_host, Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "unmuted" } }, 1202.0));
	}

	void SessionEndedIsSentBeforeTheDisconnect()
	{
		std::vector<std::string> order;
		CommonwealthOnline::Server::Afk::EndSessionAfterNotifying(
			77,
			[&order](std::uint32_t a_connection) { order.push_back("notify:" + std::to_string(a_connection)); },
			[&order](std::uint32_t a_connection) { order.push_back("disconnect:" + std::to_string(a_connection)); });
		assert(order.size() == 2);
		assert(order[0] == "notify:77");
		assert(order[1] == "disconnect:77");
	}
}

int main()
{
	using namespace F4T::ConnectionPolicy;

	ProtocolHost host;
	(void)host.HandleConnected(kConnection, kJoinUnix);
	const auto rejected = host.Stats().packetsRejected;
	assert(!Send(host, Json{ { "type", "keepAlive" }, { "afkExempt", "invalid" } }, 9.0));
	assert(host.Stats().packetsRejected == rejected + 1);
	(void)host.HandleDisconnected(kConnection, kJoinUnix);
	JoinPlayer(host);
	host.SetChatEnabled(true);

	KeepAliveNeverCountsAsActivity(host);
	HeartbeatOnlyPlayerIsKickedAt900Seconds();
	ExemptionExpiresWithTheKeepAliveStream();
	OnlyAnActivePlaySessionCanBeKicked();
	StandingHeartbeatNeverCountsAsActivity(host);
	MovedPlayerCountsAsActivity(host);
	ChatCountsAsActivity(host);
	VitalsOnlyPlayerStateDoesNotCount(host);
	PlayerCausedStateCounts(host);
	ActivitySignalIsConsumedOnce(host);
	ExemptionStreakIsCapped();
	KeepAliveStreakExpiresAfterTheCap(host);
	GenuineActivityRestartsTheExemptionStreak(host);
	ExemptFalseOrMissingClearsTheStreak(host);
	UnknownAndRejectedMessagesAreNotActivity(host);
	MutedChatIsNotActivity(host);
	SessionEndedIsSentBeforeTheDisconnect();

	const std::optional<double> lastActivity{ kJoinUnix };
	assert(!AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds, true));
	assert(!AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds - 1.0, true));
	assert(AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds + 1.0, true));

	const std::optional<double> movedClock{ kJoinUnix + 600.0 };
	assert(!AfkExpired(movedClock, kJoinUnix + DefaultAfkKickSeconds + 1.0, true));

	assert(DefaultAfkKickSeconds == 900.0);

	return 0;
}
