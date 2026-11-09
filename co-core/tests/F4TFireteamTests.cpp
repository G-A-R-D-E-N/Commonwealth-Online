#include "F4TFireteam.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <random>
#include <set>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using F4T::Fireteam::FireteamTable;
	using F4T::Fireteam::JoinRefusal;
	using F4T::Fireteam::kInviteTtlSeconds;
	using F4T::Fireteam::LeaveOutcome;

	JoinRefusal Refusal()
	{
		return JoinRefusal::None;
	}

	std::vector<std::uint32_t> Ids(std::initializer_list<std::uint32_t> a_values)
	{
		return std::vector<std::uint32_t>{ a_values };
	}

	void AssertInvariants(FireteamTable& a_table, const std::vector<std::uint32_t>& a_players)
	{
		for (const auto player : a_players) {
			const auto teamId = a_table.TeamIdOf(player);
			if (teamId.empty()) {
				assert(!a_table.IsMember(player));
				assert(!a_table.LeaderOf(player).has_value());
				assert(a_table.MembersOf(player).empty());
				continue;
			}
			assert(a_table.IsMember(player));
			const auto members = a_table.MembersOf(player);
			assert(!members.empty());
			assert(members.size() <= F4T::QuestProtocol::kMaximumFireteamMembers);
			assert(std::is_sorted(members.begin(), members.end()));
			assert(std::find(members.begin(), members.end(), player) != members.end());
			for (const auto member : members) {
				assert(a_table.TeamIdOf(member) == teamId);
			}
			const auto leader = a_table.LeaderOf(player);
			assert(leader.has_value());
			assert(std::find(members.begin(), members.end(), *leader) != members.end());
			assert(a_table.IsLeader(*leader));
			assert(a_table.LeaderOf(*leader) == leader);
		}
	}

	void RandomOperationsKeepTheTableConsistent()
	{
		const std::vector<std::uint32_t> players{ 1, 2, 3, 4, 5, 6 };
		std::mt19937 rng{ 20261101 };
		for (int run = 0; run < 40; ++run) {
			FireteamTable table;
			std::map<std::string, std::uint32_t> outstanding;
			double now = 0.0;
			for (int step = 0; step < 120; ++step) {
				now += 1.0;
				const auto pick = [&rng](std::size_t a_count) { return static_cast<std::size_t>(rng() % a_count); };
				const auto subject = players[pick(players.size())];
				switch (pick(7)) {
				case 0:
					(void)table.Create(subject);
					break;
				case 1: {
					std::string teamId;
					(void)table.Leave(subject, teamId);
					break;
				}
				case 2: {
					std::string teamId;
					(void)table.Kick(subject, players[pick(players.size())], teamId);
					break;
				}
				case 3: {
					JoinRefusal refusal{ JoinRefusal::None };
					const auto target = players[pick(players.size())];
					if (const auto inviteId = table.Invite(subject, target, now, refusal)) {
						outstanding[*inviteId] = target;
					}
					break;
				}
				case 4: {
					if (outstanding.empty()) {
						break;
					}
					auto entry = outstanding.begin();
					std::advance(entry, static_cast<long>(pick(outstanding.size())));
					JoinRefusal refusal{ JoinRefusal::None };
					const auto teamId = table.Accept(entry->first, entry->second, now, refusal);
					if (teamId || refusal == JoinRefusal::NoInvite) {
						outstanding.erase(entry);
					}
					break;
				}
				case 5: {
					if (outstanding.empty()) {
						break;
					}
					auto entry = outstanding.begin();
					std::advance(entry, static_cast<long>(pick(outstanding.size())));
					if (table.Decline(entry->first, entry->second)) {
						outstanding.erase(entry);
					}
					break;
				}
				default:
					table.ExpireInvites(now);
					break;
				}
				AssertInvariants(table, players);
			}
		}
	}
}

int main()
{
	{
		FireteamTable table;
		const auto teamId = table.Create(7);
		assert(teamId.has_value());
		assert(*teamId == "p-1");
		assert(table.TeamCount() == 1);
		assert(table.IsMember(7));
		assert(table.IsLeader(7));
		assert(!table.IsLeader(8));
		assert(table.TeamIdOf(7) == "p-1");
		assert(table.TeamIdOf(8).empty());
		assert(table.LeaderOf(7) == std::optional<std::uint32_t>{ 7 });
		assert(table.LeaderOf(8) == std::nullopt);
		assert(table.MembersOf(7) == Ids({ 7 }));
		assert(table.MembersOf(8).empty());
		assert(!table.Create(7).has_value());
		assert(!table.Create(0).has_value());
		assert(table.TeamCount() == 1);
	}

	{
		FireteamTable table;
		assert(table.Create(10).has_value());
		JoinRefusal refusal = Refusal();
		for (std::uint32_t target = 11; target <= 13; ++target) {
			const auto inviteId = table.Invite(10, target, 0.0, refusal);
			assert(refusal == JoinRefusal::None);
			assert(inviteId.has_value());
			const auto joined = table.Accept(*inviteId, target, 0.0, refusal);
			assert(refusal == JoinRefusal::None);
			assert(joined.has_value());
			assert(*joined == "p-1");
		}
		assert(table.MembersOf(10).size() == F4T::QuestProtocol::kMaximumFireteamMembers);
		assert(table.MembersOf(10) == Ids({ 10, 11, 12, 13 }));
		assert(!table.Invite(10, 14, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::Full);
		assert(!table.Invite(13, 14, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::Full);
	}

	{
		FireteamTable table;
		assert(table.Create(10).has_value());
		JoinRefusal refusal = Refusal();
		const auto pending = table.Invite(10, 14, 0.0, refusal);
		assert(pending.has_value());
		for (std::uint32_t target = 11; target <= 13; ++target) {
			const auto inviteId = table.Invite(10, target, 0.0, refusal);
			assert(inviteId.has_value());
			assert(table.Accept(*inviteId, target, 0.0, refusal).has_value());
		}
		assert(table.MembersOf(10).size() == F4T::QuestProtocol::kMaximumFireteamMembers);
		assert(!table.Accept(*pending, 14, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::Full);
		assert(!table.IsMember(14));
	}

	{
		FireteamTable table;
		assert(table.Create(1).has_value());
		assert(table.Create(2).has_value());
		assert(table.TeamCount() == 2);
		assert(table.TeamIdOf(1) == "p-1");
		assert(table.TeamIdOf(2) == "p-2");
		assert(!table.Create(1).has_value());
		JoinRefusal refusal = Refusal();
		assert(!table.Invite(3, 1, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::AlreadyMember);
		assert(!table.Invite(3, 2, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::AlreadyMember);
		assert(table.TeamCount() == 2);
	}

	{
		FireteamTable table;
		assert(table.Create(30).has_value());
		JoinRefusal refusal = Refusal();
		const auto second = table.Invite(30, 20, 0.0, refusal);
		assert(second.has_value());
		assert(table.Accept(*second, 20, 0.0, refusal).has_value());
		const auto third = table.Invite(20, 10, 0.0, refusal);
		assert(third.has_value());
		assert(table.Accept(*third, 10, 0.0, refusal).has_value());
		assert(table.MembersOf(30) == Ids({ 10, 20, 30 }));
		assert(table.IsLeader(30));
		std::string teamId;
		assert(table.Leave(30, teamId) == LeaveOutcome::LeftTeamRemains);
		assert(teamId == "p-1");
		assert(table.IsLeader(10));
		assert(!table.IsLeader(20));
		assert(table.LeaderOf(20) == std::optional<std::uint32_t>{ 10 });
		assert(table.MembersOf(20) == Ids({ 10, 20 }));
		assert(table.TeamIdOf(20) == "p-1");
		assert(table.TeamCount() == 1);
	}

	{
		FireteamTable table;
		assert(table.Create(4).has_value());
		JoinRefusal refusal = Refusal();
		const auto inviteId = table.Invite(4, 5, 0.0, refusal);
		assert(inviteId.has_value());
		assert(table.Accept(*inviteId, 5, 0.0, refusal).has_value());
		std::string teamId;
		assert(table.Kick(5, 4, teamId) == JoinRefusal::NotLeader);
		assert(teamId.empty());
		assert(table.IsMember(4));
		assert(table.Kick(4, 4, teamId) == JoinRefusal::SelfTarget);
		assert(table.Kick(4, 0, teamId) == JoinRefusal::UnknownTarget);
		assert(table.Kick(4, 9, teamId) == JoinRefusal::NotMember);
		assert(teamId.empty());
		assert(table.Kick(9, 4, teamId) == JoinRefusal::NotMember);
		assert(table.Kick(4, 5, teamId) == JoinRefusal::None);
		assert(teamId == "p-1");
		assert(!table.IsMember(5));
		assert(!table.IsLeader(5));
		assert(table.MembersOf(4) == Ids({ 4 }));
		assert(table.TeamCount() == 1);
	}

	{
		FireteamTable table;
		assert(table.Create(4).has_value());
		std::string teamId;
		assert(table.Leave(4, teamId) == LeaveOutcome::LeftTeamDisbanded);
		assert(teamId == "p-1");
		assert(table.TeamCount() == 0);
		assert(!table.IsMember(4));
		assert(table.Leave(4, teamId) == LeaveOutcome::NotMember);
		assert(teamId.empty());
		assert(table.TeamIdOf(4).empty());
		const auto again = table.Create(4);
		assert(again.has_value());
		assert(*again == "p-2");
	}

	{
		FireteamTable table;
		JoinRefusal refusal = Refusal();
		assert(!table.Invite(5, 5, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::SelfTarget);
		assert(!table.Invite(0, 5, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::UnknownTarget);
		assert(!table.Invite(5, 0, 0.0, refusal).has_value());
		assert(refusal == JoinRefusal::UnknownTarget);
		assert(table.InviteCount() == 0);
	}

	{
		FireteamTable table;
		JoinRefusal refusal = Refusal();
		const auto inviteId = table.Invite(7, 9, 100.0, refusal);
		assert(refusal == JoinRefusal::None);
		assert(inviteId.has_value());
		assert(*inviteId == "i-1");
		assert(table.InviteCount() == 1);
		assert(table.InviteParties(*inviteId) == std::make_pair(std::uint32_t{ 7 }, std::uint32_t{ 9 }));
		assert(table.InviteParties("i-99") == std::nullopt);
		assert(!table.Accept(*inviteId, 8, 100.0, refusal).has_value());
		assert(refusal == JoinRefusal::NoInvite);
		assert(table.InviteCount() == 1);
		assert(table.Decline(*inviteId, 9));
		assert(table.InviteCount() == 0);
		assert(!table.Decline(*inviteId, 9));
		assert(!table.Accept(*inviteId, 9, 100.0, refusal).has_value());
		assert(refusal == JoinRefusal::NoInvite);
		assert(!table.IsMember(7));
		assert(!table.IsMember(9));
		assert(table.TeamCount() == 0);
	}

	{
		FireteamTable table;
		JoinRefusal refusal = Refusal();
		const auto inviteId = table.Invite(7, 9, 0.0, refusal);
		assert(inviteId.has_value());
		const auto teamId = table.Accept(*inviteId, 9, 0.0, refusal);
		assert(refusal == JoinRefusal::None);
		assert(teamId.has_value());
		assert(*teamId == "p-1");
		assert(table.InviteCount() == 0);
		assert(table.IsLeader(7));
		assert(!table.IsLeader(9));
		assert(table.MembersOf(9) == Ids({ 7, 9 }));
		assert(table.TeamCount() == 1);
	}

	{
		FireteamTable table;
		JoinRefusal refusal = Refusal();
		const auto first = table.Invite(7, 9, 0.0, refusal);
		assert(first.has_value());
		assert(*first == "i-1");
		table.ExpireInvites(kInviteTtlSeconds - 1.0);
		assert(table.InviteCount() == 1);
		table.ExpireInvites(kInviteTtlSeconds);
		assert(table.InviteCount() == 0);
		assert(!table.Accept(*first, 9, 1.0, refusal).has_value());
		assert(refusal == JoinRefusal::NoInvite);
		const auto second = table.Invite(7, 9, 61.0, refusal);
		assert(second.has_value());
		assert(*second == "i-2");
		const auto refreshed = table.Invite(7, 9, 62.0, refusal);
		assert(refreshed.has_value());
		assert(*refreshed == *second);
		assert(table.InviteCount() == 1);
		table.ExpireInvites(121.0);
		assert(table.InviteCount() == 1);
		table.ExpireInvites(122.0);
		assert(table.InviteCount() == 0);
	}

	{
		FireteamTable table(10.0);
		JoinRefusal refusal = Refusal();
		assert(table.Invite(1, 2, 0.0, refusal).has_value());
		table.ExpireInvites(9.0);
		assert(table.InviteCount() == 1);
		table.ExpireInvites(10.0);
		assert(table.InviteCount() == 0);
	}

	{
		FireteamTable table;
		JoinRefusal refusal = Refusal();
		assert(table.Create(1).has_value());
		assert(table.Invite(1, 2, 0.0, refusal).has_value());
		assert(table.TeamCount() == 1);
		assert(table.InviteCount() == 1);
		table.Clear();
		assert(table.TeamCount() == 0);
		assert(table.InviteCount() == 0);
		assert(!table.IsMember(1));
		const auto created = table.Create(1);
		assert(created.has_value());
		assert(*created == "p-2");
	}

	RandomOperationsKeepTheTableConsistent();
	return 0;
}
