#include "F4TFireteamClient.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace F4T::FireteamClient
{
	namespace
	{
		std::mutex g_mutex;
		Team g_team;
		std::vector<Invite> g_invites;
		Event g_event;

		bool InTeam()
		{
			return !g_team.fireteamId.empty();
		}

		bool IsTeamMember(std::uint32_t a_playerId)
		{
			return std::any_of(g_team.members.begin(), g_team.members.end(), [a_playerId](const Member& a_member) {
				return a_member.playerId == a_playerId;
			});
		}

		void PruneInvites(std::uint64_t a_nowMilliseconds)
		{
			std::erase_if(g_invites, [a_nowMilliseconds](const Invite& a_invite) {
				return a_invite.receivedMilliseconds + kInviteLifetimeMilliseconds <= a_nowMilliseconds;
			});
		}

		std::optional<QuestWire::Json> Refuse(std::string_view a_reason)
		{
			g_event = Event{ EventKind::Refusal, std::string{ a_reason }, 0 };
			return std::nullopt;
		}

		std::optional<QuestWire::Json> Sent(QuestWire::Json a_packet)
		{
			g_event = Event{};
			return a_packet;
		}

		bool MembershipChanged(const QuestWire::FireteamSnapshotIn& a_snapshot)
		{
			return a_snapshot.fireteamId != g_team.fireteamId ||
				!std::equal(
					a_snapshot.members.begin(),
					a_snapshot.members.end(),
					g_team.members.begin(),
					g_team.members.end(),
					[](const QuestWire::FireteamMemberIn& a_incoming, const Member& a_current) {
						return a_incoming.playerId == a_current.playerId;
					});
		}

		bool GateRefused(const Context& a_context)
		{
			if (!a_context.supported) {
				Refuse(kReasonUnsupported);
				return true;
			}
			if (!a_context.connected) {
				Refuse(kReasonNotConnected);
				return true;
			}
			return false;
		}
	}

	void ApplySnapshot(const QuestWire::FireteamSnapshotIn& a_snapshot)
	{
		const std::scoped_lock lock(g_mutex);
		const bool wasInTeam = InTeam();
		if (MembershipChanged(a_snapshot)) {
			g_event = Event{};
		}
		g_team = Team{ a_snapshot.fireteamId, a_snapshot.leaderPlayerId, {} };
		for (const auto& member : a_snapshot.members) {
			g_team.members.push_back(Member{ member.playerId, member.characterName });
		}
		if (wasInTeam && !InTeam()) {
			g_event = Event{ EventKind::Removed, {}, 0 };
		}
	}

	void ApplyInvite(const QuestWire::FireteamInviteIn& a_invite, std::uint64_t a_nowMilliseconds)
	{
		const std::scoped_lock lock(g_mutex);
		if (a_invite.fromPlayerId == 0) {
			g_event = Event{ EventKind::Invited, {}, a_invite.targetPlayerId };
			return;
		}
		PruneInvites(a_nowMilliseconds);
		std::erase_if(g_invites, [&a_invite](const Invite& a_pending) { return a_pending.inviteId == a_invite.inviteId; });
		g_invites.push_back(Invite{ a_invite.inviteId, a_invite.fromPlayerId, a_invite.fromName, a_nowMilliseconds });
		if (g_invites.size() > kMaximumInvites) {
			g_invites.erase(g_invites.begin());
		}
	}

	void ApplyDecline(const QuestWire::FireteamDeclineIn& a_decline)
	{
		const std::scoped_lock lock(g_mutex);
		g_event = Event{ EventKind::Declined, {}, a_decline.fromPlayerId };
	}

	void ApplyEvent(const QuestWire::FireteamEventIn& a_event)
	{
		const std::scoped_lock lock(g_mutex);
		g_event = Event{ EventKind::Refusal, a_event.reason, 0 };
	}

	void ClearSession()
	{
		const std::scoped_lock lock(g_mutex);
		g_team = Team{};
		g_invites.clear();
		g_event = Event{};
	}

	View Current(std::uint64_t a_nowMilliseconds)
	{
		const std::scoped_lock lock(g_mutex);
		PruneInvites(a_nowMilliseconds);
		return View{ g_team, g_invites, g_event };
	}

	std::optional<QuestWire::Json> TryInvite(std::uint32_t a_target, const Context& a_context)
	{
		const std::scoped_lock lock(g_mutex);
		if (GateRefused(a_context)) {
			return std::nullopt;
		}
		if (a_target == 0) {
			return Refuse(QuestWire::kFireteamReasonInvalidTarget);
		}
		if (a_context.self && *a_context.self == a_target) {
			return Refuse(QuestWire::kFireteamReasonSelfTarget);
		}
		if (IsTeamMember(a_target)) {
			return Refuse(QuestWire::kFireteamReasonAlreadyMember);
		}
		if (g_team.members.size() >= QuestWire::kMaximumFireteamMembers) {
			return Refuse(QuestWire::kFireteamReasonFull);
		}
		return Sent(QuestWire::EncodeFireteamInvite(a_target));
	}

	std::optional<QuestWire::Json> TryRespond(
		std::string_view a_inviteId,
		bool a_accept,
		const Context& a_context,
		std::uint64_t a_nowMilliseconds)
	{
		const std::scoped_lock lock(g_mutex);
		PruneInvites(a_nowMilliseconds);
		if (GateRefused(a_context)) {
			return std::nullopt;
		}
		const auto invite = std::find_if(g_invites.begin(), g_invites.end(), [a_inviteId](const Invite& a_pending) {
			return a_pending.inviteId == a_inviteId;
		});
		if (invite == g_invites.end()) {
			return Refuse(QuestWire::kFireteamReasonNoInvite);
		}
		if (a_accept && InTeam()) {
			return Refuse(QuestWire::kFireteamReasonAlreadyMember);
		}
		g_invites.erase(invite);
		return Sent(QuestWire::EncodeFireteamRespond(a_inviteId, a_accept));
	}

	std::optional<QuestWire::Json> TryLeave(const Context& a_context)
	{
		const std::scoped_lock lock(g_mutex);
		if (GateRefused(a_context)) {
			return std::nullopt;
		}
		if (!InTeam()) {
			return Refuse(QuestWire::kFireteamReasonUnknownMember);
		}
		return Sent(QuestWire::EncodeFireteamLeave());
	}

	std::optional<QuestWire::Json> TryKick(std::uint32_t a_target, const Context& a_context)
	{
		const std::scoped_lock lock(g_mutex);
		if (GateRefused(a_context)) {
			return std::nullopt;
		}
		if (a_target == 0) {
			return Refuse(QuestWire::kFireteamReasonInvalidTarget);
		}
		if (a_context.self && *a_context.self == a_target) {
			return Refuse(QuestWire::kFireteamReasonSelfTarget);
		}
		if (!InTeam()) {
			return Refuse(QuestWire::kFireteamReasonUnknownMember);
		}
		if (!a_context.self || g_team.leaderPlayerId != *a_context.self) {
			return Refuse(QuestWire::kFireteamReasonNotLeader);
		}
		if (!IsTeamMember(a_target)) {
			return Refuse(QuestWire::kFireteamReasonUnknownMember);
		}
		return Sent(QuestWire::EncodeFireteamKick(a_target));
	}

	std::uint64_t MonotonicMilliseconds()
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}
}
