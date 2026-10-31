#include "ServerProtocol.h"

#include "F4TConnectionPolicy.h"

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
		assert(!a_host.SessionAfkExempt(kConnection));
		assert(Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", true } }, 21.0) == false);
		assert(a_host.SessionAfkExempt(kConnection));
		assert(!Send(a_host, Json{ { "type", "keepAlive" }, { "afkExempt", false } }, 22.0));
		assert(!a_host.SessionAfkExempt(kConnection));
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
			{ "actionEvents", Json::array({ Json{ { "type", 1 }, { "eventName", "meleeattackStart" } } }) }
		};
		assert(Send(a_host, actions, 82.0));
	}

	void ActivitySignalIsConsumedOnce(ProtocolHost& a_host)
	{
		assert(a_host.TakePlayerActivity(kConnection) == false);
	}
}

int main()
{
	using namespace F4T::ConnectionPolicy;

	ProtocolHost host;
	JoinPlayer(host);

	KeepAliveNeverCountsAsActivity(host);
	StandingHeartbeatNeverCountsAsActivity(host);
	MovedPlayerCountsAsActivity(host);
	ChatCountsAsActivity(host);
	VitalsOnlyPlayerStateDoesNotCount(host);
	PlayerCausedStateCounts(host);
	ActivitySignalIsConsumedOnce(host);

	const std::optional<double> lastActivity{ kJoinUnix };
	assert(!AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds, true));
	assert(!AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds - 1.0, true));
	assert(AfkExpired(lastActivity, kJoinUnix + DefaultAfkKickSeconds + 1.0, true));

	const std::optional<double> movedClock{ kJoinUnix + 600.0 };
	assert(!AfkExpired(movedClock, kJoinUnix + DefaultAfkKickSeconds + 1.0, true));

	assert(DefaultAfkKickSeconds == 900.0);

	return 0;
}
