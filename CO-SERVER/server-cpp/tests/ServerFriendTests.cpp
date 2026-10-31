#include "ServerFactionTestSupport.h"
#include "ServerSocialGraph.h"

#include <string>

using namespace FactionTestSupport;

namespace
{
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::SocialGraph;

	std::vector<OutboundFrame> SendFriendRequest(ProtocolHost& a_host, std::uint32_t a_connectionId, std::uint32_t a_targetPlayerId, double a_monotonicSeconds)
	{
		return Send(a_host, a_connectionId, Json{ { "type", "friendRequest" }, { "targetPlayerId", a_targetPlayerId } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	std::vector<OutboundFrame> SendFriendResponse(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_requestId, bool a_accept, double a_monotonicSeconds)
	{
		return Send(a_host, a_connectionId, Json{ { "type", "friendRequestResponse" }, { "requestId", a_requestId }, { "accept", a_accept } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	Json FriendState(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		return RequirePacket(Send(a_host, a_connectionId, Json{ { "type", "friendListRequest" } }, a_monotonicSeconds, a_monotonicSeconds + 99.0), a_connectionId, "friendState");
	}

	Joined JoinFriends(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = a_host.HandleMessage(
			a_connectionId,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1", "friends-v1" }) } }),
			a_monotonicSeconds,
			a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	void WithName(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_name, double a_monotonicSeconds)
	{
		(void)Send(a_host, a_connectionId, Json{ { "type", "playerState" }, { "characterName", a_name } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	void TestRequestHidesName()
	{
		ProtocolHost host{ "Friends Stranger" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto frames = SendFriendRequest(host, 10, bob.playerId, 2.0);
		const auto result = RequirePacket(frames, 10, "friendResult");
		assert(result.at("op") == "friendRequest" && result.at("ok") == true);
		const auto received = RequirePacket(frames, 20, "friendRequestReceived");
		assert(received.at("displayName") == "Stranger");
		assert(!received.contains("fromName"));
		assert(received.at("fromPlayerId") == alice.playerId);
		assert(received.at("expiresInSeconds") == 60);
	}

	void TestAcceptRevealsBothWays()
	{
		ProtocolHost host{ "Friends Accept" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto request = SendFriendRequest(host, 10, bob.playerId, 2.0);
		const auto requestId = RequirePacket(request, 20, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = SendFriendResponse(host, 20, requestId, true, 2.1);
		assert(RequirePacket(accepted, 20, "friendResult").at("ok") == true);
		assert(RequirePacket(accepted, 10, "friendResult").at("ok") == true);
		assert(RequirePacket(accepted, 10, "friendState").at("friends").size() == 1);
		assert(RequirePacket(accepted, 20, "friendState").at("friends").size() == 1);
		assert(RequirePacket(accepted, 10, "friendState").at("friends").at(0).at("name") == "Bob");
		assert(RequirePacket(accepted, 20, "friendState").at("friends").at(0).at("name") == "Alice");
		const auto aliceSeesBob = RequirePacket(accepted, 10, "playerState");
		assert(aliceSeesBob.at("displayName") == "Bob" && aliceSeesBob.at("characterName") == "Bob");
		const auto bobSeesAlice = RequirePacket(accepted, 20, "playerState");
		assert(bobSeesAlice.at("displayName") == "Alice" && bobSeesAlice.at("characterName") == "Alice");
		assert(!RequirePacket(accepted, 10, "friendState").at("friends").at(0).contains("profileId"));
		assert(alice.playerId != bob.playerId);
	}

	void TestDeclineRevealsNothing()
	{
		ProtocolHost host{ "Friends Decline" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto request = SendFriendRequest(host, 10, bob.playerId, 2.0);
		const auto requestId = RequirePacket(request, 20, "friendRequestReceived").at("requestId").get<std::string>();
		const auto declined = SendFriendResponse(host, 20, requestId, false, 2.1);
		const auto aliceResult = RequirePacket(declined, 10, "friendResult");
		assert(aliceResult.at("ok") == false && aliceResult.at("error") == "declined");
		assert(!aliceResult.contains("displayName") && !aliceResult.contains("name"));
		assert(RequirePacket(declined, 10, "friendState").at("friends").empty());
		assert(RequirePacket(declined, 10, "friendState").at("pendingOutgoingPlayerIds").empty());
		assert(FriendState(host, 10, 2.2).at("friends").empty());
		assert(FriendState(host, 20, 2.3).at("friends").empty());
		// Constraint: a stranger must stay "Stranger" after a declined request.
		const auto relay = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Alice" } }, 2.4, 102.4);
		assert(RequirePacket(relay, 20, "playerState").at("displayName") == "Stranger");
	}

	void TestExpiryRevealsNothing()
	{
		ProtocolHost host{ "Friends Expiry" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto request = SendFriendRequest(host, 10, bob.playerId, 2.0);
		const auto requestId = RequirePacket(request, 20, "friendRequestReceived").at("requestId").get<std::string>();
		const auto expired = SendFriendResponse(host, 20, requestId, true, 70.0);
		assert(RequirePacket(expired, 20, "friendResult").at("error") == "requestNotFound");
		const auto aliceExpiry = RequirePacket(expired, 10, "friendResult");
		assert(aliceExpiry.at("ok") == false && aliceExpiry.at("error") == "requestExpired");
		assert(RequirePacket(expired, 10, "friendState").at("friends").empty());
		assert(RequirePacket(expired, 20, "friendState").at("pendingIncoming").empty());
		const auto relay = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Alice" } }, 70.1, 169.1);
		assert(RequirePacket(relay, 20, "playerState").at("displayName") == "Stranger");
	}

	void TestMutualRequestsAccept()
	{
		ProtocolHost host{ "Friends Mutual" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		SendFriendRequest(host, 10, bob.playerId, 2.0);
		const auto mutual = SendFriendRequest(host, 20, alice.playerId, 2.1);
		assert(RequirePacket(mutual, 20, "friendResult").at("ok") == true);
		assert(RequirePacket(mutual, 20, "friendState").at("friends").size() == 1);
		assert(RequirePacket(mutual, 10, "friendState").at("friends").size() == 1);
		assert(RequirePacket(mutual, 10, "playerState").at("characterName") == "Bob");
		assert(RequirePacket(mutual, 20, "playerState").at("characterName") == "Alice");
	}

	void TestErrors()
	{
		ProtocolHost host{ "Friends Errors" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		assert(RequirePacket(SendFriendRequest(host, 10, alice.playerId, 2.0), 10, "friendResult").at("error") == "invalidTarget");
		assert(RequirePacket(SendFriendRequest(host, 10, 0, 2.05), 10, "friendResult").at("error") == "invalidTarget");
		assert(RequirePacket(SendFriendRequest(host, 10, 9999, 2.1), 10, "friendResult").at("error") == "targetUnavailable");
		const auto legacy = Join(host, 30, "pc", 1.2, 100.2);
		assert(RequirePacket(SendFriendRequest(host, 10, legacy.playerId, 2.2), 10, "friendResult").at("error") == "targetUnsupported");
		const auto request = SendFriendRequest(host, 10, bob.playerId, 2.3);
		const auto requestId = RequirePacket(request, 20, "friendRequestReceived").at("requestId").get<std::string>();
		assert(RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.4), 10, "friendResult").at("error") == "requestExists");
		assert(RequirePacket(SendFriendResponse(host, 10, requestId, true, 2.5), 10, "friendResult").at("error") == "requestNotFound");
		assert(RequirePacket(SendFriendResponse(host, 20, "fr-bogus", true, 2.6), 20, "friendResult").at("error") == "requestNotFound");
		assert(RequirePacket(Send(host, 10, Json{ { "type", "friendRemove" }, { "friendId", "f404" } }, 2.7, 102.7), 10, "friendResult").at("error") == "notFriends");
		SendFriendResponse(host, 20, requestId, true, 2.8);
		assert(RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.9), 10, "friendResult").at("error") == "alreadyFriends");
	}

	void TestPendingPerTarget()
	{
		ProtocolHost host{ "Friends Pending" };
		assert(host.EnableSocial(nullptr));
		const auto target = JoinFriends(host, 20, "ptarget", 1.0, 100.0);
		for (std::uint32_t index = 0; index < 6; ++index) {
			JoinFriends(host, 30 + index, "psender" + std::to_string(index), 1.1, 100.1);
		}
		for (std::uint32_t index = 0; index < 5; ++index) {
			const auto result = RequirePacket(SendFriendRequest(host, 30 + index, target.playerId, 2.0 + index * 0.01), 30 + index, "friendResult");
			assert(result.at("ok") == true);
		}
		assert(RequirePacket(SendFriendRequest(host, 35, target.playerId, 2.1), 35, "friendResult").at("error") == "requestCapacity");
	}

	void TestRequestRate()
	{
		ProtocolHost host{ "Friends Rate" };
		assert(host.EnableSocial(nullptr));
		JoinFriends(host, 10, "pa", 1.0, 100.0);
		std::vector<std::uint32_t> targets;
		for (std::uint32_t index = 0; index < 12; ++index) {
			targets.push_back(JoinFriends(host, 20 + index, "pt" + std::to_string(index), 1.1, 100.1).playerId);
		}
		for (std::uint32_t index = 0; index < 10; ++index) {
			assert(RequirePacket(SendFriendRequest(host, 10, targets[index], 2.0 + index * 0.01), 10, "friendResult").at("ok") == true);
		}
		assert(RequirePacket(SendFriendRequest(host, 10, targets[10], 2.5), 10, "friendResult").at("error") == "rateLimited");
		assert(RequirePacket(SendFriendRequest(host, 10, targets[11], 2.6), 10, "friendResult").at("error") == "rateLimited");
	}

	void TestRemoveKeepsNames()
	{
		ProtocolHost host{ "Friends Remove" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto requestId = RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.0), 20, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = SendFriendResponse(host, 20, requestId, true, 2.1);
		const auto friendId = RequirePacket(accepted, 10, "friendState").at("friends").at(0).at("friendId").get<std::string>();
		const auto removed = Send(host, 10, Json{ { "type", "friendRemove" }, { "friendId", friendId } }, 2.2, 102.2);
		assert(RequirePacket(removed, 10, "friendResult").at("ok") == true);
		assert(RequirePacket(removed, 10, "friendState").at("friends").empty());
		assert(RequirePacket(removed, 20, "friendState").at("friends").empty());
		const auto relay = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Alice" } }, 2.3, 102.3);
		assert(RequirePacket(relay, 20, "playerState").at("displayName") == "Alice");
		const auto chat = Send(host, 20, Json{ { "type", "playerState" }, { "characterName", "Bob" } }, 2.4, 102.4);
		assert(RequirePacket(chat, 10, "playerState").at("displayName") == "Bob");
		assert(alice.playerId != bob.playerId);
	}

	void TestPersistenceRoundTrip()
	{
		const auto directory = TempDirectory("friends-persist");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Friends Persist" };
			assert(host.EnableSocial(&gate));
			const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
			JoinFriends(host, 10, "pa", 1.0, 100.0);
			WithName(host, 10, "Alice", 1.2);
			WithName(host, 20, "Bob", 1.3);
			const auto requestId = RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.0), 20, "friendRequestReceived").at("requestId").get<std::string>();
			SendFriendResponse(host, 20, requestId, true, 2.1);
			store.WaitForCommits();
			const auto stored = store.GetKnownPeople();
			assert(stored.at("friends").at("pa").at(0).at("profileId") == "pb");
			assert(stored.at("friends").at("pa").at(0).at("name") == "Bob");
			assert(stored.at("friends").at("pb").at(0).at("name") == "Alice");
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Friends Reload" };
			assert(host.EnableSocial(&gate));
			JoinFriends(host, 10, "pa", 1.0, 200.0);
			const auto state = FriendState(host, 10, 2.0);
			assert(state.at("friends").size() == 1);
			assert(state.at("friends").at(0).at("name") == "Bob");
			assert(state.at("friends").at(0).at("online") == false);
			assert(!state.at("friends").at(0).contains("playerId"));
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveKnownPeople(Json{ { "knownPeople", Json{ { "pa", Json::array({ "pc" }) } } } });
			store.WaitForCommits();
			PersistenceGate gate(store);
			ProtocolHost host{ "Friends Old Store" };
			assert(host.EnableSocial(&gate));
			JoinFriends(host, 10, "pa", 1.0, 300.0);
			assert(FriendState(host, 10, 2.0).at("friends").empty());
		}
		std::filesystem::remove_all(directory);
	}

	void TestFriendCapFromStoredList()
	{
		const auto directory = TempDirectory("friends-cap");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		Json list = Json::array();
		Json stored = Json::object();
		for (int index = 0; index < 200; ++index) {
			const auto other = "friend-" + std::to_string(index);
			list.push_back(Json{ { "profileId", other }, { "name", "Friend" } });
			stored[other] = Json::array({ Json{ { "profileId", "pa" }, { "name", "Alice" } } });
		}
		stored["pa"] = list;
		store.SaveKnownPeople(Json{ { "knownPeople", Json::object() }, { "friends", std::move(stored) } });
		store.WaitForCommits();
		PersistenceGate gate(store);
		ProtocolHost host{ "Friends Cap" };
		assert(host.EnableSocial(&gate));
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		JoinFriends(host, 10, "pa", 1.0, 100.0);
		assert(RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.0), 10, "friendResult").at("error") == "friendListFull");
		std::filesystem::remove_all(directory);
	}

	void TestSaveFailureRollsBack()
	{
		const auto directory = TempDirectory("friends-save-fail");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Friends Save Failure" };
		assert(host.EnableSocial(&gate));
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		JoinFriends(host, 10, "pa", 1.0, 100.0);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		const auto requestId = RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.0), 20, "friendRequestReceived").at("requestId").get<std::string>();
		std::filesystem::remove_all(directory);
		const auto accepted = SendFriendResponse(host, 20, requestId, true, 2.1);
		assert(RequirePacket(accepted, 20, "friendResult").at("error") == "persistenceUnavailable");
		assert(FriendState(host, 10, 2.2).at("friends").empty());
		// Constraint: a failed save must leave both players masked again, not permanently disclosed.
		const auto relay = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Alice" } }, 2.3, 102.3);
		const auto seen = RequirePacket(relay, 20, "playerState");
		assert(seen.at("displayName") == "Stranger");
		assert(!seen.contains("characterName"));
	}

	void TestSlotDeletionCleansFriends()
	{
		const auto directory = TempDirectory("friends-slot");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		store.SaveKnownPeople(Json{
			{ "knownPeople", Json{ { "acct-aaaa", Json::array({ "pb" }) } } },
			{ "friends", Json{
				{ "acct-aaaa", Json::array({ Json{ { "profileId", "pb" }, { "name", "Bob" } } }) },
				{ "pb", Json::array({ Json{ { "profileId", "acct-aaaa" }, { "name", "Alice" } } }) } } }
		});
		store.WaitForCommits();
		assert(store.EraseProfileByKey("acct-aaaa#0"));
		const auto stored = store.GetKnownPeople();
		assert(!stored.at("friends").contains("acct-aaaa"));
		assert(!stored.at("friends").contains("pb"));
		std::filesystem::remove_all(directory);
	}

	void TestGraphKeepsFriendsFromEviction()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		assert(graph.AddFriend("pa", "Alice", "pb", "Bob"));
		assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
		for (std::size_t index = 0; index < 300; ++index) graph.NoteKnown("pa", "other-" + std::to_string(index));
		assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
		assert(graph.AreFriends("pa", "pb") && graph.AreFriends("pb", "pa"));
		assert(graph.FriendCount("pa") == 1);
		assert(!graph.AddFriend("pa", "Alice", "pa", "Alice"));
		assert(graph.RemoveFriend("pa", "pb"));
		assert(!graph.AreFriends("pa", "pb") && !graph.AreFriends("pb", "pa"));
		assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
	}

	void TestGraphFriendsLoadWithoutKeyAndOneSidedDropped()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		const auto directory = TempDirectory("friends-graph");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveKnownPeople(Json{
				{ "knownPeople", Json{ { "pa", Json::array({ "pb" }) } } },
				{ "friends", Json{
					{ "pa", Json::array({ Json{ { "profileId", "pb" }, { "name", "Bob" } }, Json{ { "profileId", "pc" }, { "name", "Carol" } }, Json{ { "profileId", "" }, { "name", "Bad" } } }) },
					{ "pb", Json::array({ Json{ { "profileId", "pa" }, { "name", "Alice" } } }) } } }
			});
			store.WaitForCommits();
			PersistenceGate gate(store);
			assert(graph.Enable(&gate));
			assert(graph.AreFriends("pa", "pb"));
			assert(!graph.AreFriends("pa", "pc"));
			assert(!graph.AreFriends("pa", ""));
			assert(graph.Knows("pa", "pb"));
			store.SaveKnownPeople(Json{ { "knownPeople", Json{ { "pa", Json::array({ "pb" }) } } } });
			store.WaitForCommits();
			ProtocolStats reloadStats{};
			SocialGraph reloaded{ reloadStats };
			PersistenceGate reloadedGate(store);
			assert(reloaded.Enable(&reloadedGate));
			assert(reloaded.Knows("pa", "pb"));
			assert(reloaded.FriendCount("pa") == 0);
		}
		std::filesystem::remove_all(directory);
	}

	void TestExpiryPollWithoutPacket()
	{
		ProtocolHost host{ "Friends Expiry Poll" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinFriends(host, 10, "pa", 1.0, 100.0);
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		WithName(host, 10, "Alice", 1.2);
		WithName(host, 20, "Bob", 1.3);
		assert(RequirePacket(SendFriendRequest(host, 10, bob.playerId, 2.0), 10, "friendState").at("pendingOutgoingPlayerIds").size() == 1);
		const auto frames = host.PollFriendRequests(70.0, 168.0);
		const auto expired = RequirePacket(frames, 10, "friendResult");
		assert(expired.at("ok") == false && expired.at("error") == "requestExpired");
		assert(RequirePacket(frames, 10, "friendState").at("pendingOutgoingPlayerIds").empty());
		assert(RequirePacket(frames, 20, "friendState").at("pendingIncoming").empty());
		assert(RequirePacket(SendFriendRequest(host, 10, bob.playerId, 71.0), 10, "friendResult").at("ok") == true);
		assert(alice.playerId != bob.playerId);
	}

	void TestLegacySenderRefused()
	{
		ProtocolHost host{ "Friends Legacy Sender" };
		assert(host.EnableSocial(nullptr));
		const auto bob = JoinFriends(host, 20, "pb", 1.1, 100.1);
		const auto legacy = Join(host, 10, "pa", 1.0, 100.0);
		const auto refused = SendFriendRequest(host, 10, bob.playerId, 2.0);
		assert(RequirePacket(refused, 10, "friendResult").at("error") == "unsupported");
		assert(!HasPacket(refused, 20, "friendState"));
		assert(legacy.playerId != bob.playerId);
	}

	void TestLongMultibyteNameSaves()
	{
		const auto directory = TempDirectory("friends-utf8");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			std::string longName(127, 'a');
			longName += "\xC3\xA9";
			assert(graph.AddFriend("pa", "Alice", "pb", longName));
			const auto* list = graph.FriendsOf("pa");
			assert(list != nullptr && !list->empty());
			const auto trimmed = list->front().name;
			assert(trimmed.size() <= SocialGraph::kMaximumFriendNameBytes);
			assert(trimmed.back() != '\xC3');
			assert(graph.SaveNow());
			std::string exact(126, 'a');
			exact += "\xC3\xA9";
			assert(graph.AddFriend("pc", "Carol", "pd", exact));
			assert(graph.FriendsOf("pc")->front().name.size() == SocialGraph::kMaximumFriendNameBytes);
			assert(graph.SaveNow());
			store.WaitForCommits();
			const auto stored = store.GetKnownPeople();
			assert(stored.at("friends").at("pa").at(0).at("name") == trimmed);
		}
		std::filesystem::remove_all(directory);
	}

	void TestForgetProfileInMemory()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		assert(graph.AddFriend("pa", "Alice", "pb", "Bob"));
		graph.NoteKnown("pb", "pa");
		graph.ForgetProfile("pa");
		assert(!graph.AreFriends("pa", "pb") && !graph.AreFriends("pb", "pa"));
		assert(!graph.Knows("pb", "pa") && !graph.Knows("pa", "pb"));
		assert(graph.FriendCount("pa") == 0 && graph.FriendCount("pb") == 0);
		assert(graph.KnownListCopy("pb").empty());
	}
}

int main()
{
	TestRequestHidesName();
	TestAcceptRevealsBothWays();
	TestDeclineRevealsNothing();
	TestExpiryRevealsNothing();
	TestMutualRequestsAccept();
	TestErrors();
	TestPendingPerTarget();
	TestRequestRate();
	TestRemoveKeepsNames();
	TestPersistenceRoundTrip();
	TestFriendCapFromStoredList();
	TestSaveFailureRollsBack();
	TestSlotDeletionCleansFriends();
	TestGraphKeepsFriendsFromEviction();
	TestGraphFriendsLoadWithoutKeyAndOneSidedDropped();
	TestExpiryPollWithoutPacket();
	TestLegacySenderRefused();
	TestLongMultibyteNameSaves();
	TestForgetProfileInMemory();
	return 0;
}
