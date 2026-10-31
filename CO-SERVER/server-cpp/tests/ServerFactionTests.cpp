#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	bool WelcomeHasCapability(ProtocolHost& a_host, std::uint32_t a_connectionId, std::string_view a_capability)
	{
		const auto welcome = DecodePacket(a_host.HandleConnected(a_connectionId, 100.0).front());
		for (const auto& capability : welcome.at("capabilities")) {
			if (capability == std::string{ a_capability }) return true;
		}
		return false;
	}

	void TestDisabledRejectsFactionPackets()
	{
		ProtocolHost host{ "Factions Disabled" };
		Join(host, 10, "p10", 1.0, 100.0);
		const auto before = host.Stats();
		assert(Send(host, 10, Create("Alpha Team"), 2.0, 100.2).empty());
		assert(host.Stats().packetsRejected == before.packetsRejected + 1);
		assert(Send(host, 10, Json{ { "type", "playerRosterRequest" } }, 2.1, 100.3).empty());
		assert(host.Stats().packetsRejected == before.packetsRejected + 2);
		assert(host.Stats().factionPacketsReceived == 0);
	}

	void TestCreateValidatesAndNamesAreUnique()
	{
		ProtocolHost host{ "Factions Create" };
		assert(host.EnableFactions(nullptr));
		assert(!host.EnableFactions(nullptr));
		const auto leader = Join(host, 10, "p10", 1.0, 100.0);
		const auto other = Join(host, 20, "p20", 1.1, 100.2);

		auto frames = Send(host, 10, Create("Alpha Team", "ALP"), 2.0, 101.0);
		AssertResult(frames, 10, "create", true);
		const auto state = RequirePacket(frames, 10, "factionState").at("faction");
		assert(state.at("name") == "Alpha Team");
		assert(state.at("tag") == "ALP");
		assert(state.at("leaderPlayerId") == leader.playerId);
		assert(state.at("members").size() == 1);

		AssertResult(Send(host, 20, Create("alpha TEAM"), 2.1, 101.1), 20, "create", false, "nameTaken");
		AssertResult(Send(host, 20, Create("Beta Team", "alp"), 2.2, 101.2), 20, "create", false, "tagTaken");
		AssertResult(Send(host, 10, Create("Beta Team"), 2.3, 101.3), 10, "create", false, "alreadyInFaction");
		AssertResult(Send(host, 20, Create("Gamma Team"), 2.4, 101.4), 20, "create", true);
		assert(other.playerId != leader.playerId);
	}

	void TestCreateAnswersBadInput()
	{
		ProtocolHost host{ "Factions Create Input" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		AssertResult(Send(host, 10, Create("ab"), 2.0, 101.0), 10, "create", false, "invalidName");
		AssertResult(Send(host, 10, Create("Bad_Name"), 3.0, 102.0), 10, "create", false, "invalidName");
		AssertResult(Send(host, 10, Create("Sh1t Crew"), 3.5, 102.5), 10, "create", false, "invalidName");
		AssertResult(Send(host, 10, Create("Valid Name", "NAZI"), 3.6, 102.6), 10, "create", false, "invalidTag");
		AssertResult(Send(host, 10, Create("Valid Name", "A"), 4.0, 103.0), 10, "create", false, "invalidTag");
		AssertResult(Send(host, 10, Create("Valid Name", "ABCDE"), 5.0, 104.0), 10, "create", false, "invalidTag");
		assert(Send(host, 10, Json{ { "type", "factionCreate" }, { "name", 5 } }, 6.0, 105.0).empty());
		assert(Send(host, 10, Json{ { "type", "factionCreate" }, { "name", "Valid Name" }, { "tag", 7 } }, 6.5, 105.5).empty());

		Join(host, 20, "p20", 1.1, 100.2);
		const auto blank = Send(host, 20, Create("Blank Tag", "   "), 2.0, 101.0);
		AssertResult(blank, 20, "create", true);
		assert(RequirePacket(blank, 20, "factionState").at("faction").at("tag") == "");

		Join(host, 30, "p30", 1.2, 100.4);
		const auto lower = Send(host, 30, Create("Lower Tag", "  ab  "), 2.0, 101.0);
		AssertResult(lower, 30, "create", true);
		assert(RequirePacket(lower, 30, "factionState").at("faction").at("tag") == "AB");
	}

	void TestCreateNeedsIdentity()
	{
		ProtocolHost host{ "Factions Identity" };
		assert(host.EnableFactions(nullptr));
		host.HandleConnected(40, 100.0);
		host.HandleMessage(40, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 100.1);
		AssertResult(Send(host, 40, Create("Nameless Team"), 2.0, 101.0), 40, "create", false, "identityUnavailable");
	}

	void TestInviteAcceptDeclineAndWrongTarget()
	{
		ProtocolHost host{ "Factions Invites" };
		assert(host.EnableFactions(nullptr));
		const auto leader = Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		const auto third = Join(host, 30, "p30", 1.2, 100.4);
		Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Alpha Team", "ALP"), 2.0, 101.0);

		auto frames = Send(host, 10, Invite(second.playerId), 3.0, 102.0);
		AssertResult(frames, 10, "invite", true);
		const auto received = RequirePacket(frames, 20, "factionInviteReceived");
		assert(received.at("factionName") == "Alpha Team");
		assert(received.at("factionTag") == "ALP");
		assert(received.at("fromPlayerId") == leader.playerId);
		assert(received.at("expiresInSeconds").is_number_integer());
		assert(received.at("expiresInSeconds").get<std::int64_t>() == 60);
		const auto inviteId = received.at("inviteId").get<std::string>();

		AssertResult(Send(host, 20, Invite(third.playerId), 3.1, 102.1), 20, "invite", false, "notLeader");
		AssertResult(Send(host, 10, Invite(leader.playerId), 3.2, 102.2), 10, "invite", false, "invalidTarget");
		AssertResult(Send(host, 10, Invite(second.playerId), 3.3, 102.3), 10, "invite", false, "inviteExists");
		AssertResult(Send(host, 30, Respond(inviteId, true), 3.4, 102.4), 30, "inviteResponse", false, "notInvited");

		frames = Send(host, 20, Respond(inviteId, true), 3.5, 102.5);
		AssertResult(frames, 20, "inviteResponse", true);
		assert(HasPacket(frames, 10, "factionState"));
		assert(HasPacket(frames, 20, "factionState"));
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("members").size() == 2);
		AssertResult(Send(host, 10, Invite(second.playerId), 3.6, 102.6), 10, "invite", false, "targetInFaction");

		frames = Send(host, 10, Invite(third.playerId), 4.5, 103.0);
		const auto declineId = InviteIdFrom(frames, 30);
		frames = Send(host, 30, Respond(declineId, false), 4.6, 103.1);
		AssertResult(frames, 30, "inviteResponse", true);
		AssertResult(frames, 10, "invite", false, "declined");
		assert(!HasPacket(frames, 20, "factionState"));
	}

	void TestAcceptRejectedWhileInAnotherFaction()
	{
		ProtocolHost host{ "Factions Other" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto joiner = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Alpha Team"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(joiner.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Create("Beta Team"), 3.5, 102.5), 20, "create", true);
		AssertResult(Send(host, 20, Respond(inviteId, true), 4.0, 103.0), 20, "inviteResponse", false, "alreadyInFaction");
	}

	void TestAcceptRejectedWhenFactionFull()
	{
		ProtocolHost host{ "Factions Full" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Send(host, 10, Create("Full Team"), 2.0, 101.0);
		for (std::uint32_t index = 0; index < 8; ++index) {
			const auto connectionId = 20 + index;
			const auto joiner = Join(host, connectionId, "p" + std::to_string(connectionId), 1.0, 100.0);
			const auto inviteId = InviteIdFrom(Send(host, 10, Invite(joiner.playerId), 10.0 + index, 110.0 + index), connectionId);
			AssertResult(Send(host, connectionId, Respond(inviteId, true), 10.5 + index, 110.5 + index), connectionId, "inviteResponse", true);
		}
		for (std::uint32_t index = 0; index < 6; ++index) {
			const auto connectionId = 28 + index;
			const auto joiner = Join(host, connectionId, "p" + std::to_string(connectionId), 1.0, 100.0);
			const auto inviteId = InviteIdFrom(Send(host, 10, Invite(joiner.playerId), 100.0 + index, 200.0 + index), connectionId);
			AssertResult(Send(host, connectionId, Respond(inviteId, true), 100.5 + index, 200.5 + index), connectionId, "inviteResponse", true);
		}
		const auto pending = Join(host, 34, "p34", 1.0, 100.0);
		const auto pendingInvite = InviteIdFrom(Send(host, 10, Invite(pending.playerId), 106.0, 206.0), 34);
		const auto last = Join(host, 35, "p35", 1.0, 100.0);
		const auto lastInvite = InviteIdFrom(Send(host, 10, Invite(last.playerId), 107.0, 207.0), 35);
		AssertResult(Send(host, 35, Respond(lastInvite, true), 107.5, 207.5), 35, "inviteResponse", true);
		AssertResult(Send(host, 34, Respond(pendingInvite, true), 108.0, 208.0), 34, "inviteResponse", false, "factionFull");
	}

	void TestAcceptRejectedWhenFactionDeleted()
	{
		ProtocolHost host{ "Factions Deleted" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto joiner = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Dissolve Team"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(joiner.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 10, Leave(), 4.0, 103.0), 10, "leave", true);
		AssertResult(Send(host, 20, Respond(inviteId, true), 5.0, 104.0), 20, "inviteResponse", false, "inviteNotFound");
	}

	void TestExpiredInviteFails()
	{
		ProtocolHost host{ "Factions Expiry" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto target = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Expiry Team"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(target.playerId), 5.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(inviteId, true), 66.0, 163.0), 20, "inviteResponse", false, "inviteNotFound");
	}

	void TestLeaderLeavePromotesAndLastLeaveDeletes()
	{
		ProtocolHost host{ "Factions Leave" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		const auto third = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Leave Team"), 2.0, 101.0);
		Send(host, 10, Invite(second.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);
		Send(host, 10, Invite(third.playerId), 3.2, 102.2);
		Send(host, 30, Respond("i-2", true), 3.3, 102.3);

		auto frames = Send(host, 10, Leave(), 4.0, 103.0);
		AssertResult(frames, 10, "leave", true);
		assert(RequirePacket(frames, 10, "factionState").at("faction").is_null());
		const auto promoted = RequirePacket(frames, 20, "factionState").at("faction");
		assert(promoted.at("leaderPlayerId") == second.playerId);
		assert(promoted.at("members").size() == 2);
		assert(promoted.at("members").at(0).at("leader") == true);
		assert(promoted.at("members").at(1).at("leader") == false);

		assert(Send(host, 20, Leave(), 4.1, 103.1).size() >= 2);
		frames = Send(host, 30, Leave(), 4.2, 103.2);
		AssertResult(frames, 30, "leave", true);
		assert(RequirePacket(frames, 30, "factionState").at("faction").is_null());
		AssertResult(Send(host, 30, Leave(), 4.3, 103.3), 30, "leave", false, "notMember");

		AssertResult(Send(host, 10, Create("Solo Team"), 5.0, 104.0), 10, "create", true);
		AssertResult(Send(host, 10, Leave(), 5.1, 104.1), 10, "leave", true);
		AssertResult(Send(host, 20, Create("Solo Team"), 5.2, 104.2), 20, "create", true);
		const auto invitee = Join(host, 40, "p40", 1.3, 100.6);
		Join(host, 50, "p50", 1.4, 100.8);
		Send(host, 20, Invite(invitee.playerId), 5.3, 104.3);
		const auto lastLeave = Send(host, 20, Leave(), 5.4, 104.4);
		AssertResult(lastLeave, 20, "leave", true);
		assert(RequirePacket(lastLeave, 40, "factionState").at("pendingInvites").empty());
		assert(std::none_of(lastLeave.begin(), lastLeave.end(), [](const OutboundFrame& a_frame) { return a_frame.connectionId == 50; }));
	}

	void TestPendingInviteSurvivesLeaderLeave()
	{
		ProtocolHost host{ "Factions Pending" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto target = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Survive Team"), 2.0, 101.0);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		const auto targetInvite = InviteIdFrom(Send(host, 10, Invite(target.playerId), 3.5, 102.5), 30);
		AssertResult(Send(host, 10, Leave(), 4.0, 103.0), 10, "leave", true);

		const auto accepted = Send(host, 30, Respond(targetInvite, true), 5.0, 104.0);
		AssertResult(accepted, 30, "inviteResponse", true);
		const auto state = RequirePacket(accepted, 30, "factionState").at("faction");
		assert(state.at("name") == "Survive Team");
		assert(state.at("leaderPlayerId") == member.playerId);
		assert(state.at("members").size() == 2);
	}

	void TestKickRules()
	{
		ProtocolHost host{ "Factions Kick" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Kick Team"), 2.0, 101.0);
		Send(host, 10, Invite(member.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);

		AssertResult(Send(host, 20, Kick("p10"), 4.0, 103.0), 20, "kick", false, "notLeader");
		AssertResult(Send(host, 10, Kick("p10"), 4.1, 103.1), 10, "kick", false, "invalidTarget");
		AssertResult(Send(host, 10, Kick("p99"), 4.2, 103.2), 10, "kick", false, "invalidTarget");

		const auto frames = Send(host, 10, Kick("p20"), 4.3, 103.3);
		AssertResult(frames, 10, "kick", true);
		assert(RequirePacket(frames, 20, "factionState").at("faction").is_null());
		assert(RequirePacket(frames, 10, "factionState").at("faction").at("members").size() == 1);
	}

	void TestInviteLimits()
	{
		ProtocolHost host{ "Factions Limits" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Send(host, 10, Create("Limit Team"), 2.0, 101.0);
		std::vector<std::uint32_t> players;
		for (std::uint32_t connectionId = 11; connectionId <= 27; ++connectionId) {
			players.push_back(Join(host, connectionId, "p" + std::to_string(connectionId), 1.0, 100.0).playerId);
		}

		for (std::uint32_t index = 0; index < 8; ++index) {
			const auto connectionId = 11 + index;
			const auto frames = Send(host, 10, Invite(players[index]), 10.0 + index, 110.0 + index);
			AssertResult(frames, 10, "invite", true);
			const auto inviteId = InviteIdFrom(frames, connectionId);
			AssertResult(Send(host, connectionId, Respond(inviteId, true), 20.0 + index, 120.0 + index), connectionId, "inviteResponse", true);
		}
		AssertResult(Send(host, 10, Invite(players[8]), 18.5, 118.5), 10, "invite", false, "rateLimited");

		for (std::uint32_t index = 0; index < 7; ++index) {
			const auto connectionId = 20 + index;
			const auto frames = Send(host, 10, Invite(players[9 + index]), 100.0 + index, 200.0 + index);
			AssertResult(frames, 10, "invite", true);
			const auto inviteId = InviteIdFrom(frames, connectionId);
			AssertResult(Send(host, connectionId, Respond(inviteId, true), 100.5 + index, 200.5 + index), connectionId, "inviteResponse", true);
		}
		AssertResult(Send(host, 10, Invite(players[16]), 110.0, 210.0), 10, "invite", false, "factionFull");
	}

	void TestInviteCapacity()
	{
		ProtocolHost host{ "Factions Invite Capacity" };
		assert(host.EnableFactions(nullptr));
		for (std::uint32_t leader = 0; leader < 33; ++leader) {
			const auto connectionId = 10 + leader;
			Join(host, connectionId, "l" + std::to_string(leader), 1.0, 100.0);
			AssertResult(Send(host, connectionId, Create("Lead " + std::to_string(leader)), 2.0, 101.0), connectionId, "create", true);
		}
		std::vector<std::uint32_t> targets;
		for (std::uint32_t index = 0; index < 16; ++index) {
			targets.push_back(Join(host, 100 + index, "t" + std::to_string(index), 1.0, 100.0).playerId);
		}
		for (std::uint32_t round = 0; round < 8; ++round) {
			for (std::uint32_t leader = 0; leader < 32; ++leader) {
				const auto connectionId = 10 + leader;
				AssertResult(Send(host, connectionId, Invite(targets[(leader + round) % 16]), 10.0 + round, 110.0 + round), connectionId, "invite", true);
			}
		}
		AssertResult(Send(host, 42, Invite(targets[0]), 18.0, 118.0), 42, "invite", false, "inviteCapacity");
	}

	void TestMutationRateLimit()
	{
		ProtocolHost host{ "Factions Rate" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		for (int index = 0; index < 4; ++index) {
			AssertResult(Send(host, 10, Leave(), 2.0, 101.0), 10, "leave", false, "notMember");
		}
		AssertResult(Send(host, 10, Leave(), 2.0, 101.0), 10, "leave", false, "rateLimited");
		AssertResult(Send(host, 10, Leave(), 3.0, 102.0), 10, "leave", false, "notMember");
	}

	void TestKickRejectsUnknownMember()
	{
		ProtocolHost host{ "Factions Kick Unknown" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Send(host, 10, Create("Solo Kick"), 2.0, 101.0);
		AssertResult(Send(host, 10, Kick("p55"), 3.0, 102.0), 10, "kick", false, "invalidTarget");
	}

	void TestRosterAndRequestLimit()
	{
		ProtocolHost host{ "Factions Roster" };
		assert(host.EnableFactions(nullptr));
		const auto first = Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		assert(RequirePacket(second.hello, 10, "playerRoster").at("players").size() == 2);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 2.5, 100.5);
		Send(host, 20, Json{ { "type", "playerState" }, { "characterName", "Scout" } }, 2.6, 100.6);

		auto frames = Send(host, 10, Json{ { "type", "playerRosterRequest" } }, 3.0, 101.0);
		const auto roster = RequirePacket(frames, 10, "playerRoster");
		assert(roster.at("players").size() == 2);
		bool sawNomad = false;
		bool sawScout = false;
		for (const auto& player : roster.at("players")) {
			const bool self = player.at("playerId") == first.playerId;
			sawNomad = sawNomad || (self && player.at("name") == "Nomad");
			sawScout = sawScout || (!self && player.at("displayName") == "Stranger" && !player.contains("name"));
		}
		assert(sawNomad && sawScout);
		for (const auto& player : roster.at("players")) assert(player.at("tag") == "");
		Send(host, 10, Create("Tag Show", "TS"), 3.2, 100.9);
		const auto tagged = RequirePacket(Send(host, 20, Json{ { "type", "playerRosterRequest" } }, 3.3, 101.0), 20, "playerRoster");
		for (const auto& player : tagged.at("players")) {
			assert(player.at("tag") == (player.at("playerId") == first.playerId ? "TS" : ""));
		}
		assert(Send(host, 10, Json{ { "type", "playerRosterRequest" } }, 3.5, 101.5).empty());
		assert(HasPacket(Send(host, 10, Json{ { "type", "playerRosterRequest" } }, 4.0, 102.0), 10, "playerRoster"));
	}

	void TestConnectShowsMembershipAndPendingInvites()
	{
		ProtocolHost host{ "Factions Connect" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Connect Team"), 2.0, 101.0);
		Send(host, 10, Invite(second.playerId), 3.0, 102.0);
		host.HandleDisconnected(20, 103.0);

		const auto returning = Join(host, 21, "p20", 4.0, 104.0);
		const auto pending = RequirePacket(returning.hello, 21, "factionState");
		assert(pending.at("faction").is_null());
		assert(pending.at("pendingInvites").size() == 1);
		assert(pending.at("pendingInvites").at(0).at("factionName") == "Connect Team");
		assert(pending.at("pendingInvites").at(0).at("displayName") == "Stranger");
		assert(!pending.at("pendingInvites").at(0).contains("fromName"));
		assert(pending.at("pendingInvites").at(0).at("expiresInSeconds").get<std::int64_t>() == 58);

		const auto inviteId = pending.at("pendingInvites").at(0).at("inviteId").get<std::string>();
		AssertResult(Send(host, 21, Respond(inviteId, true), 5.0, 105.0), 21, "inviteResponse", true);

		host.HandleDisconnected(21, 106.0);
		const auto member = Join(host, 22, "p20", 6.0, 107.0);
		const auto membership = RequirePacket(member.hello, 22, "factionState").at("faction");
		assert(membership.at("name") == "Connect Team");
		assert(membership.at("members").size() == 2);
		assert(RequirePacket(member.hello, 22, "factionState").at("pendingInvites").empty());
	}

	void TestDisconnectPushesOfflineStateAndRoster()
	{
		ProtocolHost host{ "Factions Disconnect" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Offline Team"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(second.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(inviteId, true), 3.1, 102.1), 20, "inviteResponse", true);

		const auto frames = host.HandleDisconnected(20, 103.0);
		assert(HasPacket(frames, 10, "playerRoster"));
		bool offline = false;
		const auto state = RequirePacket(frames, 10, "factionState");
		for (const auto& member : state.at("faction").at("members")) {
			if (member.at("profileId") == "p20") offline = member.at("online") == false;
		}
		assert(offline);
	}

	void TestExpiredPendingInviteIsLeftOut()
	{
		ProtocolHost host{ "Factions Expired Pending" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Expired Team"), 2.0, 101.0);
		Send(host, 10, Invite(second.playerId), 3.0, 102.0);
		host.HandleDisconnected(20, 103.0);
		const auto returning = Join(host, 21, "p20", 4.0, 170.0);
		assert(RequirePacket(returning.hello, 21, "factionState").at("pendingInvites").empty());
	}

	void TestNameChangePushesRosterToEveryone()
	{
		ProtocolHost host{ "Factions Name Change" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		const auto frames = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 2.5, 100.5);
		assert(HasPacket(frames, 10, "playerRoster"));
		assert(HasPacket(frames, 20, "playerRoster"));
		bool sawStranger = false;
		for (const auto frame = RequirePacket(frames, 20, "playerRoster"); const auto& player : frame.at("players")) {
			sawStranger = sawStranger || (player.at("displayName") == "Stranger" && !player.contains("name"));
		}
		assert(sawStranger);
		assert(!HasPacket(Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 2.6, 100.6), 20, "playerRoster"));
	}

	void TestDisconnectWithoutHelloSendsNoRoster()
	{
		ProtocolHost host{ "Factions Unknown Disconnect" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		host.HandleConnected(30, 100.5);
		assert(!HasPacket(host.HandleDisconnected(30, 101.0), 10, "playerRoster"));
	}

	void TestWelcomeAdvertisesFactionsOnlyWhenEnabled()
	{
		ProtocolHost disabled{ "Welcome Disabled" };
		assert(!WelcomeHasCapability(disabled, 10, "factions-v1"));
		ProtocolHost enabled{ "Welcome Enabled" };
		assert(enabled.EnableFactions(nullptr));
		assert(WelcomeHasCapability(enabled, 10, "factions-v1"));
	}

	Json MaskedRowFor(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::uint32_t a_playerId)
	{
		const auto roster = RequirePacket(a_frames, a_connectionId, "playerRoster");
		for (const auto& player : roster.at("players")) if (player.at("playerId") == a_playerId) return player;
		assert(false && "expected roster row was not present");
		return {};
	}

	int RosterCount(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		int count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == "playerRoster") ++count;
		}
		return count;
	}

	void TestRapidNameChangesPushRosterAtMostOncePerSecond()
	{
		ProtocolHost host{ "Factions Name Throttle" };
		assert(host.EnableFactions(nullptr));
		const auto peer = Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		int pushes = 0;
		for (int step = 1; step <= 9; ++step) {
			const auto frames = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", step % 2 ? "Alpha" : "Bravo" } }, 1.0 + 0.1 * step, 100.5 + 0.1 * step);
			pushes += RosterCount(frames, 20);
		}
		assert(pushes == 0);
		const auto first = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Bravo" } }, 2.5, 101.5);
		assert(RosterCount(first, 20) == 1);
		assert(MaskedRowFor(first, 20, peer.playerId).at("displayName") == "Stranger" && !MaskedRowFor(first, 20, peer.playerId).contains("name"));
		pushes += RosterCount(first, 20);
		for (int step = 1; step <= 9; ++step) {
			const auto frames = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", step % 2 ? "Alpha" : "Bravo" } }, 2.5 + 0.1 * step, 101.5 + 0.1 * step);
			pushes += RosterCount(frames, 20);
		}
		assert(pushes == 1);
		const auto last = Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Final" } }, 3.6, 102.6);
		assert(RosterCount(last, 20) == 1);
		assert(MaskedRowFor(last, 20, peer.playerId).at("displayName") == "Stranger" && !MaskedRowFor(last, 20, peer.playerId).contains("name"));
	}

	void TestInviteWindowFollowsProfileAcrossReconnect()
	{
		ProtocolHost host{ "Factions Invite Window" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Send(host, 10, Create("Window Team"), 2.0, 101.0);
		std::vector<std::uint32_t> players;
		for (std::uint32_t connectionId = 11; connectionId <= 19; ++connectionId) {
			players.push_back(Join(host, connectionId, "p" + std::to_string(connectionId), 1.0, 100.0).playerId);
		}
		for (std::uint32_t index = 0; index < 8; ++index) {
			AssertResult(Send(host, 10, Invite(players[index]), 10.0 + index, 110.0 + index), 10, "invite", true);
		}
		host.HandleDisconnected(10, 118.0);
		Join(host, 60, "p10", 19.0, 119.0);
		AssertResult(Send(host, 60, Invite(players[8]), 19.5, 119.5), 60, "invite", false, "rateLimited");
	}

	void TestPendingInviteCapPerTarget()
	{
		ProtocolHost host{ "Factions Target Cap" };
		assert(host.EnableFactions(nullptr));
		for (std::uint32_t leader = 0; leader < 17; ++leader) {
			const auto connectionId = 10 + leader;
			Join(host, connectionId, "l" + std::to_string(leader), 1.0, 100.0);
			AssertResult(Send(host, connectionId, Create("Target Lead " + std::to_string(leader)), 2.0, 101.0), connectionId, "create", true);
		}
		const auto target = Join(host, 100, "t0", 1.0, 100.0);
		for (std::uint32_t leader = 0; leader < 16; ++leader) {
			const auto connectionId = 10 + leader;
			AssertResult(Send(host, connectionId, Invite(target.playerId), 10.0, 110.0), connectionId, "invite", true);
		}
		AssertResult(Send(host, 26, Invite(target.playerId), 10.0, 110.0), 26, "invite", false, "inviteCapacity");
	}

	void TestDisbandRequiresLeaderAndClearsMembers()
	{
		ProtocolHost host{ "Factions Disband" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Disband Team", "DSB"), 2.0, 101.0);
		Send(host, 10, Invite(member.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);

		AssertResult(Send(host, 20, Disband(), 4.0, 103.0), 20, "disband", false, "notLeader");
		AssertResult(Send(host, 40, Disband(), 4.1, 103.1), 40, "disband", false, "notLeader");

		const auto frames = Send(host, 10, Disband(), 4.2, 103.2);
		AssertResult(frames, 10, "disband", true);
		assert(RequirePacket(frames, 10, "factionState").at("faction").is_null());
		assert(RequirePacket(frames, 20, "factionState").at("faction").is_null());
		assert(!HasPacket(frames, 40, "factionState"));
		AssertResult(Send(host, 20, Leave(), 4.3, 103.3), 20, "leave", false, "notMember");
		AssertResult(Send(host, 10, Disband(), 5.0, 104.0), 10, "disband", false, "notLeader");
	}

	void TestDisbandClearsInvitesAndFreesNames()
	{
		ProtocolHost host{ "Factions Disband Invites" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto target = Join(host, 30, "p30", 1.2, 100.4);
		Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Invite Team", "INV"), 2.0, 101.0);
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(target.playerId), 3.0, 102.0), 30);
		const auto frames = Send(host, 10, Disband(), 4.0, 103.0);
		AssertResult(frames, 10, "disband", true);
		assert(RequirePacket(frames, 30, "factionState").at("pendingInvites").empty());
		assert(std::none_of(frames.begin(), frames.end(), [](const OutboundFrame& a_frame) { return a_frame.connectionId == 40; }));
		AssertResult(Send(host, 30, Respond(inviteId, true), 5.0, 104.0), 30, "inviteResponse", false, "inviteNotFound");
		AssertResult(Send(host, 10, Create("Invite Team", "INV"), 6.0, 105.0), 10, "create", true);
	}

	void TestPromoteMovesLeadership()
	{
		ProtocolHost host{ "Factions Promote" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto outsider = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Promote Team"), 2.0, 101.0);
		Send(host, 10, Invite(member.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);

		const auto frames = Send(host, 10, Promote("p20"), 4.0, 103.0);
		AssertResult(frames, 10, "promote", true);
		const auto leaderState = RequirePacket(frames, 10, "factionState").at("faction");
		assert(leaderState.at("leaderPlayerId") == member.playerId);
		assert(leaderState.at("members").at(0).at("leader") == false);
		assert(leaderState.at("members").at(1).at("leader") == true);
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("leaderPlayerId") == member.playerId);
		AssertResult(Send(host, 10, Invite(outsider.playerId), 4.5, 103.5), 10, "invite", false, "notLeader");
		AssertResult(Send(host, 20, Invite(outsider.playerId), 4.6, 103.6), 20, "invite", true);
	}

	void TestPromoteRejectsBadTargets()
	{
		ProtocolHost host{ "Factions Promote Rules" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Create("Rules Team"), 2.0, 101.0);
		Send(host, 10, Invite(member.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);

		AssertResult(Send(host, 10, Promote("p40"), 4.0, 103.0), 10, "promote", false, "invalidTarget");
		AssertResult(Send(host, 10, Promote("p10"), 4.1, 103.1), 10, "promote", false, "invalidTarget");
		AssertResult(Send(host, 10, Promote("p99"), 4.2, 103.2), 10, "promote", false, "invalidTarget");
		AssertResult(Send(host, 20, Promote("p20"), 4.3, 103.3), 20, "promote", false, "notLeader");
		AssertResult(Send(host, 20, Promote("p10"), 4.4, 103.4), 20, "promote", false, "notLeader");
		AssertResult(Send(host, 40, Promote("p20"), 4.5, 103.5), 40, "promote", false, "notLeader");
	}

	void TestLeaderLeavePrefersOnline()
	{
		ProtocolHost host{ "Factions Successor" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto first = Join(host, 20, "p20", 1.1, 100.2);
		const auto second = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Create("Successor Team"), 2.0, 101.0);
		Send(host, 10, Invite(first.playerId), 3.0, 102.0);
		Send(host, 20, Respond("i-1", true), 3.1, 102.1);
		Send(host, 10, Invite(second.playerId), 3.2, 102.2);
		Send(host, 30, Respond("i-2", true), 3.3, 102.3);
		host.HandleDisconnected(20, 103.0);

		const auto frames = Send(host, 10, Leave(), 4.0, 103.5);
		AssertResult(frames, 10, "leave", true);
		const auto successor = RequirePacket(frames, 30, "factionState").at("faction");
		assert(successor.at("leaderPlayerId") == second.playerId);
		assert(successor.at("members").at(0).at("online") == false);
		assert(successor.at("members").at(1).at("leader") == true);

		AssertResult(Send(host, 30, Leave(), 5.0, 104.0), 30, "leave", true);
		const auto returning = Join(host, 21, "p20", 6.0, 105.0);
		assert(RequirePacket(returning.hello, 21, "factionState").at("faction").at("leaderPlayerId") == returning.playerId);
	}

	void TestSameFactionHitIsDropped()
	{
		ProtocolHost host{ "Faction Friendly Fire Off" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto outsider = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		host.HandleMessage(30, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.3, 101.3);

		const auto before = host.Stats();
		assert(SendRangedHit(host, 10, member.playerId, 1, 2.5).empty());
		assert(host.Stats().combatHitsRejected == before.combatHitsRejected + 1);
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted);

		assert(HasPacket(SendRangedHit(host, 10, outsider.playerId, 2, 2.6), 30, "combatHit"));
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}

	void TestOtherFactionAndUnaffiliatedHitsRouted()
	{
		ProtocolHost host{ "Faction Other Factions" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		const auto rival = Join(host, 40, "p40", 1.3, 100.6);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		AssertResult(Send(host, 40, Create("Beta Team"), 2.0, 101.0), 40, "create", true);

		ArmRangedAttacker(host, 10, 40);
		assert(HasPacket(SendRangedHit(host, 10, rival.playerId, 1, 2.5), 40, "combatHit"));

		ArmRangedAttacker(host, 30, 20);
		assert(HasPacket(SendRangedHit(host, 30, member.playerId, 1, 2.5), 20, "combatHit"));
	}

	void TestSameFactionHitRoutedWhenFactionFriendlyFireOn()
	{
		ProtocolHost host{ "Faction Friendly Fire On", {}, {}, true, true };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		assert(HasPacket(SendRangedHit(host, 10, member.playerId, 1, 2.5), 20, "combatHit"));
		assert(host.Stats().combatHitsRouted == 1);
	}

	void TestWelcomeAdvertisesFactionFriendlyFire()
	{
		ProtocolHost off{ "Faction Friendly Fire Off" };
		assert(!DecodePacket(off.HandleConnected(10, 100.0).front()).at("factionFriendlyFire").get<bool>());
		ProtocolHost on{ "Faction Friendly Fire On", {}, {}, true, true };
		assert(DecodePacket(on.HandleConnected(10, 100.0).front()).at("factionFriendlyFire").get<bool>());
	}
}

int main()
{
	TestDisabledRejectsFactionPackets();
	TestCreateValidatesAndNamesAreUnique();
	TestCreateAnswersBadInput();
	TestCreateNeedsIdentity();
	TestInviteAcceptDeclineAndWrongTarget();
	TestAcceptRejectedWhileInAnotherFaction();
	TestAcceptRejectedWhenFactionFull();
	TestAcceptRejectedWhenFactionDeleted();
	TestExpiredInviteFails();
	TestLeaderLeavePromotesAndLastLeaveDeletes();
	TestPendingInviteSurvivesLeaderLeave();
	TestKickRules();
	TestKickRejectsUnknownMember();
	TestInviteLimits();
	TestInviteCapacity();
	TestMutationRateLimit();
	TestRosterAndRequestLimit();
	TestConnectShowsMembershipAndPendingInvites();
	TestDisconnectPushesOfflineStateAndRoster();
	TestDisconnectWithoutHelloSendsNoRoster();
	TestNameChangePushesRosterToEveryone();
	TestExpiredPendingInviteIsLeftOut();
	TestWelcomeAdvertisesFactionsOnlyWhenEnabled();
	TestRapidNameChangesPushRosterAtMostOncePerSecond();
	TestInviteWindowFollowsProfileAcrossReconnect();
	TestPendingInviteCapPerTarget();
	TestDisbandRequiresLeaderAndClearsMembers();
	TestDisbandClearsInvitesAndFreesNames();
	TestPromoteMovesLeadership();
	TestPromoteRejectsBadTargets();
	TestLeaderLeavePrefersOnline();
	TestSameFactionHitIsDropped();
	TestOtherFactionAndUnaffiliatedHitsRouted();
	TestSameFactionHitRoutedWhenFactionFriendlyFireOn();
	TestWelcomeAdvertisesFactionFriendlyFire();
	return 0;
}
