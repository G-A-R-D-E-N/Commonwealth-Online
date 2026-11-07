#include "F4TFireteamClient.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::FireteamClient::Context;
	using F4T::FireteamClient::EventKind;
	using F4T::QuestWire::FireteamDeclineIn;
	using F4T::QuestWire::FireteamEventIn;
	using F4T::QuestWire::FireteamInviteIn;
	using F4T::QuestWire::FireteamSnapshotIn;

	Context Ready(std::uint32_t a_self)
	{
		return Context{ true, true, a_self };
	}

	FireteamSnapshotIn Snapshot(std::uint32_t a_leader, const std::vector<std::uint32_t>& a_members)
	{
		FireteamSnapshotIn snapshot;
		snapshot.fireteamId = "p-1";
		snapshot.leaderPlayerId = a_leader;
		for (const auto playerId : a_members) {
			snapshot.members.push_back(F4T::QuestWire::FireteamMemberIn{ playerId, "Member" });
		}
		return snapshot;
	}

	FireteamInviteIn Invite(std::string_view a_inviteId, std::uint32_t a_from)
	{
		return FireteamInviteIn{ std::string{ a_inviteId }, a_from, "Ana", 0 };
	}

	void TestUnsupportedAndDisconnectedRefuseBeforeTheTable()
	{
		F4T::FireteamClient::ClearSession();
		assert(!F4T::FireteamClient::TryInvite(9, Context{ false, true, 3u }));
		assert(F4T::FireteamClient::Current(0).event.reason == "unsupported");
		assert(!F4T::FireteamClient::TryRespond("inv-1", true, Context{ false, true, 3u }, 0));
		assert(F4T::FireteamClient::Current(0).event.reason == "unsupported");
		assert(!F4T::FireteamClient::TryLeave(Context{ true, false, 3u }));
		assert(F4T::FireteamClient::Current(0).event.reason == "not_connected");
		assert(!F4T::FireteamClient::TryKick(7, Context{ true, false, 3u }));
		assert(F4T::FireteamClient::Current(0).event.reason == "not_connected");
	}

	void TestInviteRefusesBadTargetsAndAFullTeam()
	{
		F4T::FireteamClient::ClearSession();
		const auto context = Ready(3);
		assert(!F4T::FireteamClient::TryInvite(0, context));
		assert(F4T::FireteamClient::Current(0).event.reason == "invalid_target");
		assert(!F4T::FireteamClient::TryInvite(3, context));
		assert(F4T::FireteamClient::Current(0).event.reason == "self_target");

		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		assert(!F4T::FireteamClient::TryInvite(7, context));
		assert(F4T::FireteamClient::Current(0).event.reason == "already_member");

		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7, 8, 9 }));
		assert(!F4T::FireteamClient::TryInvite(11, context));
		assert(F4T::FireteamClient::Current(0).event.reason == "full");

		F4T::FireteamClient::ClearSession();
		const auto invite = F4T::FireteamClient::TryInvite(7, context);
		assert(invite);
		assert(invite->at("type") == "fireteamInvite");
		assert(invite->at("targetPlayerId") == 7);
	}

	void TestRespondNeedsALiveInviteAndNoTeam()
	{
		F4T::FireteamClient::ClearSession();
		const auto context = Ready(7);
		assert(!F4T::FireteamClient::TryRespond("inv-1", true, context, 1000));
		assert(F4T::FireteamClient::Current(1000).event.reason == "no_invite");

		F4T::FireteamClient::ApplyInvite(Invite("inv-1", 3), 1000);
		const auto decline = F4T::FireteamClient::TryRespond("inv-1", false, context, 1500);
		assert(decline);
		assert(decline->at("type") == "fireteamRespond");
		assert(decline->at("inviteId") == "inv-1");
		assert(decline->at("accept") == false);
		assert(F4T::FireteamClient::Current(1500).invites.empty());

		F4T::FireteamClient::ApplyInvite(Invite("inv-2", 3), 1600);
		const auto accept = F4T::FireteamClient::TryRespond("inv-2", true, context, 1600);
		assert(accept);
		assert(accept->at("accept") == true);
		assert(F4T::FireteamClient::Current(1600).invites.empty());

		F4T::FireteamClient::ApplyInvite(Invite("inv-3", 3), 1700);
		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		assert(!F4T::FireteamClient::TryRespond("inv-3", true, context, 1700));
		assert(F4T::FireteamClient::Current(1700).event.reason == "already_member");
		assert(F4T::FireteamClient::Current(1700).invites.size() == 1);
	}

	void TestLeaveNeedsATeamAndKickNeedsTheLeader()
	{
		F4T::FireteamClient::ClearSession();
		assert(!F4T::FireteamClient::TryLeave(Ready(7)));
		assert(F4T::FireteamClient::Current(0).event.reason == "unknown_member");

		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7, 8 }));
		const auto leave = F4T::FireteamClient::TryLeave(Ready(7));
		assert(leave);
		assert(leave->at("type") == "fireteamLeave");

		assert(!F4T::FireteamClient::TryKick(8, Ready(7)));
		assert(F4T::FireteamClient::Current(0).event.reason == "not_leader");
		assert(!F4T::FireteamClient::TryKick(3, Ready(3)));
		assert(F4T::FireteamClient::Current(0).event.reason == "self_target");
		assert(!F4T::FireteamClient::TryKick(42, Ready(3)));
		assert(F4T::FireteamClient::Current(0).event.reason == "unknown_member");

		const auto kick = F4T::FireteamClient::TryKick(8, Ready(3));
		assert(kick);
		assert(kick->at("type") == "fireteamKick");
		assert(kick->at("targetPlayerId") == 8);
	}

	void TestLeavingTheTeamRaisesRemoved()
	{
		F4T::FireteamClient::ClearSession();
		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		assert(F4T::FireteamClient::Current(0).team.members.size() == 2);
		F4T::FireteamClient::ApplySnapshot(FireteamSnapshotIn{});
		const auto view = F4T::FireteamClient::Current(0);
		assert(view.team.fireteamId.empty());
		assert(view.event.kind == EventKind::Removed);
	}

	void TestIncomingInviteExpiresAtSixtySeconds()
	{
		F4T::FireteamClient::ClearSession();
		F4T::FireteamClient::ApplyInvite(Invite("inv-4", 3), 1000);
		assert(F4T::FireteamClient::Current(60'999).invites.size() == 1);
		assert(F4T::FireteamClient::Current(61'000).invites.empty());
	}

	void TestEchoDeclineAndServerRefusalSetTheEvent()
	{
		F4T::FireteamClient::ClearSession();
		F4T::FireteamClient::ApplyInvite(FireteamInviteIn{ "inv-5", 0, "", 9 }, 1000);
		auto view = F4T::FireteamClient::Current(1000);
		assert(view.invites.empty());
		assert(view.event.kind == EventKind::Invited);
		assert(view.event.playerId == 9);

		F4T::FireteamClient::ApplyDecline(FireteamDeclineIn{ "inv-5", 9 });
		view = F4T::FireteamClient::Current(1000);
		assert(view.event.kind == EventKind::Declined);
		assert(view.event.playerId == 9);

		F4T::FireteamClient::ApplyEvent(FireteamEventIn{ "full" });
		view = F4T::FireteamClient::Current(1000);
		assert(view.event.kind == EventKind::Refusal);
		assert(view.event.reason == "full");
	}

	void TestPendingInvitesAreCappedAndReplacedById()
	{
		F4T::FireteamClient::ClearSession();
		F4T::FireteamClient::ApplyInvite(Invite("inv-6", 3), 1000);
		F4T::FireteamClient::ApplyInvite(Invite("inv-6", 3), 2000);
		const auto refreshed = F4T::FireteamClient::Current(2000);
		assert(refreshed.invites.size() == 1);
		assert(refreshed.invites.front().receivedMilliseconds == 2000);

		F4T::FireteamClient::ClearSession();
		for (std::size_t i = 0; i <= F4T::FireteamClient::kMaximumInvites; ++i) {
			F4T::FireteamClient::ApplyInvite(Invite("inv-" + std::to_string(i), 3), 1000);
		}
		const auto capped = F4T::FireteamClient::Current(1000);
		assert(capped.invites.size() == F4T::FireteamClient::kMaximumInvites);
		assert(capped.invites.front().inviteId == "inv-1");
	}

	void TestClearSessionDropsTheTeamInvitesAndEvent()
	{
		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		F4T::FireteamClient::ApplyInvite(Invite("inv-7", 3), 1000);
		F4T::FireteamClient::ApplyEvent(FireteamEventIn{ "full" });
		F4T::FireteamClient::ClearSession();
		const auto view = F4T::FireteamClient::Current(1000);
		assert(view.team.fireteamId.empty());
		assert(view.team.members.empty());
		assert(view.invites.empty());
		assert(view.event.kind == EventKind::None);
	}

	void TestASentActionOrAMembershipChangeClearsTheLastEvent()
	{
		F4T::FireteamClient::ClearSession();
		const auto context = Ready(3);
		assert(!F4T::FireteamClient::TryInvite(0, context));
		assert(F4T::FireteamClient::Current(0).event.kind == EventKind::Refusal);
		assert(F4T::FireteamClient::TryInvite(7, context));
		assert(F4T::FireteamClient::Current(0).event.kind == EventKind::None);

		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		F4T::FireteamClient::ApplyEvent(FireteamEventIn{ "full" });
		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7 }));
		assert(F4T::FireteamClient::Current(0).event.reason == "full");
		F4T::FireteamClient::ApplySnapshot(Snapshot(3, { 3, 7, 8 }));
		assert(F4T::FireteamClient::Current(0).event.kind == EventKind::None);

		F4T::FireteamClient::ApplyEvent(FireteamEventIn{ "not_leader" });
		assert(F4T::FireteamClient::TryLeave(Ready(7)));
		assert(F4T::FireteamClient::Current(0).event.kind == EventKind::None);
	}
}

int main()
{
	TestUnsupportedAndDisconnectedRefuseBeforeTheTable();
	TestInviteRefusesBadTargetsAndAFullTeam();
	TestRespondNeedsALiveInviteAndNoTeam();
	TestLeaveNeedsATeamAndKickNeedsTheLeader();
	TestLeavingTheTeamRaisesRemoved();
	TestIncomingInviteExpiresAtSixtySeconds();
	TestEchoDeclineAndServerRefusalSetTheEvent();
	TestPendingInvitesAreCappedAndReplacedById();
	TestClearSessionDropsTheTeamInvitesAndEvent();
	TestASentActionOrAMembershipChangeClearsTheLastEvent();
	return 0;
}
