#include "ServerFireteam.h"

#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using F4T::Fireteam::JoinRefusal;
		using F4T::QuestProtocol::FireteamInvite;
		using F4T::QuestProtocol::FireteamMember;
		using F4T::QuestProtocol::FireteamRespond;
		using F4T::QuestProtocol::FireteamSnapshot;
	}

	std::string_view FireteamService::ReasonName(JoinRefusal a_refusal)
	{
		switch (a_refusal) {
		case JoinRefusal::Full: return F4T::QuestProtocol::kFireteamReasonFull;
		case JoinRefusal::AlreadyMember: return F4T::QuestProtocol::kFireteamReasonAlreadyMember;
		case JoinRefusal::SelfTarget: return F4T::QuestProtocol::kFireteamReasonSelfTarget;
		case JoinRefusal::UnknownTarget: return F4T::QuestProtocol::kFireteamReasonInvalidTarget;
		case JoinRefusal::NoInvite: return F4T::QuestProtocol::kFireteamReasonNoInvite;
		case JoinRefusal::NotLeader: return F4T::QuestProtocol::kFireteamReasonNotLeader;
		case JoinRefusal::NotMember: return F4T::QuestProtocol::kFireteamReasonUnknownMember;
		case JoinRefusal::None: break;
		}
		return {};
	}

	void FireteamService::NotePeers(const Peers& a_peers)
	{
		tracked.clear();
		for (const auto& peer : a_peers) {
			if (peer.playerId == 0) continue;
			tracked.insert_or_assign(peer.playerId, peer);
		}
	}

	void FireteamService::ForgetPlayer(std::uint32_t a_playerId)
	{
		tracked.erase(a_playerId);
	}

	void FireteamService::DropInvitesInvolving(std::uint32_t a_playerId)
	{
		for (auto invite = pendingInvites.begin(); invite != pendingInvites.end();) {
			if (invite->second.fromPlayerId != a_playerId && invite->second.targetPlayerId != a_playerId) {
				++invite;
				continue;
			}
			table.Decline(invite->first, invite->second.targetPlayerId);
			invite = pendingInvites.erase(invite);
		}
	}

	const FireteamService::FireteamPeer* FireteamService::TrackedFor(std::uint32_t a_playerId) const
	{
		const auto found = tracked.find(a_playerId);
		return found == tracked.end() ? nullptr : &found->second;
	}

	const FireteamService::FireteamPeer* FireteamService::ReachableFor(std::uint32_t a_playerId) const
	{
		const auto* peer = TrackedFor(a_playerId);
		if (peer == nullptr || peer->connectionId == 0 || !peer->capable) return nullptr;
		return peer;
	}

	const FireteamService::FireteamPeer* FireteamService::PeerFor(std::uint32_t a_playerId, const Peers* a_peers) const
	{
		if (a_peers == nullptr) return TrackedFor(a_playerId);
		for (const auto& peer : *a_peers) {
			if (peer.playerId == a_playerId) return &peer;
		}
		return nullptr;
	}

	std::string FireteamService::NameFor(const FireteamPeer& a_viewer, const FireteamPeer& a_subject, bool a_teammate) const
	{
		if (a_viewer.playerId == a_subject.playerId) return a_subject.name;
		return social.NameViewFor(
			a_viewer.socialKey,
			a_viewer.profileId,
			a_subject.socialKey,
			a_subject.name,
			a_teammate || factions.SameFaction(a_viewer.profileId, a_subject.profileId)).displayName;
	}

	bool FireteamService::Capable(const FireteamPeer& a_peer) const
	{
		return a_peer.playerId != 0 && a_peer.connectionId != 0 && a_peer.capable;
	}

	FireteamSnapshot FireteamService::SnapshotFor(const FireteamPeer& a_viewer, const Peers* a_peers) const
	{
		FireteamSnapshot snapshot;
		snapshot.fireteamId = table.TeamIdOf(a_viewer.playerId);
		if (snapshot.fireteamId.empty()) return snapshot;
		snapshot.leaderPlayerId = table.LeaderOf(a_viewer.playerId).value_or(0);
		const auto members = table.MembersOf(a_viewer.playerId);
		snapshot.members.reserve(members.size());
		for (const auto member : members) {
			const auto* subject = PeerFor(member, a_peers);
			snapshot.members.push_back(FireteamMember{ member, subject == nullptr ? std::string{} : NameFor(a_viewer, *subject, true) });
		}
		return snapshot;
	}

	OutboundFrame FireteamService::EmptySnapshotFrame(std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		return ReliableFrame(a_connectionId, F4T::QuestProtocol::MakeFireteamSnapshot(FireteamSnapshot{}, a_unixTimeSeconds));
	}

	std::vector<OutboundFrame> FireteamService::SnapshotFramesFor(
		const std::vector<std::uint32_t>& a_players,
		const Peers* a_peers,
		double a_unixTimeSeconds) const
	{
		std::vector<OutboundFrame> frames;
		for (const auto playerId : a_players) {
			const auto* viewer = ReachableFor(playerId);
			if (viewer == nullptr) continue;
			frames.push_back(ReliableFrame(viewer->connectionId, F4T::QuestProtocol::MakeFireteamSnapshot(SnapshotFor(*viewer, a_peers), a_unixTimeSeconds)));
		}
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::RefusalFrames(
		const FireteamPeer& a_self,
		JoinRefusal a_refusal,
		double a_unixTimeSeconds) const
	{
		std::vector<OutboundFrame> frames;
		if (a_refusal == JoinRefusal::None) return frames;
		frames.push_back(ReliableFrame(a_self.connectionId, F4T::QuestProtocol::MakeFireteamEvent(
			F4T::QuestProtocol::kFireteamEventType,
			ReasonName(a_refusal),
			a_self.playerId,
			a_unixTimeSeconds)));
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::Dispatch(
		std::string_view a_type,
		const FireteamPeer& a_self,
		FireteamSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		NotePeers(a_peers);
		if (!Capable(a_self)) return {};
		if (a_type == F4T::QuestProtocol::kFireteamInviteType) {
			return HandleInvite(a_self, a_state, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == F4T::QuestProtocol::kFireteamRespondType) {
			return HandleRespond(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == F4T::QuestProtocol::kFireteamLeaveType) {
			return HandleLeave(a_self, a_peers, a_unixTimeSeconds);
		}
		if (a_type == F4T::QuestProtocol::kFireteamKickType) {
			return HandleKick(a_self, a_peers, a_packet, a_unixTimeSeconds);
		}
		return {};
	}

	std::vector<OutboundFrame> FireteamService::HandleInvite(
		const FireteamPeer& a_self,
		FireteamSessionState& a_state,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto invite = F4T::QuestProtocol::NormalizeFireteamInvite(a_packet);
		if (!invite) return {};
		if (!F4T::PacketRateLimit::TakeToken(a_state.invites, a_monotonicSeconds)) return {};
		const auto* target = ReachableFor(invite->targetPlayerId);
		if (target == nullptr) return RefusalFrames(a_self, JoinRefusal::UnknownTarget, a_unixTimeSeconds);
		JoinRefusal refusal{ JoinRefusal::None };
		const auto inviteId = table.Invite(a_self.playerId, invite->targetPlayerId, a_unixTimeSeconds, refusal);
		if (!inviteId) return RefusalFrames(a_self, refusal, a_unixTimeSeconds);
		pendingInvites.insert_or_assign(*inviteId, PendingInvite{
			a_self.playerId,
			invite->targetPlayerId,
			a_unixTimeSeconds + F4T::Fireteam::kInviteTtlSeconds });
		std::vector<OutboundFrame> frames;
		FireteamInvite toInvitee;
		toInvitee.inviteId = *inviteId;
		toInvitee.fromPlayerId = a_self.playerId;
		toInvitee.fromName = NameFor(*target, a_self, false);
		frames.push_back(ReliableFrame(target->connectionId, F4T::QuestProtocol::MakeFireteamInvite(toInvitee, a_unixTimeSeconds)));
		FireteamInvite echo;
		echo.inviteId = *inviteId;
		echo.targetPlayerId = invite->targetPlayerId;
		frames.push_back(ReliableFrame(a_self.connectionId, F4T::QuestProtocol::MakeFireteamInvite(echo, a_unixTimeSeconds)));
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::HandleRespond(
		const FireteamPeer& a_self,
		FireteamSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto respond = F4T::QuestProtocol::NormalizeFireteamRespond(a_packet);
		if (!respond) return {};
		if (!F4T::PacketRateLimit::TakeToken(a_state.responses, a_monotonicSeconds)) return {};
		if (respond->accept) {
			JoinRefusal refusal{ JoinRefusal::None };
			const auto teamId = table.Accept(respond->inviteId, a_self.playerId, a_unixTimeSeconds, refusal);
			if (!teamId) {
				if (refusal == JoinRefusal::NoInvite && !table.InviteParties(respond->inviteId)) pendingInvites.erase(respond->inviteId);
				return RefusalFrames(a_self, refusal, a_unixTimeSeconds);
			}
			pendingInvites.erase(respond->inviteId);
			return SnapshotFramesFor(table.MembersOf(a_self.playerId), &a_peers, a_unixTimeSeconds);
		}
		const auto parties = table.InviteParties(respond->inviteId);
		if (!table.Decline(respond->inviteId, a_self.playerId)) {
			return RefusalFrames(a_self, JoinRefusal::NoInvite, a_unixTimeSeconds);
		}
		pendingInvites.erase(respond->inviteId);
		std::vector<OutboundFrame> frames;
		if (!parties) return frames;
		const auto* inviter = ReachableFor(parties->first);
		if (inviter == nullptr) return frames;
		FireteamRespond declined;
		declined.inviteId = respond->inviteId;
		declined.accept = false;
		frames.push_back(ReliableFrame(inviter->connectionId, F4T::QuestProtocol::MakeFireteamRespond(declined, a_self.playerId, a_unixTimeSeconds)));
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::HandleLeave(
		const FireteamPeer& a_self,
		const Peers& a_peers,
		double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		const auto before = table.MembersOf(a_self.playerId);
		std::string teamId;
		const auto outcome = table.Leave(a_self.playerId, teamId);
		if (outcome == F4T::Fireteam::LeaveOutcome::LeftTeamDisbanded) disbandedTeams.push_back(teamId);
		DropInvitesInvolving(a_self.playerId);
		if (outcome == F4T::Fireteam::LeaveOutcome::NotMember) {
			return RefusalFrames(a_self, JoinRefusal::NotMember, a_unixTimeSeconds);
		}
		std::vector<std::uint32_t> remaining;
		remaining.reserve(before.size());
		for (const auto member : before) {
			if (member != a_self.playerId) remaining.push_back(member);
		}
		AppendFrames(frames, SnapshotFramesFor(remaining, &a_peers, a_unixTimeSeconds));
		frames.push_back(EmptySnapshotFrame(a_self.connectionId, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::HandleKick(
		const FireteamPeer& a_self,
		const Peers& a_peers,
		const Json& a_packet,
		double a_unixTimeSeconds)
	{
		const auto kick = F4T::QuestProtocol::NormalizeFireteamKick(a_packet);
		if (!kick) return {};
		std::vector<OutboundFrame> frames;
		const auto before = table.MembersOf(a_self.playerId);
		std::string teamId;
		const auto refusal = table.Kick(a_self.playerId, kick->targetPlayerId, teamId);
		if (refusal != JoinRefusal::None) return RefusalFrames(a_self, refusal, a_unixTimeSeconds);
		DropInvitesInvolving(kick->targetPlayerId);
		if (const auto* target = ReachableFor(kick->targetPlayerId); target != nullptr) {
			frames.push_back(EmptySnapshotFrame(target->connectionId, a_unixTimeSeconds));
		}
		std::vector<std::uint32_t> remaining;
		remaining.reserve(before.size());
		for (const auto member : before) {
			if (member != kick->targetPlayerId) remaining.push_back(member);
		}
		AppendFrames(frames, SnapshotFramesFor(remaining, &a_peers, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::SnapshotFrames(
		std::uint32_t a_connectionId,
		std::uint32_t a_playerId,
		double a_unixTimeSeconds) const
	{
		std::vector<OutboundFrame> frames;
		if (a_connectionId == 0) return frames;
		const auto* viewer = TrackedFor(a_playerId);
		const FireteamPeer unknownViewer{ a_connectionId, a_playerId };
		frames.push_back(ReliableFrame(a_connectionId, F4T::QuestProtocol::MakeFireteamSnapshot(SnapshotFor(viewer == nullptr ? unknownViewer : *viewer, nullptr), a_unixTimeSeconds)));
		return frames;
	}

	std::vector<std::string> FireteamService::TakeDisbandedTeams()
	{
		return std::exchange(disbandedTeams, {});
	}

	std::vector<OutboundFrame> FireteamService::DisconnectFrames(std::uint32_t a_playerId, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		if (a_playerId == 0) return frames;
		const auto* leaver = ReachableFor(a_playerId);
		const auto before = table.MembersOf(a_playerId);
		std::string teamId;
		const auto outcome = table.Leave(a_playerId, teamId);
		if (outcome == F4T::Fireteam::LeaveOutcome::LeftTeamDisbanded) disbandedTeams.push_back(teamId);
		DropInvitesInvolving(a_playerId);
		if (outcome != F4T::Fireteam::LeaveOutcome::NotMember) {
			std::vector<std::uint32_t> remaining;
			remaining.reserve(before.size());
			for (const auto member : before) {
				if (member != a_playerId) remaining.push_back(member);
			}
			AppendFrames(frames, SnapshotFramesFor(remaining, nullptr, a_unixTimeSeconds));
			if (leaver != nullptr) frames.push_back(EmptySnapshotFrame(leaver->connectionId, a_unixTimeSeconds));
		}
		ForgetPlayer(a_playerId);
		return frames;
	}

	std::vector<OutboundFrame> FireteamService::ExpiryFrames(double a_nowSeconds)
	{
		std::vector<OutboundFrame> frames;
		std::vector<std::uint32_t> expiredInviters;
		for (auto invite = pendingInvites.begin(); invite != pendingInvites.end();) {
			if (a_nowSeconds < invite->second.expiresAtSeconds) {
				++invite;
				continue;
			}
			expiredInviters.push_back(invite->second.fromPlayerId);
			invite = pendingInvites.erase(invite);
		}
		table.ExpireInvites(a_nowSeconds);
		for (const auto inviterId : expiredInviters) {
			const auto* inviter = ReachableFor(inviterId);
			if (inviter == nullptr) continue;
			frames.push_back(ReliableFrame(inviter->connectionId, F4T::QuestProtocol::MakeFireteamEvent(
				F4T::QuestProtocol::kFireteamRespondType,
				F4T::QuestProtocol::kFireteamReasonNoInvite,
				inviterId,
				a_nowSeconds)));
		}
		return frames;
	}

	std::string FireteamService::TeamIdOfPlayer(std::uint32_t a_playerId) const
	{
		return table.TeamIdOf(a_playerId);
	}

	std::vector<std::uint32_t> FireteamService::MembersOfPlayer(std::uint32_t a_playerId) const
	{
		return table.MembersOf(a_playerId);
	}
}
