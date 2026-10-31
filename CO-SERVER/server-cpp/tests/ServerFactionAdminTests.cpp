#include "ServerFactionAdminAction.h"
#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	void TestAdminListCarriesRosterFields()
	{
		ProtocolHost host{ "Factions Admin List" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Leader" } }, 1.5, 100.5);
		FormFaction(host, 10, 20, member.playerId, "Listed Team");
		AssertResult(Send(host, 10, SetProfile("Scouts only", "#3366ff"), 3.0, 103.0), 10, "setProfile", true);
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("factionId") == "f-1");
		assert(listed.at("description") == "Scouts only");
		assert(listed.at("color") == "#3366FF");
		assert(listed.at("createdAt") == 101.0);
		assert(listed.at("memberNames").at("p10") == "Leader");
		assert(listed.at("officers").empty());
		assert(listed.at("memberCount") == 2);
	}

	void TestAdminPromoteHandsLeadership()
	{
		ProtocolHost host{ "Factions Admin Promote" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Promote Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		std::string failure;
		assert(host.AdminPromoteFaction("f-99", "p20", 4.1, failure).empty() && failure == "factionNotFound");
		failure.clear();
		assert(host.AdminPromoteFaction("f-1", "p30", 4.2, failure).empty() && failure == "invalidTarget");
		failure.clear();
		assert(host.AdminPromoteFaction("f-1", "p10", 4.3, failure).empty() && failure == "alreadyLeader");
		failure.clear();
		const auto same = CommonwealthOnline::Server::RunFactionAdminAction(host, CommonwealthOnline::Server::FactionAdminOp::Promote, "f-1", "p10", 4.35, [](std::vector<OutboundFrame>&) {});
		assert(!same.ok && same.message == "Profile p10 already leads faction f-1.");
		const auto frames = host.AdminPromoteFaction("f-1", "p20", 4.4, failure);
		assert(failure.empty());
		assert(RequirePacket(frames, 10, "factionState").at("faction").at("leaderPlayerId") == member.playerId);
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("leaderPlayerId") == member.playerId);
		assert(!HasPacket(frames, 30, "factionState"));
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("leaderProfileId") == "p20");
		assert(listed.at("officers").empty());
	}

	void TestAdminPromoteRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("admin-promote-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Admin Promote Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Keep Leader");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		std::string failure;
		assert(host.AdminPromoteFaction("f-1", "p20", 5.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("leaderProfileId") == "p10");
		assert(listed.at("officers") == Json::array({ "p20" }));
		AssertResult(Send(host, 10, Promote("p20"), 6.0, 106.0), 10, "promote", false, "persistenceUnavailable");
	}

	void TestAdminPromotePersistsAcrossReload()
	{
		const auto directory = TempDirectory("admin-promote-reload");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Admin Promote Persist" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			FormFaction(host, 10, 20, member.playerId, "Heir Team");
			std::string failure;
			assert(!host.AdminPromoteFaction("f-1", "p20", 4.0, failure).empty() && failure.empty());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Admin Promote Reload" };
			assert(host.EnableFactions(&gate));
			assert(host.AdminListFactions().at("factions").at(0).at("leaderProfileId") == "p20");
			const auto leader = Join(host, 20, "p20", 1.0, 200.0);
			const auto returning = Join(host, 30, "p10", 1.1, 200.2);
			const auto state = RequirePacket(returning.hello, 30, "factionState").at("faction");
			assert(state.at("leaderPlayerId") == leader.playerId);
		}
		std::filesystem::remove_all(directory);
	}

	void TestAdminKickRemovesMemberAndTheirInvites()
	{
		ProtocolHost host{ "Factions Admin Kick" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Kick Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		assert(HasPacket(Send(host, 20, Invite(invitee.playerId), 4.1, 104.1), 30, "factionInviteReceived"));
		std::string failure;
		const auto frames = host.AdminKickFaction("f-1", "p20", 5.0, failure);
		assert(failure.empty());
		assert(RequirePacket(frames, 10, "factionState").at("faction").at("members").size() == 1);
		assert(RequirePacket(frames, 30, "factionState").at("pendingInvites").empty());
		AssertResult(frames, 20, "kick", false, "kicked");
		assert(RequirePacket(frames, 20, "factionState").at("faction").is_null());
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("memberCount") == 1);
		assert(!listed.at("memberNames").contains("p20"));
		assert(listed.at("officers").empty());
	}

	void TestAdminKickSkipsExpiredInviteNotice()
	{
		ProtocolHost host{ "Factions Admin Kick Expired" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, member.playerId, "Expiry Team");
		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 104.0), 10, "setRank", true);
		assert(HasPacket(Send(host, 20, Invite(invitee.playerId), 4.1, 104.1), 30, "factionInviteReceived"));
		std::string failure;
		const auto frames = host.AdminKickFaction("f-1", "p20", 200.0, failure);
		assert(failure.empty());
		assert(!HasPacket(frames, 30, "factionState"));
	}

	void TestAdminKickRefusesLeaderAndUnknownTargets()
	{
		ProtocolHost host{ "Factions Admin Kick Refusals" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Refuse Team");
		std::string failure;
		assert(host.AdminKickFaction("f-1", "p10", 4.0, failure).empty() && failure == "leaderKick");
		failure.clear();
		assert(host.AdminKickFaction("f-1", "p99", 4.1, failure).empty() && failure == "invalidTarget");
		failure.clear();
		assert(host.AdminKickFaction("f-9", "p20", 4.2, failure).empty() && failure == "factionNotFound");
		assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 2);
	}

	void TestAdminKickRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("admin-kick-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Admin Kick Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Stay Team");
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		std::string failure;
		assert(host.AdminKickFaction("f-1", "p20", 5.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		const auto listed = host.AdminListFactions().at("factions").at(0);
		assert(listed.at("memberCount") == 2);
		assert(listed.at("memberNames").contains("p20"));
		AssertResult(Send(host, 20, Leave(), 6.0, 106.0), 20, "leave", false, "persistenceUnavailable");
	}

	void TestAdminActionMessagesWhenDisabled()
	{
		using CommonwealthOnline::Server::FactionAdminOp;
		using CommonwealthOnline::Server::RunFactionAdminAction;
		const auto send = [](std::vector<OutboundFrame>&) {};
		ProtocolHost disabled{ "Factions Admin Message Disabled" };
		const auto disband = RunFactionAdminAction(disabled, FactionAdminOp::Disband, "f-1", "", 1.0, send);
		assert(!disband.ok && disband.message == "Factions are not enabled on this server.");
		ProtocolHost enabled{ "Factions Admin Message Enabled" };
		assert(enabled.EnableFactions(nullptr));
		const auto missing = RunFactionAdminAction(enabled, FactionAdminOp::Kick, "f-9", "p20", 1.0, send);
		assert(!missing.ok && missing.message == "No faction with id f-9.");
	}
}

int main()
{
	TestAdminListCarriesRosterFields();
	TestAdminPromoteHandsLeadership();
	TestAdminPromoteRollsBackOnSaveFailure();
	TestAdminPromotePersistsAcrossReload();
	TestAdminKickRemovesMemberAndTheirInvites();
	TestAdminKickSkipsExpiredInviteNotice();
	TestAdminKickRefusesLeaderAndUnknownTargets();
	TestAdminKickRollsBackOnSaveFailure();
	TestAdminActionMessagesWhenDisabled();
	return 0;
}
