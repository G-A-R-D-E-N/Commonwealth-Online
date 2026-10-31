#include "ServerFactionTestSupport.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

using namespace FactionTestSupport;

namespace
{
	struct SaveFailureHarness
	{
		std::filesystem::path directory{ TempDirectory("faction-gap") };
		std::filesystem::path path{ directory / "durable-state.json" };
		DurableStateFileStore store{ path, [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; } };
		PersistenceGate gate{ store };
		ProtocolHost host{ "Factions Gap" };

		SaveFailureHarness()
		{
			assert(host.EnableFactions(&gate));
		}

		void FailSaves()
		{
			store.WaitForCommits();
			std::filesystem::remove_all(directory);
		}

		void RestoreSaves()
		{
			std::filesystem::create_directories(directory);
		}
	};

	std::size_t FramesTo(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return static_cast<std::size_t>(std::count_if(a_frames.begin(), a_frames.end(), [a_connectionId](const OutboundFrame& a_frame) {
			return a_frame.connectionId == a_connectionId;
		}));
	}

	void TestCreateSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		Join(harness.host, 10, "p10", 1.0, 100.0);
		const auto before = harness.host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(harness.host, 10, Create("Doomed Team", "DMD"), 2.0, 101.0), 10, "create", false, "persistenceUnavailable");
		assert(harness.host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(harness.host, 10, Create("Doomed Team", "DMD"), 4.0, 103.0), 10, "create", true);
		assert(harness.host.AdminListFactions().at("factions").at(0).at("factionId") == "f-1");
		std::filesystem::remove_all(harness.directory);
	}

	void TestInviteAcceptSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Accept Team");
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(invitee.playerId), 4.0, 104.0), 30);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 30, Respond(inviteId, true), 6.0, 106.0), 30, "inviteResponse", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 30, Respond(inviteId, true), 8.0, 108.0), 30, "inviteResponse", true);
		assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 3);
		std::filesystem::remove_all(harness.directory);
	}

	void TestLeaderLeaveSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Leave Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, Leave(), 6.0, 106.0), 10, "leave", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 10, Leave(), 8.0, 108.0), 10, "leave", true);
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("leaderProfileId") == "p20");
		assert(listed.at("officers").empty());
		std::filesystem::remove_all(harness.directory);
	}

	void TestKickSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Kick Restore Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 3.0, 103.0), 10, "setRank", true);
		const auto inviteId = InviteIdFrom(Send(host, 20, Invite(invitee.playerId), 3.5, 103.5), 30);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, Kick("p20"), 4.0, 104.0), 10, "kick", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 30, Respond(inviteId, true), 5.0, 105.0), 30, "inviteResponse", true);
		AssertResult(Send(host, 10, Kick("p20"), 6.0, 106.0), 10, "kick", true);
		assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 2);
		std::filesystem::remove_all(harness.directory);
	}

	void TestDisbandSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Disband Restore Team");
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(invitee.playerId), 4.0, 104.0), 30);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, Disband(), 6.0, 106.0), 10, "disband", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 30, Respond(inviteId, true), 7.0, 107.0), 30, "inviteResponse", true);
		assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 3);
		AssertResult(Send(host, 10, Disband(), 8.0, 108.0), 10, "disband", true);
		assert(host.AdminListFactions().at("factions").empty());
		std::filesystem::remove_all(harness.directory);
	}

	void TestPromoteSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Promote Restore Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, Promote("p20"), 6.0, 106.0), 10, "promote", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 10, Promote("p20"), 8.0, 108.0), 10, "promote", true);
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("leaderProfileId") == "p20");
		assert(listed.at("officers").empty());
		std::filesystem::remove_all(harness.directory);
	}

	void TestSetRankSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Rank Restore Team");
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 6.0, 106.0), 10, "setRank", true);
		assert(host.AdminListFactions().at("factions").at(0).at("officers") == Json::array({ "p20" }));
		std::filesystem::remove_all(harness.directory);
	}

	void TestSetProfileSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		Send(host, 10, Create("Profile Restore Team"), 2.0, 101.0);
		AssertResult(Send(host, 10, SetProfile("Old text", "#112233"), 4.0, 104.0), 10, "setProfile", true);
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		AssertResult(Send(host, 10, SetProfile("New text", "#445566"), 6.0, 106.0), 10, "setProfile", false, "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		AssertResult(Send(host, 10, SetProfile("New text", "#445566"), 8.0, 108.0), 10, "setProfile", true);
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("description") == "New text");
		assert(listed.at("color") == "#445566");
		std::filesystem::remove_all(harness.directory);
	}

	void TestAdminPromoteSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Admin Promote Restore");
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		std::string failure;
		assert(host.AdminPromoteFaction("f-1", "p20", 6.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		failure.clear();
		assert(!host.AdminPromoteFaction("f-1", "p20", 8.0, failure).empty() && failure.empty());
		assert(host.AdminListFactions().at("factions").at(0).at("leaderProfileId") == "p20");
		std::filesystem::remove_all(harness.directory);
	}

	void TestAdminKickSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Admin Kick Restore");
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		std::string failure;
		assert(host.AdminKickFaction("f-1", "p20", 6.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		failure.clear();
		assert(!host.AdminKickFaction("f-1", "p20", 8.0, failure).empty() && failure.empty());
		assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 1);
		std::filesystem::remove_all(harness.directory);
	}

	void TestAdminDisbandSaveFailureRestoresExactState()
	{
		SaveFailureHarness harness;
		auto& host = harness.host;
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Admin Disband Restore");
		assert(HasPacket(Send(host, 10, Invite(invitee.playerId), 4.0, 104.0), 30, "factionInviteReceived"));
		const auto before = host.AdminListFactions();
		harness.FailSaves();
		std::string failure;
		assert(host.AdminDisbandFaction("f-1", 6.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		assert(host.AdminListFactions() == before);
		harness.RestoreSaves();
		failure.clear();
		const auto frames = host.AdminDisbandFaction("f-1", 8.0, failure);
		assert(failure.empty());
		assert(RequirePacket(frames, 30, "factionState").at("faction").is_null());
		assert(host.AdminListFactions().at("factions").empty());
		std::filesystem::remove_all(harness.directory);
	}

	void TestAdminKickOfflineMemberSendsNoFrames()
	{
		ProtocolHost host{ "Factions Admin Offline Kick" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Offline Kick Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		assert(HasPacket(Send(host, 20, Invite(invitee.playerId), 6.0, 106.0), 30, "factionInviteReceived"));
		host.HandleDisconnected(20, 107.0);

		std::string failure;
		const auto frames = host.AdminKickFaction("f-1", "p20", 8.0, failure);
		assert(failure.empty());
		assert(FramesTo(frames, 20) == 0);
		assert(RequirePacket(frames, 10, "factionState").at("faction").at("members").size() == 1);
		assert(RequirePacket(frames, 30, "factionState").at("pendingInvites").empty());
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("memberCount") == 1);
		assert(listed.at("officers").empty());

		const auto returning = Join(host, 40, "p20", 9.0, 109.0);
		assert(RequirePacket(returning.hello, 40, "factionState").at("faction").is_null());
		AssertResult(Send(host, 40, Create("Freed Team"), 11.0, 111.0), 40, "create", true);
	}

	void TestAdminPromoteOfflineMemberUpdatesState()
	{
		ProtocolHost host{ "Factions Admin Offline Heir" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Offline Heir");
		host.HandleDisconnected(20, 105.0);

		std::string failure;
		const auto frames = host.AdminPromoteFaction("f-1", "p20", 6.0, failure);
		assert(failure.empty());
		assert(FramesTo(frames, 20) == 0);
		const auto state = RequirePacket(frames, 10, "factionState").at("faction");
		assert(!state.contains("leaderPlayerId"));
		assert(state.at("members").at(1).at("leader") == true);
		assert(state.at("members").at(1).at("online") == false);
		assert(host.AdminListFactions().at("factions").at(0).at("leaderProfileId") == "p20");

		const auto returning = Join(host, 30, "p20", 7.0, 207.0);
		assert(RequirePacket(returning.hello, 30, "factionState").at("faction").at("leaderPlayerId") == returning.playerId);
	}

	void TestAdminPromoteWithOfflineLeaderSendsOnlyToOnline()
	{
		ProtocolHost host{ "Factions Admin Offline Leader" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto third = Join(host, 30, "p30", 1.2, 100.4);
		Join(host, 40, "p40", 1.3, 100.6);
		FormFaction(host, 10, 20, member.playerId, "Leaderless Team");
		const auto inviteId = InviteIdFrom(Send(host, 10, Invite(third.playerId), 4.0, 104.0), 30);
		AssertResult(Send(host, 30, Respond(inviteId, true), 4.1, 104.1), 30, "inviteResponse", true);
		host.HandleDisconnected(10, 105.0);

		std::string failure;
		const auto frames = host.AdminPromoteFaction("f-1", "p20", 6.0, failure);
		assert(failure.empty());
		assert(FramesTo(frames, 10) == 0);
		assert(FramesTo(frames, 40) == 0);
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("leaderPlayerId") == member.playerId);
		assert(RequirePacket(frames, 30, "factionState").at("faction").at("leaderPlayerId") == member.playerId);

		const auto returning = Join(host, 50, "p10", 7.0, 207.0);
		const auto state = RequirePacket(returning.hello, 50, "factionState").at("faction");
		assert(state.at("leaderPlayerId") == member.playerId);
		assert(state.at("members").at(0).at("leader") == false);
	}

	void TestAdminDisbandWithOfflineLeaderSendsOnlyToOnline()
	{
		ProtocolHost host{ "Factions Admin Offline Disband" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Offline Disband Team");
		assert(HasPacket(Send(host, 10, Invite(invitee.playerId), 4.0, 104.0), 30, "factionInviteReceived"));
		host.HandleDisconnected(10, 105.0);

		std::string failure;
		const auto frames = host.AdminDisbandFaction("f-1", 6.0, failure);
		assert(failure.empty());
		assert(FramesTo(frames, 10) == 0);
		assert(RequirePacket(frames, 20, "factionState").at("faction").is_null());
		assert(RequirePacket(frames, 30, "factionState").at("faction").is_null());
		assert(host.AdminListFactions().at("factions").empty());

		const auto returning = Join(host, 40, "p10", 7.0, 207.0);
		assert(RequirePacket(returning.hello, 40, "factionState").at("faction").is_null());
		AssertResult(Send(host, 40, Create("Heir Team"), 9.0, 209.0), 40, "create", true);
	}

	void TestAdminOpsRefuseWhenFactionsDisabled()
	{
		ProtocolHost host{ "Factions Admin Disabled" };
		std::string failure;
		assert(host.AdminDisbandFaction("f-1", 1.0, failure).empty() && failure == "factionsDisabled");
		failure.clear();
		assert(host.AdminPromoteFaction("f-1", "p20", 1.0, failure).empty() && failure == "factionsDisabled");
		failure.clear();
		assert(host.AdminKickFaction("f-1", "p20", 1.0, failure).empty() && failure == "factionsDisabled");
		assert(host.AdminListFactions().at("enabled") == false);
		assert(host.AdminListFactions().at("factions").empty());
	}

	Json MemberIn(const Json& a_factionState, std::string_view a_profileId)
	{
		for (const auto& member : a_factionState.at("faction").at("members")) {
			if (member.at("profileId") == std::string{ a_profileId }) return member;
		}
		assert(false && "member missing from factionState");
		return Json{};
	}

	Json Vitals(std::int64_t a_healthPercent, bool a_downed = false, bool a_dead = false)
	{
		return Json{
			{ "type", "playerState" },
			{ "vitals", Json{ { "healthPercent", a_healthPercent }, { "isDowned", a_downed }, { "isDead", a_dead } } }
		};
	}

	void TestMemberHealthOnRoster()
	{
		ProtocolHost host{ "Factions Roster Health" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Health Team");

		const auto beforeVitals = Send(host, 10, SetRank("p20", "officer"), 3.0, 103.0);
		assert(!MemberIn(RequirePacket(beforeVitals, 10, "factionState"), "p20").contains("health"));

		auto frames = Send(host, 20, Vitals(74), 4.0, 104.0);
		const auto first = MemberIn(RequirePacket(frames, 10, "factionState"), "p20");
		assert(first.at("health") == 74);
		assert(first.at("downed") == false);
		assert(first.at("dead") == false);
		assert(HasPacket(frames, 20, "factionState"));

		assert(!HasPacket(Send(host, 20, Vitals(76), 4.4, 104.4), 10, "factionState"));

		frames = Send(host, 20, Vitals(12, true), 4.5, 104.5);
		const auto downed = MemberIn(RequirePacket(frames, 10, "factionState"), "p20");
		assert(downed.at("health") == 12);
		assert(downed.at("downed") == true);

		assert(HasPacket(Send(host, 20, Vitals(60), 6.0, 106.0), 10, "factionState"));
		assert(MemberIn(RequirePacket(Send(host, 20, Vitals(40), 8.0, 108.0), 10, "factionState"), "p20").at("health") == 40);

		host.HandleDisconnected(20, 109.0);
		const auto offline = MemberIn(RequirePacket(Send(host, 10, Vitals(90), 10.0, 110.0), 10, "factionState"), "p20");
		assert(offline.at("online") == false);
		assert(!offline.contains("health"));
		assert(!offline.contains("downed"));
	}

	void TestInviteRejectsUnavailableTargets()
	{
		ProtocolHost host{ "Factions Unavailable Target" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Create("Quiet Team"), 2.0, 101.0);
		AssertResult(Send(host, 10, Invite(9999), 3.0, 102.0), 10, "invite", false, "targetUnavailable");
		host.HandleDisconnected(20, 103.0);
		AssertResult(Send(host, 10, Invite(second.playerId), 4.0, 104.0), 10, "invite", false, "targetUnavailable");
		const auto unbound = Join(host, 30, "", 5.0, 105.0);
		AssertResult(Send(host, 10, Invite(unbound.playerId), 6.0, 106.0), 10, "invite", false, "targetUnavailable");
	}

	void TestLoadLogsDuplicateMemberInOneFaction()
	{
		const auto directory = TempDirectory("duplicate-member");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{
				{ "nextFactionNumber", 2 },
				{ "factions", Json{
					{ "f-1", Stored("Alpha Team", "ALP", "p1", Json::array({ "p1", "p1" }), Json{ { "p1", "One" } }) }
				} }
			});
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Duplicate Member" };
		std::ostringstream log;
		auto* previous = std::cerr.rdbuf(log.rdbuf());
		assert(host.EnableFactions(&gate));
		std::cerr.rdbuf(previous);
		assert(log.str().find("Faction f-1 dropped duplicate member p1.") != std::string::npos);
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("members").size() == 1);
		assert(listed.at("members").at(0) == "p1");
		std::filesystem::remove_all(directory);
		std::printf("faction duplicate member logged p1\n");
	}

	void TestMemberRenamePersistsWithoutLaterMutation()
	{
		const auto directory = TempDirectory("rename-persist");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Rename" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			FormFaction(host, 10, 20, member.playerId, "Rename Team");
			Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Renamed" } }, 4.0, 104.0);
			host.HandleDisconnected(20, 105.0);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Rename Reload" };
			assert(host.EnableFactions(&gate));
			const auto names = host.AdminListFactions().at("factions").at(0).at("memberNames");
			assert(names.at("p10") == "Renamed");
		}
		std::filesystem::remove_all(directory);
		std::printf("faction member name persisted Renamed\n");
	}
}

int main()
{
	TestCreateSaveFailureRestoresExactState();
	TestInviteAcceptSaveFailureRestoresExactState();
	TestLeaderLeaveSaveFailureRestoresExactState();
	TestKickSaveFailureRestoresExactState();
	TestDisbandSaveFailureRestoresExactState();
	TestPromoteSaveFailureRestoresExactState();
	TestSetRankSaveFailureRestoresExactState();
	TestSetProfileSaveFailureRestoresExactState();
	TestAdminPromoteSaveFailureRestoresExactState();
	TestAdminKickSaveFailureRestoresExactState();
	TestAdminDisbandSaveFailureRestoresExactState();
	TestInviteRejectsUnavailableTargets();
	TestAdminKickOfflineMemberSendsNoFrames();
	TestAdminPromoteOfflineMemberUpdatesState();
	TestAdminPromoteWithOfflineLeaderSendsOnlyToOnline();
	TestAdminDisbandWithOfflineLeaderSendsOnlyToOnline();
	TestAdminOpsRefuseWhenFactionsDisabled();
	TestMemberHealthOnRoster();
	TestLoadLogsDuplicateMemberInOneFaction();
	TestMemberRenamePersistsWithoutLaterMutation();
	return 0;
}
