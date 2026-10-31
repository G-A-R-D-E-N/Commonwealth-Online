#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	void TestOfficerRankPermissions()
	{
		ProtocolHost host{ "Factions Officer Rank" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto officer = Join(host, 20, "p20", 1.1, 100.2);
		const auto member = Join(host, 30, "p30", 1.2, 100.4);
		const auto outsider = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Officer Team"), 2.0, 101.0);
		Send(host, 10, Invite(officer.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		Send(host, 10, Invite(member.playerId), 5.0, 104.0);
		Send(host, 30, Respond("i-2", true), 6.0, 105.0);

		AssertResult(Send(host, 20, SetRank("p30", "officer"), 7.0, 106.0), 20, "setRank", false, "notLeader");
		AssertResult(Send(host, 10, SetRank("p10", "officer"), 8.0, 107.0), 10, "setRank", false, "invalidTarget");
		AssertResult(Send(host, 10, SetRank("p99", "officer"), 9.0, 108.0), 10, "setRank", false, "invalidTarget");
		AssertResult(Send(host, 10, SetRank("p40", "officer"), 10.0, 109.0), 10, "setRank", false, "invalidTarget");

		const auto frames = Send(host, 10, SetRank("p20", "officer"), 11.0, 110.0);
		AssertResult(frames, 10, "setRank", true);
		const auto state = RequirePacket(frames, 10, "factionState").at("faction");
		assert(state.at("members").at(0).at("officer") == false);
		assert(state.at("members").at(1).at("officer") == true);
		assert(state.at("members").at(2).at("officer") == false);
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("members").at(1).at("officer") == true);
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 12.0, 111.0), 10, "setRank", true);

		AssertResult(Send(host, 20, Invite(outsider.playerId), 13.0, 112.0), 20, "invite", true);
		AssertResult(Send(host, 30, SetRank("p20", "member"), 14.0, 113.0), 30, "setRank", false, "notLeader");
		assert(Send(host, 10, Json{ { "type", "factionSetRank" }, { "targetProfileId", "p20" }, { "rank", "leader" } }, 15.0, 114.0).empty());

		const auto kicked = Send(host, 20, Kick("p30"), 16.0, 115.0);
		AssertResult(kicked, 20, "kick", true);
		AssertResult(kicked, 30, "kick", false, "kicked");
		assert(RequirePacket(kicked, 30, "factionState").at("faction").is_null());
		AssertResult(Send(host, 20, Kick("p10"), 17.0, 116.0), 20, "kick", false, "notLeader");

		AssertResult(Send(host, 10, SetRank("p20", "member"), 18.0, 117.0), 10, "setRank", true);
		AssertResult(Send(host, 20, Invite(outsider.playerId), 19.0, 118.0), 20, "invite", false, "notLeader");
	}

	void TestOfficerKickRules()
	{
		ProtocolHost host{ "Factions Officer Kick" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto first = Join(host, 20, "p20", 1.1, 100.2);
		const auto second = Join(host, 30, "p30", 1.2, 100.4);
		const auto third = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Kick Rules Team"), 2.0, 101.0);
		Send(host, 10, Invite(first.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		Send(host, 10, Invite(second.playerId), 5.0, 104.0);
		Send(host, 30, Respond("i-2", true), 6.0, 105.0);
		Send(host, 10, Invite(third.playerId), 7.0, 106.0);
		Send(host, 40, Respond("i-3", true), 8.0, 107.0);
		Send(host, 10, SetRank("p20", "officer"), 9.0, 108.0);
		Send(host, 10, SetRank("p30", "officer"), 10.0, 109.0);

		AssertResult(Send(host, 20, Kick("p30"), 11.0, 110.0), 20, "kick", false, "notLeader");
		AssertResult(Send(host, 30, Kick("p20"), 12.0, 111.0), 30, "kick", false, "notLeader");
		AssertResult(Send(host, 20, Kick("p10"), 13.0, 112.0), 20, "kick", false, "notLeader");
		AssertResult(Send(host, 20, Kick("p20"), 14.0, 113.0), 20, "kick", false, "invalidTarget");

		const auto plainKick = Send(host, 20, Kick("p40"), 15.0, 114.0);
		AssertResult(plainKick, 20, "kick", true);
		assert(RequirePacket(plainKick, 40, "factionState").at("faction").is_null());
		AssertResult(Send(host, 10, Kick("p30"), 16.0, 115.0), 10, "kick", true);
	}

	void TestOfficerLeaveAndKickDropRank()
	{
		ProtocolHost host{ "Factions Officer Drop" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto first = Join(host, 20, "p20", 1.1, 100.2);
		const auto second = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Drop Team"), 2.0, 101.0);
		Send(host, 10, Invite(first.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		Send(host, 10, Invite(second.playerId), 5.0, 104.0);
		Send(host, 30, Respond("i-2", true), 6.0, 105.0);
		Send(host, 10, SetRank("p20", "officer"), 7.0, 106.0);
		Send(host, 10, SetRank("p30", "officer"), 8.0, 107.0);

		AssertResult(Send(host, 20, Leave(), 9.0, 108.0), 20, "leave", true);
		const auto rejoin = Send(host, 10, Invite(first.playerId), 10.0, 109.0);
		const auto returnedFrames = Send(host, 20, Respond(InviteIdFrom(rejoin, 20), true), 11.0, 110.0);
		const auto returned = RequirePacket(returnedFrames, 20, "factionState").at("faction");
		assert(returned.at("members").at(1).at("officer") == true);
		assert(returned.at("members").at(2).at("officer") == false);

		AssertResult(Send(host, 10, Kick("p30"), 12.0, 111.0), 10, "kick", true);
		const auto reinvite = Send(host, 10, Invite(second.playerId), 13.0, 112.0);
		const auto back = Send(host, 30, Respond(InviteIdFrom(reinvite, 30), true), 14.0, 113.0);
		assert(RequirePacket(back, 30, "factionState").at("faction").at("members").at(2).at("officer") == false);
	}

	void TestPromoteClearsOfficerFlag()
	{
		ProtocolHost host{ "Factions Officer Promote" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto officer = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Successor Officer Team"), 2.0, 101.0);
		Send(host, 10, Invite(officer.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		Send(host, 10, SetRank("p20", "officer"), 5.0, 104.0);

		const auto frames = Send(host, 10, Promote("p20"), 6.0, 105.0);
		AssertResult(frames, 10, "promote", true);
		const auto state = RequirePacket(frames, 10, "factionState").at("faction");
		assert(state.at("members").at(0).at("officer") == false);
		assert(state.at("members").at(1).at("leader") == true);
		assert(state.at("members").at(1).at("officer") == false);
		AssertResult(Send(host, 10, Invite(officer.playerId), 7.0, 106.0), 10, "invite", false, "notLeader");
	}

	void TestLeaderLeaveClearsSuccessorOfficerFlag()
	{
		ProtocolHost host{ "Factions Officer Succession" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto officer = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Succession Team"), 2.0, 101.0);
		Send(host, 10, Invite(officer.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 5.0, 104.0), 10, "setRank", true);

		const auto frames = Send(host, 10, Leave(), 6.0, 105.0);
		AssertResult(frames, 10, "leave", true);
		const auto successor = RequirePacket(frames, 20, "factionState").at("faction").at("members").at(0);
		assert(successor.at("profileId") == "p20" && successor.at("leader") == true && successor.at("officer") == false);
	}

	std::string SuccessorAfterLeave(const std::string& a_name, bool a_p20Officer, bool a_p20Online, bool a_p30Officer, bool a_p30Online)
	{
		ProtocolHost host{ a_name };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto first = Join(host, 20, "p20", 1.1, 100.2);
		const auto second = Join(host, 30, "p30", 1.2, 100.4);
		const auto third = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Succession Tiers"), 2.0, 101.0);
		Send(host, 10, Invite(first.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		Send(host, 10, Invite(second.playerId), 5.0, 104.0);
		Send(host, 30, Respond("i-2", true), 6.0, 105.0);
		Send(host, 10, Invite(third.playerId), 7.0, 106.0);
		Send(host, 40, Respond("i-3", true), 8.0, 107.0);
		if (a_p20Officer) Send(host, 10, SetRank("p20", "officer"), 9.0, 108.0);
		if (a_p30Officer) Send(host, 10, SetRank("p30", "officer"), 10.0, 109.0);
		host.HandleDisconnected(40, 110.0);
		if (!a_p20Online) host.HandleDisconnected(20, 110.1);
		if (!a_p30Online) host.HandleDisconnected(30, 110.2);
		AssertResult(Send(host, 10, Leave(), 11.0, 111.0), 10, "leave", true);
		const auto observer = Join(host, 41, "p40", 12.0, 112.0);
		const auto state = RequirePacket(observer.hello, 41, "factionState");
		for (const auto& member : state.at("faction").at("members")) {
			if (member.at("leader") == true) return member.at("profileId").get<std::string>();
		}
		return {};
	}

	void TestLeaderLeaveSuccessorTiers()
	{
		assert(SuccessorAfterLeave("Succession Online Officer", false, true, true, true) == "p30");
		assert(SuccessorAfterLeave("Succession Online Member", true, false, false, true) == "p30");
		assert(SuccessorAfterLeave("Succession Offline Officer", false, false, true, false) == "p30");
		assert(SuccessorAfterLeave("Succession Front Fallback", false, false, false, false) == "p20");
	}

	void TestExpiredInviteNoticesOnlineInviter()
	{
		ProtocolHost host{ "Factions Invite Expiry" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto invitee = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Expiry Team"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(invitee.playerId), 3.0, 102.0), 20);

		const auto swept = Send(host, 30, Leave(), 64.5, 163.0);
		AssertResult(swept, 10, "invite", false, "inviteExpired");
		AssertResult(swept, 30, "leave", false, "notMember");
		AssertResult(Send(host, 20, Respond(inviteId, true), 65.0, 163.5), 20, "inviteResponse", false, "inviteNotFound");
	}

	void TestIdleServerPollNoticesExpiredInviteOnce()
	{
		ProtocolHost host{ "Factions Invite Idle Poll" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto invitee = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Idle Poll Team"), 2.0, 101.0);
		AssertResult(Send(host, 10, Invite(invitee.playerId), 3.0, 102.0), 10, "invite", true);
		assert(host.PollFactionInvites(40.0, 140.0).empty());
		AssertResult(host.PollFactionInvites(64.5, 164.5), 10, "invite", false, "inviteExpired");
		assert(host.PollFactionInvites(65.0, 165.0).empty());
	}

	void TestExpiredInviteSkipsOfflineInviter()
	{
		ProtocolHost host{ "Factions Invite Offline" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto invitee = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Offline Team"), 2.0, 101.0);
		Send(host, 10, Invite(invitee.playerId), 3.0, 102.0);
		host.HandleDisconnected(10, 102.5);
		assert(!HasPacket(Send(host, 20, Leave(), 64.5, 163.0), 10, "factionResult"));
	}

	void TestExpiredInvitesNoticeInviterOnce()
	{
		ProtocolHost host{ "Factions Invite Once" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto first = Join(host, 20, "p20", 1.1, 100.2);
		const auto second = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Batch Team", "BAT"), 2.0, 101.0);
		AssertResult(Send(host, 10, Invite(first.playerId), 3.0, 102.0), 10, "invite", true);
		AssertResult(Send(host, 10, Invite(second.playerId), 3.1, 102.1), 10, "invite", true);

		const auto swept = Send(host, 20, Leave(), 64.5, 163.0);
		int notices = 0;
		for (const auto& frame : swept) {
			if (frame.connectionId == 10 && DecodePacket(frame).value("reason", std::string{}) == "inviteExpired") ++notices;
		}
		assert(notices == 1);
	}

	void TestPendingInviteCarriesFactionTag()
	{
		ProtocolHost host{ "Factions Invite Tag" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto invitee = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Tagged Invite Team", "TIT"), 2.0, 101.0);
		AssertResult(Send(host, 10, Invite(invitee.playerId), 3.0, 102.0), 10, "invite", true);
		host.HandleDisconnected(20, 102.5);

		const auto back = Join(host, 21, "p20", 3.5, 103.0);
		const auto pending = RequirePacket(back.hello, 21, "factionState").at("pendingInvites");
		assert(pending.size() == 1 && pending.at(0).at("factionTag") == "TIT");
	}

	void TestKickClearsInvitesFromKicked()
	{
		ProtocolHost host{ "Factions Kick Invites" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto officer = Join(host, 20, "p20", 1.1, 100.2);
		const auto outsider = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Kick Invite Team"), 2.0, 101.0);
		Send(host, 10, Invite(officer.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 4.0, 103.0);
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 5.0, 104.0), 10, "setRank", true);
		const auto sent = Send(host, 20, Invite(outsider.playerId), 6.0, 105.0);
		AssertResult(sent, 20, "invite", true);
		const auto inviteId = InviteIdFrom(sent, 40);

		const auto purged = Send(host, 10, Kick("p20"), 7.0, 106.0);
		AssertResult(purged, 10, "kick", true);
		AssertResult(purged, 20, "kick", false, "kicked");
		assert(RequirePacket(purged, 40, "factionState").at("pendingInvites").empty());
		AssertResult(Send(host, 40, Respond(inviteId, true), 8.0, 107.0), 40, "inviteResponse", false, "inviteNotFound");
	}
}

int main()
{
	TestOfficerRankPermissions();
	TestOfficerKickRules();
	TestOfficerLeaveAndKickDropRank();
	TestPromoteClearsOfficerFlag();
	TestLeaderLeaveClearsSuccessorOfficerFlag();
	TestLeaderLeaveSuccessorTiers();
	TestKickClearsInvitesFromKicked();
	TestExpiredInviteNoticesOnlineInviter();
	TestExpiredInviteSkipsOfflineInviter();
	TestIdleServerPollNoticesExpiredInviteOnce();
	TestExpiredInvitesNoticeInviterOnce();
	TestPendingInviteCarriesFactionTag();
	return 0;
}
