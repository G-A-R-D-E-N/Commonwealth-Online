#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	Json EntryFor(const Json& a_players, std::uint32_t a_playerId)
	{
		for (const auto& entry : a_players) {
			if (entry.at("playerId") == a_playerId) return entry;
		}
		assert(false);
		return {};
	}

	void TestCapabilityIsAdvertisedInWelcome()
	{
		ProtocolHost host{ "Map Presence Capability" };
		const auto capabilities = RequirePacket(host.HandleConnected(10, 100.0), 10, "welcome").at("capabilities");
		assert(std::find(capabilities.begin(), capabilities.end(), Json("map-presence-v1")) != capabilities.end());
	}

	void TestNoSessionsProducesNoPresence()
	{
		ProtocolHost host{ "Map Presence Empty" };
		assert(host.PollPresence(0.0, 100.0).empty());
	}

	void TestPresenceBroadcastsMovedPlayersOncePerSecond()
	{
		ProtocolHost host{ "Map Presence Broadcast" };
		assert(host.EnableFactions(nullptr));
		const auto alpha = Join(host, 10, "p10", 1.0, 100.0);
		const auto bravo = Join(host, 20, "p20", 1.1, 100.2);
		const auto charlie = Join(host, 30, "p30", 1.2, 100.4);
		AssertResult(Send(host, 10, Create("Alpha Team", "ALP"), 1.5, 100.6), 10, "create", true);
		host.BindSessionSteamId(10, "76561198000000001");
		Send(host, 10, Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad" },
			{ "vitals", Json{ { "healthPercent", 0 }, { "isDowned", true }, { "isDead", false } } }
		}, 1.6, 100.7);
		auto elevated = Transform(100.0);
		elevated["z"] = 42.5;
		Send(host, 10, elevated, 2.0, 101.0);
		Send(host, 20, Transform(200.0), 2.1, 101.1);

		const auto first = host.PollPresence(0.0, 101.2);
		assert(HasPacket(first, 10, "mapPresence") && HasPacket(first, 20, "mapPresence") && HasPacket(first, 30, "mapPresence"));
		const auto* frame = FindFrame(first, 10, "mapPresence");
		assert(frame->delivery == CommonwealthOnline::Server::Delivery::ReliableOrdered);
		const auto presence = DecodePacket(*frame);
		assert(presence.at("serverTime") == 101.2);
		const auto& players = presence.at("players");
		assert(players.size() == 2);
		for (const auto& entry : players) assert(entry.at("playerId") != charlie.playerId);

		const auto self = EntryFor(players, alpha.playerId);
		assert(self.size() == 12);
		assert(self.at("friend") == false);
		assert(self.at("x") == 100.0 && self.at("y") == 0.0 && self.at("z") == 42.5 && self.at("angleZ") == 0.0);
		assert(self.at("worldspaceId") == "0000F00D" && self.at("cellId") == "0000CAFE");
		assert(self.at("name") == "Nomad" && self.at("tag") == "ALP");
		assert(self.at("downed") == true && self.at("steamId") == "76561198000000001");

		const auto other = EntryFor(players, bravo.playerId);
		assert(other.at("x") == 200.0 && other.at("name") == "Unknown" && other.at("tag") == "");
		assert(other.at("downed") == false && other.at("steamId") == "");

		assert(host.PollPresence(0.5, 101.7).empty());
		const auto third = host.PollPresence(1.1, 102.3);
		assert(RequirePacket(third, 10, "mapPresence").at("players").size() == 2);

		host.HandleDisconnected(20, 102.5);
		const auto fourth = host.PollPresence(2.2, 103.4);
		assert(!HasPacket(fourth, 20, "mapPresence"));
		const auto remaining = RequirePacket(fourth, 10, "mapPresence").at("players");
		assert(remaining.size() == 1 && EntryFor(remaining, alpha.playerId).at("name") == "Nomad");
	}

	void TestPresenceIsCappedAtSessionLimit()
	{
		ProtocolHost host{ "Map Presence Cap" };
		for (std::uint32_t number = 1; number <= 101; ++number) {
			const auto digits = std::to_string(1000 + number).substr(1);
			const auto connectionId = 100 + number;
			Join(host, connectionId, "m" + digits, 1.0 + number * 0.01, 100.0 + number * 0.01);
			Send(host, connectionId, Json{ { "type", "playerState" }, { "characterName", std::string(128, '\x01') } }, 1.5 + number * 0.01, 100.5 + number * 0.01);
			Send(host, connectionId, Transform(100.0 + number), 2.0 + number * 0.01, 101.0 + number * 0.01);
		}
		const auto frames = host.PollPresence(0.0, 200.0);
		const auto* frame = FindFrame(frames, 101, "mapPresence");
		assert(frame != nullptr && frame->payload.size() < 64 * 1024);
		const auto presence = DecodePacket(*frame);
		assert(presence.at("players").size() == 100);
		for (const auto& player : presence.at("players")) assert(player.at("name") == "Player");
	}
	struct Roster
	{
		std::uint32_t alice{};
		std::uint32_t ann{};
		std::uint32_t bob{};
		std::uint32_t carl{};
	};

	Roster BuildRoster(ProtocolHost& a_host)
	{
		assert(a_host.EnableFactions(nullptr));
		Roster roster;
		roster.alice = Join(a_host, 10, "p10", 1.0, 100.0).playerId;
		roster.ann = Join(a_host, 11, "p11", 1.1, 100.1).playerId;
		roster.bob = Join(a_host, 20, "p20", 1.2, 100.2).playerId;
		roster.carl = Join(a_host, 30, "p30", 1.3, 100.3).playerId;
		AssertResult(Send(a_host, 10, Create("Alpha Team", "ALP"), 1.5, 100.6), 10, "create", true);
		const auto invite = Send(a_host, 10, Invite(roster.ann), 1.6, 100.7);
		AssertResult(Send(a_host, 11, Respond(InviteIdFrom(invite, 11), true), 1.7, 100.8), 11, "inviteResponse", true);
		AssertResult(Send(a_host, 20, Create("Bravo Team", "BRV"), 1.8, 100.9), 20, "create", true);
		const std::uint32_t connections[] = { 10, 11, 20, 30 };
		for (const auto connectionId : connections) Send(a_host, connectionId, Transform(100.0 * connectionId), 2.0, 101.0);
		return roster;
	}

	std::vector<std::uint32_t> VisibleIds(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<std::uint32_t> ids;
		const auto packet = RequirePacket(a_frames, a_connectionId, "mapPresence");
		for (const auto& entry : packet.at("players")) ids.push_back(entry.at("playerId"));
		std::sort(ids.begin(), ids.end());
		return ids;
	}

	std::vector<std::uint32_t> Sorted(std::vector<std::uint32_t> a_ids)
	{
		std::sort(a_ids.begin(), a_ids.end());
		return a_ids;
	}

	void TestPublicPlayersVisibleByDefaultSendsEveryone()
	{
		ProtocolHost host{ "Map Presence Public" };
		assert(host.MapPublicPlayers());
		const auto roster = BuildRoster(host);
		const auto frames = host.PollPresence(0.0, 110.0);
		const auto everyone = Sorted({ roster.alice, roster.ann, roster.bob, roster.carl });
		for (const auto connectionId : { 10u, 11u, 20u, 30u }) {
			assert(VisibleIds(frames, connectionId) == everyone);
			assert(RequirePacket(frames, connectionId, "mapPresence").at("publicPlayersVisible") == true);
		}
	}

	void TestHiddenPublicPlayersFilterPerRecipient()
	{
		ProtocolHost host{ "Map Presence Hidden" };
		const auto roster = BuildRoster(host);
		host.SetMapPublicPlayers(false);
		assert(!host.MapPublicPlayers());
		const auto frames = host.PollPresence(0.0, 110.0);
		assert(VisibleIds(frames, 30) == std::vector<std::uint32_t>{ roster.carl });
		assert(VisibleIds(frames, 20) == std::vector<std::uint32_t>{ roster.bob });
		const auto alpha = Sorted({ roster.alice, roster.ann });
		assert(VisibleIds(frames, 10) == alpha);
		assert(VisibleIds(frames, 11) == alpha);
		for (const auto connectionId : { 10u, 11u, 20u, 30u }) {
			assert(RequirePacket(frames, connectionId, "mapPresence").at("publicPlayersVisible") == false);
		}

		auto step = Transform(1010.0);
		step["movementType"] = "normal";
		Send(host, 10, step, 3.0, 111.0);
		Send(host, 10, Json{ { "type", "playerState" }, { "vitals", Json{ { "healthPercent", 0 }, { "isDowned", true }, { "isDead", false } } } }, 3.1, 111.1);
		const auto next = host.PollPresence(1.1, 112.0);
		const auto moved = EntryFor(RequirePacket(next, 11, "mapPresence").at("players"), roster.alice);
		assert(moved.at("x") == 1010.0 && moved.at("downed") == true);
		assert(VisibleIds(next, 30) == std::vector<std::uint32_t>{ roster.carl });
		assert(VisibleIds(next, 20) == std::vector<std::uint32_t>{ roster.bob });
	}

	void TestTogglingChangesTheNextPacket()
	{
		ProtocolHost host{ "Map Presence Toggle" };
		const auto roster = BuildRoster(host);
		assert(VisibleIds(host.PollPresence(0.0, 110.0), 30).size() == 4);
		host.SetMapPublicPlayers(false);
		assert(host.PollPresence(0.5, 110.5).empty());
		assert(VisibleIds(host.PollPresence(1.1, 111.1), 30) == std::vector<std::uint32_t>{ roster.carl });
		host.SetMapPublicPlayers(true);
		const auto restored = host.PollPresence(2.2, 112.2);
		assert(VisibleIds(restored, 30).size() == 4);
		assert(RequirePacket(restored, 30, "mapPresence").at("publicPlayersVisible") == true);
	}

	void TestSteamIdOnlyReachesSelf()
	{
		ProtocolHost host{ "Map Presence SteamId" };
		const auto roster = BuildRoster(host);
		host.BindSessionSteamId(10, "76561198000000001");
		host.BindSessionSteamId(20, "76561198000000002");
		const auto players = RequirePacket(host.PollPresence(0.0, 110.0), 10, "mapPresence").at("players");
		assert(EntryFor(players, roster.alice).at("steamId") == "76561198000000001");
		assert(EntryFor(players, roster.bob).at("steamId") == "");
	}

	void MakeFriends(ProtocolHost& a_host, std::uint32_t a_requesterConnectionId, std::uint32_t a_accepterConnectionId, std::uint32_t a_accepterPlayerId, double a_monotonicSeconds)
	{
		const auto sent = Send(a_host, a_requesterConnectionId, Json{ { "type", "friendRequest" }, { "targetPlayerId", a_accepterPlayerId } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
		assert(RequirePacket(sent, a_requesterConnectionId, "friendResult").at("ok") == true);
		const auto requestId = RequirePacket(sent, a_accepterConnectionId, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = Send(
			a_host,
			a_accepterConnectionId,
			Json{ { "type", "friendRequestResponse" }, { "requestId", requestId }, { "accept", true } },
			a_monotonicSeconds + 0.1,
			a_monotonicSeconds + 99.1);
		assert(RequirePacket(accepted, a_requesterConnectionId, "friendState").at("friends").size() >= 1);
	}

	Joined JoinFriends(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = Send(a_host, a_connectionId, Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1", "friends-v1" }) } }, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	void TestFriendFlagIsPerRecipient()
	{
		ProtocolHost host{ "Map Presence Friend" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "p10", 1.0, 100.0).playerId;
		const auto ann = Join(host, 11, "p11", 1.1, 100.1).playerId;
		const auto bob = JoinFriends(host, 20, "p20", 1.2, 100.2).playerId;
		const auto carl = Join(host, 30, "p30", 1.3, 100.3).playerId;
		for (const auto connectionId : { 10u, 11u, 20u, 30u }) Send(host, connectionId, Transform(100.0 * connectionId), 2.0, 101.0);
		MakeFriends(host, 10, 20, bob, 3.0);
		const auto frames = host.PollPresence(0.0, 110.0);
		const auto fromAlice = RequirePacket(frames, 10, "mapPresence").at("players");
		assert(EntryFor(fromAlice, bob).at("friend") == true);
		assert(EntryFor(fromAlice, ann).at("friend") == false);
		const auto fromBob = RequirePacket(frames, 20, "mapPresence").at("players");
		assert(EntryFor(fromBob, alice).at("friend") == true);
		assert(EntryFor(fromBob, carl).at("friend") == false);
	}

	void TestHiddenPublicPlayersKeepTheCap()
	{
		ProtocolHost host{ "Map Presence Hidden Cap" };
		for (std::uint32_t number = 1; number <= 101; ++number) {
			const auto digits = std::to_string(1000 + number).substr(1);
			const auto connectionId = 100 + number;
			Join(host, connectionId, "m" + digits, 1.0 + number * 0.01, 100.0 + number * 0.01);
			Send(host, connectionId, Transform(100.0 + number), 2.0 + number * 0.01, 101.0 + number * 0.01);
		}
		host.SetMapPublicPlayers(false);
		const auto hidden = host.PollPresence(0.0, 200.0);
		for (std::uint32_t number = 1; number <= 101; ++number) {
			assert(RequirePacket(hidden, 100 + number, "mapPresence").at("players").size() <= 1);
		}
		host.SetMapPublicPlayers(true);
		assert(RequirePacket(host.PollPresence(1.1, 201.1), 101, "mapPresence").at("players").size() == 100);
	}
}

int main()
{
	TestCapabilityIsAdvertisedInWelcome();
	TestNoSessionsProducesNoPresence();
	TestPresenceBroadcastsMovedPlayersOncePerSecond();
	TestPresenceIsCappedAtSessionLimit();
	TestPublicPlayersVisibleByDefaultSendsEveryone();
	TestHiddenPublicPlayersFilterPerRecipient();
	TestTogglingChangesTheNextPacket();
	TestSteamIdOnlyReachesSelf();
	TestFriendFlagIsPerRecipient();
	TestHiddenPublicPlayersKeepTheCap();
	return 0;
}
