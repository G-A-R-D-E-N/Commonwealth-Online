#include "ServerFactionTestSupport.h"
#include "F4TPrivacyValidation.h"
#include "ServerMapPresence.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using CommonwealthOnline::Server::CanSeeOnMap;
	using CommonwealthOnline::Server::MapPresencePeer;
	using CommonwealthOnline::Server::MapViewer;
	namespace Privacy = F4T::PrivacyValidation;

	Json PrivacyJson(const char* a_onlineStatus, bool a_showLocation, const char* a_friendRequests)
	{
		return Json{ { "onlineStatus", a_onlineStatus }, { "showLocation", a_showLocation }, { "friendRequests", a_friendRequests } };
	}

	Joined JoinWith(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, Json a_capabilities, const Json& a_privacy, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", std::move(a_capabilities) } };
		if (!a_privacy.is_null()) hello["privacy"] = a_privacy;
		joined.hello = Send(a_host, a_connectionId, hello, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	Joined JoinPrivate(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds, const Json& a_privacy)
	{
		return JoinWith(a_host, a_connectionId, a_profileId, Json::array({ "social-names-v1", "friends-v1", "privacy-v1" }), a_privacy, a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> SetPrivacy(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_privacy, double a_monotonicSeconds)
	{
		return Send(a_host, a_connectionId, Json{ { "type", "privacySet" }, { "privacy", a_privacy } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	Json FriendListFor(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		return RequirePacket(Send(a_host, a_connectionId, Json{ { "type", "friendListRequest" } }, a_monotonicSeconds, a_monotonicSeconds + 99.0), a_connectionId, "friendState");
	}

	void MakeFriends(ProtocolHost& a_host, std::uint32_t a_requesterConnectionId, std::uint32_t a_accepterConnectionId, std::uint32_t a_accepterPlayerId, double a_monotonicSeconds)
	{
		const auto sent = Send(a_host, a_requesterConnectionId, Json{ { "type", "friendRequest" }, { "targetPlayerId", a_accepterPlayerId } }, a_monotonicSeconds, a_monotonicSeconds + 99.0);
		assert(RequirePacket(sent, a_requesterConnectionId, "friendResult").at("ok") == true);
		const auto requestId = RequirePacket(sent, a_accepterConnectionId, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = Send(a_host, a_accepterConnectionId, Json{ { "type", "friendRequestResponse" }, { "requestId", requestId }, { "accept", true } }, a_monotonicSeconds + 0.1, a_monotonicSeconds + 99.0);
		assert(RequirePacket(accepted, a_requesterConnectionId, "friendResult").at("ok") == true);
	}

	Json NonLeaderMember(const Json& a_state)
	{
		for (const auto& member : a_state.at("faction").at("members")) {
			if (member.at("leader") == false) return member;
		}
		assert(false);
		return {};
	}

	std::vector<std::uint32_t> VisibleIds(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<std::uint32_t> ids;
		const auto packet = RequirePacket(a_frames, a_connectionId, "mapPresence");
		for (const auto& entry : packet.at("players")) ids.push_back(entry.at("playerId").get<std::uint32_t>());
		std::sort(ids.begin(), ids.end());
		return ids;
	}

	std::vector<std::uint32_t> Pair(std::uint32_t a_first, std::uint32_t a_second)
	{
		return { (std::min)(a_first, a_second), (std::max)(a_first, a_second) };
	}

	void TestValidatorAcceptsAndRejects()
	{
		const auto valid = Privacy::NormalizePrivacy(PrivacyJson("Nobody", false, "FriendsOfFriends"));
		assert(valid.has_value());
		assert(valid->onlineStatus == Privacy::OnlineStatus::Nobody && !valid->showLocation && valid->friendRequests == Privacy::FriendRequests::FriendsOfFriends);
		assert(!Privacy::NormalizePrivacy(Json::array()));
		assert(!Privacy::NormalizePrivacy(Json{ { "onlineStatus", "Nobody" }, { "showLocation", true } }));
		assert(!Privacy::NormalizePrivacy(Json{ { "onlineStatus", "Nobody" }, { "showLocation", true }, { "friendRequests", "Everyone" }, { "extra", 1 } }));
		assert(!Privacy::NormalizePrivacy(PrivacyJson("Offline", true, "Everyone")));
		assert(!Privacy::NormalizePrivacy(PrivacyJson("Nobody", true, "Friends")));
		assert(!Privacy::NormalizePrivacy(Json{ { "onlineStatus", "Nobody" }, { "showLocation", "false" }, { "friendRequests", "Everyone" } }));
	}

	void TestPrivacyCapabilityIsAdvertised()
	{
		ProtocolHost host{ "Privacy Capability" };
		const auto capabilities = RequirePacket(host.HandleConnected(10, 100.0), 10, "welcome").at("capabilities");
		assert(std::find(capabilities.begin(), capabilities.end(), Json("privacy-v1")) != capabilities.end());
	}

	void TestOldClientsGetDefaults()
	{
		ProtocolHost host{ "Privacy Old Client" };
		assert(host.EnableSocial(nullptr));
		host.SetMapPublicPlayers(false);
		const auto alice = JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto oldBob = JoinWith(host, 20, "p20", Json::array({ "social-names-v1", "friends-v1" }), PrivacyJson("Nobody", false, "Nobody"), 1.1, 100.1);
		MakeFriends(host, 10, 20, oldBob.playerId, 2.0);
		assert(FriendListFor(host, 10, 2.5).at("friends").at(0).at("online") == true);
		assert(!HasPacket(SetPrivacy(host, 20, PrivacyJson("Nobody", false, "Nobody"), 3.0), 10, "friendState"));
		assert(FriendListFor(host, 10, 3.5).at("friends").at(0).at("online") == true);
		Send(host, 10, Transform(100.0), 4.0, 104.0);
		Send(host, 20, Transform(200.0), 4.0, 104.0);
		assert(VisibleIds(host.PollPresence(0.0, 104.5), 10) == Pair(alice.playerId, oldBob.playerId));
	}

	void TestHelloPrivacyIsValidatedAndFallsBackToDefaults()
	{
		ProtocolHost host{ "Privacy Hello" };
		assert(host.EnableSocial(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto strictBob = JoinPrivate(host, 20, "p20", 1.1, 100.1, PrivacyJson("Friends", true, "Nobody"));
		const auto badCarl = JoinPrivate(host, 30, "p30", 1.2, 100.2, PrivacyJson("Friends", true, "Bogus"));
		const auto declined = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", strictBob.playerId } }, 2.0, 102.0);
		assert(RequirePacket(declined, 10, "friendResult").at("error") == "declined");
		const auto accepted = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", badCarl.playerId } }, 2.1, 102.1);
		assert(RequirePacket(accepted, 10, "friendResult").at("ok") == true);
	}

	void TestPrivacySetBeforeHelloIsIgnored()
	{
		ProtocolHost host{ "Privacy Before Hello" };
		assert(host.EnableSocial(nullptr));
		(void)host.HandleConnected(10, 100.0);
		assert(!HasPacket(SetPrivacy(host, 10, PrivacyJson("Nobody", false, "Nobody"), 1.0), 10, "friendState"));
	}

	void TestNobodyHidesOnlineFromFriendsAndSelfSeesIt()
	{
		ProtocolHost host{ "Privacy Online Friends" };
		assert(host.EnableSocial(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		MakeFriends(host, 10, 20, bob.playerId, 2.0);
		const auto before = FriendListFor(host, 10, 2.5).at("friends").at(0);
		assert(before.at("online") == true && before.contains("playerId"));

		const auto hidden = SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Everyone"), 3.0);
		const auto hiddenRow = RequirePacket(hidden, 10, "friendState").at("friends").at(0);
		assert(hiddenRow.at("online") == false && !hiddenRow.contains("playerId"));
		const auto selfRow = FriendListFor(host, 20, 3.1).at("friends").at(0);
		assert(selfRow.at("online") == true);
		assert(FriendListFor(host, 10, 3.2).at("friends").at(0).at("online") == false);

		const auto shown = SetPrivacy(host, 20, PrivacyJson("Everyone", true, "Everyone"), 4.0);
		const auto shownRow = RequirePacket(shown, 10, "friendState").at("friends").at(0);
		assert(shownRow.at("online") == true && shownRow.contains("playerId"));
	}

	void TestNobodyIsOfflineInFactionRosterExceptForSelf()
	{
		ProtocolHost host{ "Privacy Online Faction" };
		assert(host.EnableFactions(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		FormFaction(host, 10, 20, bob.playerId, "Alpha");
		const auto hidden = SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Everyone"), 3.0);
		const auto leaderView = NonLeaderMember(RequirePacket(hidden, 10, "factionState"));
		assert(leaderView.at("online") == false && !leaderView.contains("playerId"));
		const auto selfView = NonLeaderMember(RequirePacket(hidden, 20, "factionState"));
		assert(selfView.at("online") == true && selfView.contains("playerId"));
	}

	void TestNobodyHiddenFromFactionListCountAndRoster()
	{
		ProtocolHost host{ "Privacy Faction List" };
		assert(host.EnableFactions(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		(void)JoinPrivate(host, 30, "p30", 1.2, 100.2, Json{});
		FormFaction(host, 10, 20, bob.playerId, "Alpha");
		(void)SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Everyone"), 3.0);

		const auto strangerList = RequirePacket(Send(host, 30, Json{ { "type", "factionList" } }, 3.1, 103.1), 30, "factionList");
		assert(strangerList.at("factions").front().at("onlineCount") == 1);
		const auto selfList = RequirePacket(Send(host, 20, Json{ { "type", "factionList" } }, 3.2, 103.2), 20, "factionList");
		assert(selfList.at("factions").front().at("onlineCount") == 2);

		const auto roster = RequirePacket(Send(host, 30, Json{ { "type", "playerRosterRequest" } }, 3.3, 103.3), 30, "playerRoster");
		for (const auto& player : roster.at("players")) assert(player.at("playerId").get<std::uint32_t>() != bob.playerId);
	}

	void TestPrivacyChangeSendsNoFactionStateWhenFactionsDisabled()
	{
		ProtocolHost host{ "Privacy No Factions" };
		assert(host.EnableSocial(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto changed = SetPrivacy(host, 10, PrivacyJson("Nobody", true, "Everyone"), 3.0);
		assert(!HasPacket(changed, 10, "factionState"));
		assert(!HasPacket(changed, 10, "playerRoster"));
	}

	void TestFriendRequestPolicyDeclinesWithoutLeak()
	{
		ProtocolHost host{ "Privacy Requests" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, PrivacyJson("Friends", true, "Nobody"));
		const auto carl = JoinPrivate(host, 30, "p30", 1.2, 100.2, PrivacyJson("Friends", true, "FriendsOfFriends"));
		const auto dan = JoinPrivate(host, 40, "p40", 1.3, 100.3, Json{});
		(void)JoinPrivate(host, 50, "p50", 1.4, 100.4, Json{});
		MakeFriends(host, 10, 40, dan.playerId, 2.0);
		MakeFriends(host, 30, 40, dan.playerId, 2.5);

		const auto toNobody = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", bob.playerId } }, 3.0, 103.0);
		const auto refusal = RequirePacket(toNobody, 10, "friendResult");
		assert(refusal.at("ok") == false && refusal.at("error") == "declined");
		assert(!HasPacket(toNobody, 20, "friendRequestReceived"));

		const auto strangerToFoF = Send(host, 50, Json{ { "type", "friendRequest" }, { "targetPlayerId", carl.playerId } }, 3.1, 103.1);
		assert(RequirePacket(strangerToFoF, 50, "friendResult").at("error") == "declined");
		assert(!HasPacket(strangerToFoF, 30, "friendRequestReceived"));

		const auto mutualToFoF = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", carl.playerId } }, 3.2, 103.2);
		assert(RequirePacket(mutualToFoF, 10, "friendResult").at("ok") == true);
		assert(HasPacket(mutualToFoF, 30, "friendRequestReceived"));

		const auto toEveryone = Send(host, 50, Json{ { "type", "friendRequest" }, { "targetPlayerId", alice.playerId } }, 3.3, 103.3);
		assert(RequirePacket(toEveryone, 50, "friendResult").at("ok") == true);
	}

	void TestReciprocalRequestStillAcceptsWhenTargetIsNobody()
	{
		ProtocolHost host{ "Privacy Reciprocal" };
		assert(host.EnableSocial(nullptr));
		const auto alice = JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		const auto sent = Send(host, 20, Json{ { "type", "friendRequest" }, { "targetPlayerId", alice.playerId } }, 2.0, 102.0);
		assert(RequirePacket(sent, 20, "friendResult").at("ok") == true);
		(void)SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Nobody"), 2.5);
		const auto accepted = Send(host, 10, Json{ { "type", "friendRequest" }, { "targetPlayerId", bob.playerId } }, 3.0, 103.0);
		assert(RequirePacket(accepted, 10, "friendResult").at("ok") == true);
		assert(RequirePacket(accepted, 10, "friendState").at("friends").size() == 1);
	}

	void TestLivePrivacyChangesAreRateLimited()
	{
		ProtocolHost host{ "Privacy Rate" };
		assert(host.EnableSocial(nullptr));
		(void)JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		MakeFriends(host, 10, 20, bob.playerId, 2.0);
		const Json toggles[] = { PrivacyJson("Nobody", true, "Everyone"), PrivacyJson("Everyone", true, "Everyone"), PrivacyJson("Nobody", true, "Everyone"), PrivacyJson("Friends", true, "Everyone") };
		for (const auto& toggle : toggles) assert(HasPacket(SetPrivacy(host, 20, toggle, 5.0), 10, "friendState"));
		assert(SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Everyone"), 5.0).empty());
		assert(FriendListFor(host, 10, 5.1).at("friends").at(0).at("online") == true);
		assert(HasPacket(SetPrivacy(host, 20, PrivacyJson("Nobody", true, "Everyone"), 6.0), 10, "friendState"));
		assert(FriendListFor(host, 10, 6.1).at("friends").at(0).at("online") == false);
	}

	void TestShowLocationOffHidesFromFriendsOnly()
	{
		ProtocolHost host{ "Privacy Location" };
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		const auto alice = JoinPrivate(host, 10, "p10", 1.0, 100.0, Json{});
		const auto bob = JoinPrivate(host, 20, "p20", 1.1, 100.1, Json{});
		const auto carl = JoinPrivate(host, 30, "p30", 1.2, 100.2, Json{});
		const auto eve = JoinPrivate(host, 40, "p40", 1.3, 100.3, Json{});
		MakeFriends(host, 10, 20, bob.playerId, 2.0);
		AssertResult(Send(host, 20, Create("Bravo Team", "BRV"), 2.5, 102.5), 20, "create", true);
		const auto invite = Send(host, 20, Invite(carl.playerId), 2.6, 102.6);
		AssertResult(Send(host, 30, Respond(InviteIdFrom(invite, 30), true), 2.7, 102.7), 30, "inviteResponse", true);
		host.SetMapPublicPlayers(false);
		for (const auto connectionId : { 10u, 20u, 30u, 40u }) Send(host, connectionId, Transform(100.0 * connectionId), 3.0, 103.0);
		(void)SetPrivacy(host, 20, PrivacyJson("Friends", false, "Everyone"), 3.5);
		const auto hidden = host.PollPresence(0.0, 104.0);
		assert(VisibleIds(hidden, 10) == std::vector<std::uint32_t>{ alice.playerId });
		auto bobSees = std::vector<std::uint32_t>{ alice.playerId, bob.playerId, carl.playerId };
		std::sort(bobSees.begin(), bobSees.end());
		assert(VisibleIds(hidden, 20) == bobSees);
		assert(VisibleIds(hidden, 30) == Pair(bob.playerId, carl.playerId));
		assert(VisibleIds(hidden, 40) == std::vector<std::uint32_t>{ eve.playerId });

		(void)SetPrivacy(host, 20, PrivacyJson("Friends", true, "Everyone"), 3.6);
		const auto shown = host.PollPresence(2.0, 104.1);
		assert(VisibleIds(shown, 10) == Pair(alice.playerId, bob.playerId));
		assert(VisibleIds(shown, 40) == std::vector<std::uint32_t>{ eve.playerId });
	}

	void TestCanSeeOnMapTeamSurvivesHiddenLocation()
	{
		MapPresencePeer subject;
		subject.playerId = 1;
		subject.teamId = "T1";
		subject.showLocation = false;
		assert(CanSeeOnMap(true, MapViewer{ 1, "", "" }, subject, false));
		assert(CanSeeOnMap(false, MapViewer{ 2, "", "T1" }, subject, false));
		assert(!CanSeeOnMap(true, MapViewer{ 3, "", "" }, subject, true));
		subject.showLocation = true;
		assert(CanSeeOnMap(true, MapViewer{ 3, "", "" }, subject, false));
	}
}

int main()
{
	TestValidatorAcceptsAndRejects();
	TestPrivacyCapabilityIsAdvertised();
	TestOldClientsGetDefaults();
	TestHelloPrivacyIsValidatedAndFallsBackToDefaults();
	TestPrivacySetBeforeHelloIsIgnored();
	TestNobodyHidesOnlineFromFriendsAndSelfSeesIt();
	TestNobodyIsOfflineInFactionRosterExceptForSelf();
	TestNobodyHiddenFromFactionListCountAndRoster();
	TestPrivacyChangeSendsNoFactionStateWhenFactionsDisabled();
	TestFriendRequestPolicyDeclinesWithoutLeak();
	TestReciprocalRequestStillAcceptsWhenTargetIsNobody();
	TestLivePrivacyChangesAreRateLimited();
	TestShowLocationOffHidesFromFriendsOnly();
	TestCanSeeOnMapTeamSurvivesHiddenLocation();
	return 0;
}
