#include "ServerFactionTestSupport.h"

#include "DurableStateFileStore.h"
#include "PersistenceGate.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace FactionTestSupport;

namespace
{

	std::filesystem::path TempDirectory(std::string_view a_suffix)
	{
		auto directory = std::filesystem::temp_directory_path() /
			("co-server-slot-social-" + std::string{ a_suffix } + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}

	Json SlotHello()
	{
		return Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 }, { "capabilities", Json::array({ "social-names-v1", "friends-v1" }) } };
	}

	Json FriendHello()
	{
		return Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1", "friends-v1" }) } };
	}

	Json PlayerName(const std::string& a_name)
	{
		return Json{ { "type", "playerState" }, { "characterName", a_name } };
	}

	std::vector<OutboundFrame> ConnectSlot(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_account, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_account);
		auto hello = Send(a_host, a_connectionId, SlotHello(), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(hello, a_connectionId, "sessionReady"));
		return hello;
	}

	Joined JoinFriend(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_account, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_account);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = Send(a_host, a_connectionId, FriendHello(), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	void SetName(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_name, double a_monotonicSeconds)
	{
		(void)Send(a_host, a_connectionId, PlayerName(a_name), a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	Json FriendState(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		return RequirePacket(Send(a_host, a_connectionId, Json{ { "type", "friendListRequest" } }, a_monotonicSeconds, a_monotonicSeconds + 99.0), a_connectionId, "friendState");
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

	void CreateSlotsAndSelect(ProtocolHost& a_host, const std::string& a_account, std::uint32_t a_selectSlot)
	{
		(void)ConnectSlot(a_host, 90, a_account, 1.0, 100.0);
		const auto first = Send(a_host, 90, Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "First" } }, 1.1, 100.2);
		assert(RequirePacket(first, 90, "characterResult").at("ok") == true);
		const auto second = Send(a_host, 90, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Second" } }, 1.2, 100.3);
		assert(RequirePacket(second, 90, "characterResult").at("ok") == true);
		const auto selected = Send(a_host, 90, Json{ { "type", "characterSelect" }, { "slot", a_selectSlot } }, 1.3, 100.4);
		assert(RequirePacket(selected, 90, "characterResult").at("ok") == true);
		a_host.HandleDisconnected(90, 100.5);
	}

	void TestSlotsHaveIndependentFriendsAndNames()
	{
		const auto directory = TempDirectory("independent");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Independent" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-acct", 0);

		const auto hello10 = ConnectSlot(host, 10, "slot-acct", 2.0, 110.0);
		assert(RequirePacket(hello10, 10, "sessionReady").at("persistenceProfile") == "slot-acct#0");
		const auto bobId = JoinFriend(host, 20, "slot-bobby", 2.1, 110.1).playerId;
		SetName(host, 10, "First", 2.2);
		SetName(host, 20, "Bob", 2.3);
		MakeFriends(host, 10, 20, bobId, 3.0);
		assert(RequirePacket(Send(host, 20, PlayerName("Bobby"), 3.4, 113.4), 10, "playerState").at("displayName") == "Bobby");

		const auto switched = Send(host, 10, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 3.6, 113.6);
		assert(RequirePacket(switched, 10, "characterResult").at("ok") == true);
		assert(FriendState(host, 10, 3.7).at("friends").size() == 1);
		host.HandleDisconnected(10, 114.0);

		const auto hello11 = ConnectSlot(host, 11, "slot-acct", 4.0, 114.1);
		assert(RequirePacket(hello11, 11, "sessionReady").at("persistenceProfile") == "slot-acct#1");
		assert(FriendState(host, 11, 4.2).at("friends").empty());
		assert(FriendState(host, 20, 4.3).at("friends").size() == 1);

		SetName(host, 11, "Second", 4.4);
		const auto nameToSlot1 = Send(host, 20, PlayerName("Bob"), 4.5, 114.5);
		assert(RequirePacket(nameToSlot1, 11, "playerState").at("displayName") == "Stranger");
		assert(!RequirePacket(nameToSlot1, 11, "playerState").contains("characterName"));

		const auto request = Send(host, 11, Json{ { "type", "friendRequest" }, { "targetPlayerId", bobId } }, 4.6, 114.6);
		assert(RequirePacket(request, 11, "friendResult").at("ok") == true);
		assert(RequirePacket(request, 20, "friendRequestReceived").at("displayName") == "Stranger");

		host.HandleDisconnected(11, 115.0);
		std::filesystem::remove_all(directory);
	}

	void TestDeletingSlotKeepsSiblingFriendsAndNames()
	{
		const auto directory = TempDirectory("delete");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Delete" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-acct", 0);

		(void)ConnectSlot(host, 10, "slot-acct", 2.0, 110.0);
		const auto bobId = JoinFriend(host, 20, "slot-bobby", 2.1, 110.1).playerId;
		SetName(host, 10, "First", 2.2);
		SetName(host, 20, "Bob", 2.3);
		MakeFriends(host, 10, 20, bobId, 3.0);
		const auto selected = Send(host, 10, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 3.5, 113.5);
		assert(RequirePacket(selected, 10, "characterResult").at("ok") == true);
		host.HandleDisconnected(10, 114.0);

		(void)ConnectSlot(host, 11, "slot-acct", 4.0, 114.1);
		SetName(host, 11, "Second", 4.2);
		MakeFriends(host, 11, 20, bobId, 4.5);
		assert(FriendState(host, 20, 4.8).at("friends").size() == 2);

		const auto deleted = Send(host, 11, Json{ { "type", "characterDelete" }, { "slot", 0 } }, 5.0, 115.0);
		const auto challenge = RequirePacket(deleted, 11, "characterDeleteChallenge");
		const auto confirmed = Send(
			host,
			11,
			Json{ { "type", "characterDelete" }, { "slot", 0 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } },
			5.1,
			115.0);
		assert(RequirePacket(confirmed, 11, "characterResult").at("ok") == true);
		const auto bobPurged = RequirePacket(confirmed, 20, "friendState").at("friends");
		assert(bobPurged.size() == 1);
		assert(bobPurged.at(0).at("name") == "Second");
		host.HandleDisconnected(11, 116.0);
		store.WaitForCommits();

		assert(!store.GetPlayerState("slot-acct#0").has_value());
		assert(store.GetPlayerState("slot-acct#1").has_value());
		const auto known = store.GetKnownPeople().at("knownPeople");
		assert(!known.contains("slot-acct#0"));
		assert(known.contains("slot-acct#1") && !known.at("slot-acct#1").empty());
		for (const auto& [owner, list] : known.items()) {
			(void)owner;
			for (const auto& entry : list) assert(entry.get<std::string>() != "slot-acct#0");
		}

		const auto bobFriends = FriendState(host, 20, 6.0).at("friends");
		assert(bobFriends.size() == 1);
		assert(bobFriends.at(0).at("name") == "Second");

		(void)ConnectSlot(host, 12, "slot-acct", 6.1, 116.0);
		const auto rebound = Send(host, 12, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 6.2, 116.1);
		assert(RequirePacket(rebound, 12, "characterResult").at("ok") == true);
		host.HandleDisconnected(12, 116.5);
		const auto hello13 = ConnectSlot(host, 13, "slot-acct", 6.3, 116.6);
		assert(RequirePacket(hello13, 13, "sessionReady").at("persistenceProfile") == "slot-acct#1");
		const auto nameAfterDelete = Send(host, 20, PlayerName("Bobby"), 6.4, 116.7);
		assert(RequirePacket(nameAfterDelete, 13, "playerState").at("displayName") == "Bobby");
		host.HandleDisconnected(13, 117.0);
		host.HandleDisconnected(20, 117.1);
		std::filesystem::remove_all(directory);
	}

	void TestLegacySessionsKeyByAccount()
	{
		const auto directory = TempDirectory("legacy");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);

		{
			ProtocolHost host{ "Legacy Keys" };
			assert(host.EnableSocial(&gate));
			const auto alice = JoinFriend(host, 10, "legacy-a", 1.0, 100.0);
			assert(!alice.hello.empty() && !RequirePacket(alice.hello, 10, "sessionReady").contains("persistenceProfile"));
			const auto bob = JoinFriend(host, 20, "legacy-b", 1.1, 100.1);
			MakeFriends(host, 10, 20, bob.playerId, 2.0);
			store.WaitForCommits();
			const auto known = store.GetKnownPeople().at("knownPeople");
			assert(known.contains("legacy-a") && known.contains("legacy-b"));
			assert(!known.contains("legacy-a#0") && !known.contains("legacy-b#0"));
		}

		{
			ProtocolHost host{ "Unslotted Keys" };
			assert(host.EnableSocial(&gate));
			assert(host.EnableCharacterSlots(&gate, 3));
			(void)host.HandleConnected(30, 200.0);
			host.BindSessionProfile(30, "unslotted-a");
			const auto hello30 = Send(host, 30, SlotHello(), 1.0, 200.1);
			assert(!RequirePacket(hello30, 30, "sessionReady").contains("persistenceProfile"));
			const auto bob = JoinFriend(host, 40, "unslotted-b", 1.1, 200.2);
			const auto refused = Send(host, 30, Json{ { "type", "friendRequest" }, { "targetPlayerId", bob.playerId } }, 2.0, 102.0);
			assert(RequirePacket(refused, 30, "friendResult").at("ok") == false);
			// Constraint: a no-key friends-v1 session gets an empty friendState, never a list.
			const auto emptyState = RequirePacket(Send(host, 30, Json{ { "type", "friendListRequest" } }, 2.1, 102.1), 30, "friendState");
			assert(emptyState.at("friends").empty() && emptyState.at("pendingIncoming").empty() && emptyState.at("pendingOutgoingPlayerIds").empty());
			store.WaitForCommits();
			const auto known = store.GetKnownPeople().at("knownPeople");
			assert(!known.contains("unslotted-a") && !known.contains("unslotted-a#0"));
		}
		std::filesystem::remove_all(directory);
	}

	Json RosterRow(const Json& a_roster, std::uint32_t a_playerId)
	{
		for (const auto& row : a_roster.at("players")) {
			if (row.at("playerId").get<std::uint32_t>() == a_playerId) return row;
		}
		assert(false);
		return {};
	}

	void TestSlotAcceptRefreshesRosterAndFactionNames()
	{
		const auto directory = TempDirectory("slot-roster");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Roster" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-alice", 0);
		const auto alice = RequirePacket(ConnectSlot(host, 10, "slot-alice", 2.0, 110.0), 10, "sessionReady").at("playerId").get<std::uint32_t>();
		SetName(host, 10, "Alice", 2.1);
		CreateSlotsAndSelect(host, "slot-bob", 0);
		const auto bob = RequirePacket(ConnectSlot(host, 20, "slot-bob", 2.2, 110.2), 20, "sessionReady").at("playerId").get<std::uint32_t>();
		SetName(host, 20, "Bob", 2.3);
		AssertResult(Send(host, 20, Create("Vault"), 2.4, 110.4), 20, "create", true);
		CreateSlotsAndSelect(host, "slot-carol", 0);
		(void)ConnectSlot(host, 30, "slot-carol", 2.5, 110.5);
		SetName(host, 30, "Carol", 2.6);

		const auto requested = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", bob } }, 3.0, 113.0);
		const auto requestId = RequirePacket(requested, 20, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = Send(host, 20, Json{ { "type", "friendRequestResponse" }, { "requestId", requestId }, { "accept", true } }, 3.1, 113.1);
		// Constraint: both slot-keyed accepting sessions get the real names in their roster and faction list, not only the playerState cache.
		const auto aliceSeesBob = RosterRow(RequirePacket(accepted, 10, "playerRoster"), bob);
		assert(aliceSeesBob.at("displayName") == "Bob" && aliceSeesBob.at("name") == "Bob");
		const auto bobSeesAlice = RosterRow(RequirePacket(accepted, 20, "playerRoster"), alice);
		assert(bobSeesAlice.at("displayName") == "Alice" && bobSeesAlice.at("name") == "Alice");
		assert(RequirePacket(accepted, 10, "factionList").at("factions").at(0).at("leader") == "Bob");
		assert(RequirePacket(accepted, 20, "factionList").at("factions").at(0).at("leader") == "Bob");

		// Constraint: a slot-keyed non-friend still sees "Stranger" for both players and the faction leader.
		const auto carolRoster = Send(host, 30, Json{ { "type", "playerRosterRequest" } }, 3.2, 113.2);
		const auto carol = RequirePacket(carolRoster, 30, "playerRoster");
		assert(RosterRow(carol, alice).at("displayName") == "Stranger" && !RosterRow(carol, alice).contains("name"));
		assert(RosterRow(carol, bob).at("displayName") == "Stranger" && !RosterRow(carol, bob).contains("name"));
		const auto carolList = Send(host, 30, Json{ { "type", "factionList" } }, 3.3, 113.3);
		assert(RequirePacket(carolList, 30, "factionList").at("factions").at(0).at("leader") == "Stranger");
		host.HandleDisconnected(10, 114.0);
		host.HandleDisconnected(20, 114.1);
		host.HandleDisconnected(30, 114.2);
		std::filesystem::remove_all(directory);
	}

	void TestOfflineFactionMemberResolvesForSlotFriend()
	{
		const auto directory = TempDirectory("offline-faction");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Offline Faction" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-alice", 0);
		(void)ConnectSlot(host, 10, "slot-alice", 2.0, 110.0);
		SetName(host, 10, "Alice", 2.1);
		CreateSlotsAndSelect(host, "slot-bob", 0);
		const auto bob = RequirePacket(ConnectSlot(host, 20, "slot-bob", 2.2, 110.2), 20, "sessionReady").at("playerId").get<std::uint32_t>();
		SetName(host, 20, "Bob", 2.3);
		AssertResult(Send(host, 20, Create("Vault"), 2.4, 110.4), 20, "create", true);
		CreateSlotsAndSelect(host, "slot-carol", 0);
		(void)ConnectSlot(host, 30, "slot-carol", 2.5, 110.5);
		SetName(host, 30, "Carol", 2.6);
		MakeFriends(host, 10, 20, bob, 3.0);

		(void)host.HandleDisconnected(20, 113.5);
		const auto aliceList = Send(host, 10, Json{ { "type", "factionList" } }, 3.5, 113.6);
		assert(RequirePacket(aliceList, 10, "factionList").at("factions").at(0).at("leader") == "Bob");
		const auto carolList = Send(host, 30, Json{ { "type", "factionList" } }, 3.6, 113.7);
		assert(RequirePacket(carolList, 30, "factionList").at("factions").at(0).at("leader") == "Stranger");
		host.HandleDisconnected(10, 114.0);
		host.HandleDisconnected(30, 114.1);
		std::filesystem::remove_all(directory);
	}

	void TestUnboundReconnectDropsStoredSocialKey()
	{
		const auto directory = TempDirectory("stale-key");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Stale Social Key" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-alice", 0);
		(void)ConnectSlot(host, 10, "slot-alice", 2.0, 110.0);
		SetName(host, 10, "Alice", 2.1);
		CreateSlotsAndSelect(host, "slot-bob", 0);
		const auto bob = RequirePacket(ConnectSlot(host, 20, "slot-bob", 2.2, 110.2), 20, "sessionReady").at("playerId").get<std::uint32_t>();
		SetName(host, 20, "Bob", 2.3);
		AssertResult(Send(host, 20, Create("Vault"), 2.4, 110.4), 20, "create", true);
		MakeFriends(host, 10, 20, bob, 3.0);
		store.WaitForCommits();
		assert(store.GetFactions().at("factions").at("f-1").at("memberSocialKeys").at("slot-bob") == "slot-bob#0");

		host.HandleDisconnected(20, 113.0);
		const auto unbound = ConnectSlot(host, 21, "slot-bob", 4.0, 113.5);
		assert(!RequirePacket(unbound, 21, "sessionReady").contains("persistenceProfile"));
		assert(RequirePacket(unbound, 21, "factionState").at("faction").at("members").size() == 1);
		host.HandleDisconnected(21, 113.8);
		store.WaitForCommits();
		assert(!store.GetFactions().at("factions").at("f-1").at("memberSocialKeys").contains("slot-bob"));
		const auto aliceList = Send(host, 10, Json{ { "type", "factionList" } }, 4.5, 114.0);
		assert(RequirePacket(aliceList, 10, "factionList").at("factions").at(0).at("leader") == "Stranger");
		host.HandleDisconnected(10, 114.1);
		std::filesystem::remove_all(directory);
	}

	void TestNoFriendsCapabilityGetsNoFriendState()
	{
		const auto directory = TempDirectory("no-friends-cap");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot No Friends Capability" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		(void)host.HandleConnected(40, 200.0);
		host.BindSessionProfile(40, "nocap-a");
		const Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 }, { "capabilities", Json::array({ "social-names-v1" }) } };
		assert(RequirePacket(Send(host, 40, hello, 1.0, 200.1), 40, "sessionReady").at("playerId").is_number());
		// Constraint: a client without friends-v1 gets no friendState at all, even with no selected slot.
		assert(!HasPacket(Send(host, 40, Json{ { "type", "friendListRequest" } }, 1.1, 200.2), 40, "friendState"));
		host.HandleDisconnected(40, 201.0);
		std::filesystem::remove_all(directory);
	}

	void TestSlotAdminSeesRealNames()
	{
		const auto directory = TempDirectory("admin");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Admin", {}, { "slot-admin" } };
		assert(host.EnableSocial(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-admin", 0);
		const auto hello10 = ConnectSlot(host, 10, "slot-admin", 2.0, 110.0);
		host.SetSessionAdminVerified(10, true);
		assert(RequirePacket(hello10, 10, "sessionReady").at("persistenceProfile") == "slot-admin#0");
		(void)JoinFriend(host, 20, "slot-subject", 2.1, 110.1);
		const auto relayed = Send(host, 20, PlayerName("Secret"), 2.3, 112.3);
		assert(RequirePacket(relayed, 10, "playerState").at("displayName") == "Secret");
		host.HandleDisconnected(10, 113.0);
		host.HandleDisconnected(20, 113.1);
		std::filesystem::remove_all(directory);
	}

	void TestSupersededSlotDropsFriendOnline()
	{
		const auto directory = TempDirectory("supersede");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Supersede" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		CreateSlotsAndSelect(host, "slot-acct", 0);
		(void)ConnectSlot(host, 10, "slot-acct", 2.0, 110.0);
		const auto bobId = JoinFriend(host, 20, "slot-bobby", 2.1, 110.1).playerId;
		SetName(host, 10, "First", 2.2);
		SetName(host, 20, "Bob", 2.3);
		MakeFriends(host, 10, 20, bobId, 3.0);
		const auto selected = Send(host, 10, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 3.5, 113.5);
		assert(RequirePacket(selected, 10, "characterResult").at("ok") == true);
		(void)host.HandleConnected(30, 114.0);
		host.BindSessionProfile(30, "slot-acct");
		const auto hello30 = Send(host, 30, SlotHello(), 4.0, 114.1);
		assert(RequirePacket(hello30, 30, "sessionReady").at("persistenceProfile") == "slot-acct#1");
		const auto bobState = RequirePacket(hello30, 20, "friendState");
		assert(bobState.at("friends").size() == 1);
		assert(bobState.at("friends").at(0).at("online") == false);
		host.HandleDisconnected(30, 115.0);
		host.HandleDisconnected(20, 115.1);
		std::filesystem::remove_all(directory);
	}

	void TestFriendInviteShowsRealInviterName()
	{
		const auto directory = TempDirectory("invite");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Slot Invite" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3));
		(void)JoinFriend(host, 10, "slot-inviter", 1.0, 100.0);
		SetName(host, 10, "Inviter", 1.1);
		const auto victimId = JoinFriend(host, 20, "slot-victim", 1.2, 100.2).playerId;
		SetName(host, 20, "Victim", 1.3);
		(void)JoinFriend(host, 30, "slot-stranger", 1.4, 100.4);
		SetName(host, 30, "Sneaky", 1.5);
		MakeFriends(host, 10, 20, victimId, 2.0);
		const auto created = Send(host, 10, Json{ { "type", "factionCreate" }, { "name", "Crew" } }, 2.5, 102.5);
		assert(RequirePacket(created, 10, "factionResult").at("ok") == true);
		const auto invited = Send(host, 10, Json{ { "type", "factionInvite" }, { "targetPlayerId", victimId } }, 3.0, 103.0);
		assert(RequirePacket(invited, 20, "factionInviteReceived").at("displayName") == "Inviter");
		const auto strangerCreated = Send(host, 30, Json{ { "type", "factionCreate" }, { "name", "Other" } }, 3.5, 103.5);
		assert(RequirePacket(strangerCreated, 30, "factionResult").at("ok") == true);
		const auto strangerInvited = Send(host, 30, Json{ { "type", "factionInvite" }, { "targetPlayerId", victimId } }, 4.0, 104.0);
		assert(RequirePacket(strangerInvited, 20, "factionInviteReceived").at("displayName") == "Stranger");
		host.HandleDisconnected(10, 105.0);
		host.HandleDisconnected(20, 105.1);
		host.HandleDisconnected(30, 105.2);
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestSlotsHaveIndependentFriendsAndNames();
	TestDeletingSlotKeepsSiblingFriendsAndNames();
	TestLegacySessionsKeyByAccount();
	TestSlotAdminSeesRealNames();
	TestSupersededSlotDropsFriendOnline();
	TestFriendInviteShowsRealInviterName();
	TestNoFriendsCapabilityGetsNoFriendState();
	TestSlotAcceptRefreshesRosterAndFactionNames();
	TestOfflineFactionMemberResolvesForSlotFriend();
	TestUnboundReconnectDropsStoredSocialKey();
	return 0;
}
