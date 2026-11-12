#include "ServerQuestTestSupport.h"

#include "F4TQuestProtocol.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace QuestTestSupport;

namespace
{
	struct Client
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::vector<OutboundFrame> hello;
	};

	std::vector<OutboundFrame> SendPacket(ProtocolHost& a_host, const Client& a_client, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return Send(a_host, a_client.connectionId, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
	}

	Client JoinClient(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const std::string& a_profileId,
		bool a_fireteam,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Client client;
		client.connectionId = a_connectionId;
		client.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		Json capabilities = Json::array({
			std::string{ F4T::QuestProtocol::kSyncCapability },
			std::string{ F4T::QuestProtocol::kReadyCapability } });
		if (a_fireteam) capabilities.push_back(std::string{ F4T::QuestProtocol::kFireteamCapability });
		client.hello = Send(a_host, a_connectionId, Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", std::move(capabilities) } }, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(client.hello, a_connectionId, "sessionReady"));
		return client;
	}

	std::vector<OutboundFrame> Invite(ProtocolHost& a_host, const Client& a_from, std::uint32_t a_targetPlayerId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return SendPacket(a_host, a_from, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamInviteType } },
			{ "targetPlayerId", a_targetPlayerId } }, a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> Respond(ProtocolHost& a_host, const Client& a_from, const std::string& a_inviteId, bool a_accept, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return SendPacket(a_host, a_from, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamRespondType } },
			{ "inviteId", a_inviteId },
			{ "accept", a_accept } }, a_monotonicSeconds, a_unixTimeSeconds);
	}

	std::string InviteIdFor(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const auto packet = RequirePacket(a_frames, a_connectionId, F4T::QuestProtocol::kFireteamInviteType);
		const auto inviteId = packet.at("inviteId").get<std::string>();
		assert(!inviteId.empty());
		return inviteId;
	}

	std::string ReasonFor(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return RequirePacket(a_frames, a_connectionId, F4T::QuestProtocol::kFireteamEventType).at("reason").get<std::string>();
	}

	std::vector<std::uint32_t> MemberIds(const Json& a_snapshot)
	{
		std::vector<std::uint32_t> ids;
		for (const auto& member : a_snapshot.at("members")) {
			ids.push_back(member.at("playerId").get<std::uint32_t>());
		}
		return ids;
	}

	void AssertEmptySnapshot(const Json& a_snapshot)
	{
		assert(a_snapshot.at("fireteamId").get<std::string>().empty());
		assert(a_snapshot.at("leaderPlayerId").get<std::uint32_t>() == 0);
		assert(a_snapshot.at("members").empty());
	}

	void AssertTeamSnapshot(const Json& a_snapshot, std::uint32_t a_leaderPlayerId, const std::vector<std::uint32_t>& a_members)
	{
		assert(!a_snapshot.at("fireteamId").get<std::string>().empty());
		assert(a_snapshot.at("leaderPlayerId").get<std::uint32_t>() == a_leaderPlayerId);
		const auto ids = MemberIds(a_snapshot);
		assert(ids.size() == a_members.size());
		for (const auto member : a_members) {
			bool found = false;
			for (const auto id : ids) {
				if (id == member) found = true;
			}
			assert(found);
		}
	}

	void TestWelcomeSnapshotIsEmpty()
	{
		ProtocolHost host{ "Fireteam Welcome" };
		const auto capable = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto snapshots = FramesOfType(capable.hello, F4T::QuestProtocol::kFireteamSnapshotType);
		assert(snapshots.size() == 1);
		AssertEmptySnapshot(snapshots.front());

		const auto legacy = JoinClient(host, 20, "p20", false, 1.1, 100.1);
		assert(FramesOfType(legacy.hello, F4T::QuestProtocol::kFireteamSnapshotType).empty());
	}

	void TestInviteEchoAndAcceptSnapshot()
	{
		ProtocolHost host{ "Fireteam Invite" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);

		const auto invited = Invite(host, leader, member.playerId, 2.0, 102.0);
		const auto toMember = RequirePacket(invited, member.connectionId, F4T::QuestProtocol::kFireteamInviteType);
		assert(toMember.at("fromPlayerId").get<std::uint32_t>() == leader.playerId);
		assert(toMember.value("fromName", std::string{}) == "Stranger");
		assert(!toMember.contains("targetPlayerId"));
		const auto toLeader = RequirePacket(invited, leader.connectionId, F4T::QuestProtocol::kFireteamInviteType);
		assert(toLeader.at("targetPlayerId").get<std::uint32_t>() == member.playerId);
		assert(toLeader.at("fromPlayerId").get<std::uint32_t>() == 0);
		assert(toLeader.at("inviteId").get<std::string>() == toMember.at("inviteId").get<std::string>());
		assert(FramesOfType(invited, F4T::QuestProtocol::kFireteamEventType).empty());

		const auto inviteId = toMember.at("inviteId").get<std::string>();
		const auto accepted = Respond(host, member, inviteId, true, 3.0, 103.0);
		const auto leaderSnapshot = RequirePacket(accepted, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType);
		const auto memberSnapshot = RequirePacket(accepted, member.connectionId, F4T::QuestProtocol::kFireteamSnapshotType);
		AssertTeamSnapshot(leaderSnapshot, leader.playerId, { leader.playerId, member.playerId });
		AssertTeamSnapshot(memberSnapshot, leader.playerId, { leader.playerId, member.playerId });
		assert(leaderSnapshot.at("fireteamId").get<std::string>() == memberSnapshot.at("fireteamId").get<std::string>());

		const auto again = Invite(host, leader, member.playerId, 4.0, 104.0);
		assert(ReasonFor(again, leader.connectionId) == F4T::QuestProtocol::kFireteamReasonAlreadyMember);
		assert(!HasPacket(again, member.connectionId, F4T::QuestProtocol::kFireteamInviteType));
	}

	void TestTeamCapacityIsFour()
	{
		ProtocolHost host{ "Fireteam Capacity" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);
		const auto fourth = JoinClient(host, 40, "p40", true, 1.3, 100.3);
		const auto fifth = JoinClient(host, 50, "p50", true, 1.4, 100.4);
		const auto sixth = JoinClient(host, 60, "p60", true, 1.5, 100.5);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third, fourth }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			assert(InviteIdFor(invited, joiner.connectionId).size() > 1);
			const auto accepted = Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.5, 100.5 + static_cast<double>(slot));
			assert(RequirePacket(accepted, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType).at("members").size() == 2 + (slot - 2));
			slot += 1;
		}

		const auto full = Invite(host, leader, fifth.playerId, 8.0, 108.0);
		assert(ReasonFor(full, leader.connectionId) == F4T::QuestProtocol::kFireteamReasonFull);
		assert(!HasPacket(full, fifth.connectionId, F4T::QuestProtocol::kFireteamInviteType));

		const auto outsider = Invite(host, sixth, fifth.playerId, 9.0, 109.0);
		assert(HasPacket(outsider, fifth.connectionId, F4T::QuestProtocol::kFireteamInviteType));
		const auto declined = Respond(host, fifth, InviteIdFor(outsider, fifth.connectionId), true, 10.0, 110.0);
		const auto outsiderSnapshot = RequirePacket(declined, sixth.connectionId, F4T::QuestProtocol::kFireteamSnapshotType);
		AssertTeamSnapshot(outsiderSnapshot, sixth.playerId, { sixth.playerId, fifth.playerId });
	}

	void TestDeclineNotifiesOnlyInviter()
	{
		ProtocolHost host{ "Fireteam Decline" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto observer = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		const auto invited = Invite(host, leader, member.playerId, 2.0, 102.0);
		const auto inviteId = InviteIdFor(invited, member.connectionId);
		const auto declined = Respond(host, member, inviteId, false, 3.0, 103.0);

		const auto toLeader = RequirePacket(declined, leader.connectionId, F4T::QuestProtocol::kFireteamRespondType);
		assert(toLeader.at("inviteId").get<std::string>() == inviteId);
		assert(toLeader.at("accept").get<bool>() == false);
		assert(toLeader.at("fromPlayerId").get<std::uint32_t>() == member.playerId);
		assert(!HasPacket(declined, member.connectionId, F4T::QuestProtocol::kFireteamRespondType));
		assert(!HasPacket(declined, observer.connectionId, F4T::QuestProtocol::kFireteamRespondType));
		assert(FramesOfType(declined, F4T::QuestProtocol::kFireteamSnapshotType).empty());

		const auto lateAccept = Respond(host, member, inviteId, true, 4.0, 104.0);
		assert(ReasonFor(lateAccept, member.connectionId) == F4T::QuestProtocol::kFireteamReasonNoInvite);
	}

	void TestLeaveSendsEmptySnapshotToLeaver()
	{
		ProtocolHost host{ "Fireteam Leave" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.4, 100.4 + static_cast<double>(slot));
			slot += 1;
		}

		const auto left = SendPacket(host, second, Json{ { "type", std::string{ F4T::QuestProtocol::kFireteamLeaveType } } }, 6.0, 106.0);
		AssertEmptySnapshot(RequirePacket(left, second.connectionId, F4T::QuestProtocol::kFireteamSnapshotType));
		AssertTeamSnapshot(RequirePacket(left, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
		AssertTeamSnapshot(RequirePacket(left, third.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
		assert(!HasPacket(left, second.connectionId, F4T::QuestProtocol::kFireteamEventType));

		const auto leftAgain = SendPacket(host, second, Json{ { "type", std::string{ F4T::QuestProtocol::kFireteamLeaveType } } }, 7.0, 107.0);
		assert(ReasonFor(leftAgain, second.connectionId) == F4T::QuestProtocol::kFireteamReasonUnknownMember);
	}

	void TestKickLeaderOnly()
	{
		ProtocolHost host{ "Fireteam Kick" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.4, 100.4 + static_cast<double>(slot));
			slot += 1;
		}

		const auto denied = SendPacket(host, second, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamKickType } },
			{ "targetPlayerId", third.playerId } }, 6.0, 106.0);
		assert(ReasonFor(denied, second.connectionId) == F4T::QuestProtocol::kFireteamReasonNotLeader);
		assert(!HasPacket(denied, third.connectionId, F4T::QuestProtocol::kFireteamSnapshotType));

		const auto kicked = SendPacket(host, leader, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamKickType } },
			{ "targetPlayerId", third.playerId } }, 7.0, 107.0);
		AssertEmptySnapshot(RequirePacket(kicked, third.connectionId, F4T::QuestProtocol::kFireteamSnapshotType));
		AssertTeamSnapshot(RequirePacket(kicked, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, second.playerId });
		AssertTeamSnapshot(RequirePacket(kicked, second.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, second.playerId });
		assert(!HasPacket(kicked, leader.connectionId, F4T::QuestProtocol::kFireteamEventType));
	}

	void TestLeaderDisconnectPromotesLowestMember()
	{
		ProtocolHost host{ "Fireteam Disconnect" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.4, 100.4 + static_cast<double>(slot));
			slot += 1;
		}

		const auto frames = host.HandleDisconnected(leader.connectionId, 106.0);
		AssertTeamSnapshot(RequirePacket(frames, second.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), second.playerId, { second.playerId, third.playerId });
		AssertTeamSnapshot(RequirePacket(frames, third.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), second.playerId, { second.playerId, third.playerId });
	}

	void TestIncapableClientIsNeitherTargetedNorServed()
	{
		ProtocolHost host{ "Fireteam Capability" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto legacy = JoinClient(host, 20, "p20", false, 1.1, 100.1);

		const auto invited = Invite(host, leader, legacy.playerId, 2.0, 102.0);
		assert(ReasonFor(invited, leader.connectionId) == F4T::QuestProtocol::kFireteamReasonInvalidTarget);
		assert(FramesOfType(invited, F4T::QuestProtocol::kFireteamInviteType).empty());
		assert(!HasPacket(invited, legacy.connectionId, F4T::QuestProtocol::kFireteamEventType));

		const auto fromLegacy = Invite(host, legacy, leader.playerId, 3.0, 103.0);
		assert(FramesOfType(fromLegacy, F4T::QuestProtocol::kFireteamInviteType).empty());
		assert(FramesOfType(fromLegacy, F4T::QuestProtocol::kFireteamEventType).empty());
		assert(FramesOfType(fromLegacy, F4T::QuestProtocol::kFireteamSnapshotType).empty());
		assert(host.Stats().packetsRejected > 0);
	}
	void SyncClient(ProtocolHost& a_host, const Client& a_client, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		Send(a_host, a_client.connectionId, Report(kTutorialQuest, kTutorialExitStage), a_monotonicSeconds, a_unixTimeSeconds);
		Send(a_host, a_client.connectionId, Json{ { "type", "questReady" } }, a_monotonicSeconds + 0.01, a_unixTimeSeconds + 0.01);
	}

	void TestFireteamQuestStageIsNotRelayed()
	{
		ProtocolHost host{ "Fireteam Quest Scope" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto first = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		SyncClient(host, first, 1.2, 100.2);
		SyncClient(host, second, 1.3, 100.3);

		const auto invite = Invite(host, first, second.playerId, 2.0, 102.0);
		const auto accepted = Respond(host, second, InviteIdFor(invite, 20), true, 2.1, 102.1);
		AssertTeamSnapshot(RequirePacket(accepted, 10, F4T::QuestProtocol::kFireteamSnapshotType), first.playerId, { first.playerId, second.playerId });

		const auto policy = RequirePacket(Send(host, 10, ScopeRequestPacket(kQuest, "fireteam"), 2.2, 102.2), 10, "questPolicy");
		assert(policy.at("fireteam").get<bool>());
		assert(policy.at("fireteamQuests").size() == 1);
		assert(policy.at("fireteamQuests").at(0) == kQuest);
		assert(HasPacket(Send(host, 20, ScopeRequestPacket(kQuest, "fireteam"), 2.3, 102.3), 20, "questPolicy"));

		const auto beforeStage = host.Stats();
		const auto fireteamStage = Send(host, 10, Report(kQuest, 30), 2.4, 102.4);
		assert(!HasPacket(fireteamStage, 10, "questStage"));
		assert(!HasPacket(fireteamStage, 20, "questStage"));
		assert(host.Stats().questStagesRelayed == beforeStage.questStagesRelayed + 1);
		assert(HasPacket(fireteamStage, 20, F4T::QuestProtocol::kQuestPresenceType));

		assert(host.QuestStatus().at("stages").empty());

		assert(HasPacket(Send(host, 10, ScopeRequestPacket(kOtherQuest, "shared"), 2.5, 102.5), 10, "questPolicy"));
		const auto sharedStage = Send(host, 10, Report(kOtherQuest, 5), 2.6, 102.6);
		assert(HasPacket(sharedStage, 10, "questStage"));
		assert(HasPacket(sharedStage, 20, "questStage"));
	}

	void TestFireteamMemberSharedScopeIsApplied()
	{
		ProtocolHost host{ "Fireteam Shared Scope" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		SyncClient(host, leader, 1.2, 100.2);
		SyncClient(host, member, 1.3, 100.3);

		const auto invite = Invite(host, leader, member.playerId, 2.0, 102.0);
		Respond(host, member, InviteIdFor(invite, 20), true, 2.1, 102.1);
		assert(HasPacket(Send(host, 20, ScopeRequestPacket(kQuest, "fireteam"), 2.2, 102.2), 20, "questPolicy"));

		const auto applied = host.Stats().questScopesApplied;
		const auto shared = Send(host, 20, ScopeRequestPacket(kQuest, "shared"), 2.3, 102.3);
		assert(!HasPacket(shared, 20, "questNotReady"));
		assert(host.Stats().questScopesApplied == applied + 1);
		assert(RequirePacket(shared, 20, "questPolicy").at("fireteamQuests").empty());
	}

	void TestExpiredInviteNotifiesTheInviter()
	{
		ProtocolHost host{ "Fireteam Expiry" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto guest = JoinClient(host, 20, "p20", true, 1.1, 100.1);

		const auto invite = Invite(host, leader, guest.playerId, 2.0, 102.0);
		assert(!InviteIdFor(invite, 20).empty());

		const auto expired = host.PollFireteamInvites(200.0);
		assert(!expired.empty());
		const auto notice = RequirePacket(expired, leader.connectionId, F4T::QuestProtocol::kFireteamRespondType);
		assert(notice.at("reason") == std::string{ F4T::QuestProtocol::kFireteamReasonNoInvite });
	}
	void TestRefusedAcceptStillExpiresTheInvite()
	{
		ProtocolHost host{ "Fireteam Refused Expiry" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto first = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto second = JoinClient(host, 30, "p30", true, 1.2, 100.2);
		const auto late = JoinClient(host, 40, "p40", true, 1.3, 100.3);
		const auto waiting = JoinClient(host, 50, "p50", true, 1.4, 100.4);

		const auto inviteFirst = Invite(host, leader, first.playerId, 2.0, 102.0);
		assert(!Respond(host, first, InviteIdFor(inviteFirst, 20), true, 2.1, 102.1).empty());
		const auto inviteSecond = Invite(host, leader, second.playerId, 2.2, 102.2);
		assert(!Respond(host, second, InviteIdFor(inviteSecond, 30), true, 2.3, 102.3).empty());

		const auto inviteWaiting = Invite(host, leader, waiting.playerId, 2.4, 102.4);
		const auto waitingInviteId = InviteIdFor(inviteWaiting, 50);

		const auto inviteLate = Invite(host, leader, late.playerId, 2.5, 102.5);
		assert(!Respond(host, late, InviteIdFor(inviteLate, 40), true, 2.6, 102.6).empty());

		const auto refused = Respond(host, waiting, waitingInviteId, true, 2.7, 102.7);
		assert(ReasonFor(refused, waiting.connectionId) == F4T::QuestProtocol::kFireteamReasonFull);

		const auto expired = host.PollFireteamInvites(200.0);
		assert(!expired.empty());
		const auto notice = RequirePacket(expired, leader.connectionId, F4T::QuestProtocol::kFireteamRespondType);
		assert(notice.at("reason") == std::string{ F4T::QuestProtocol::kFireteamReasonNoInvite });
	}

	void TestMemberDisconnectRemovesItFromTeam()
	{
		ProtocolHost host{ "Fireteam Member Disconnect" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.4, 100.4 + static_cast<double>(slot));
			slot += 1;
		}

		const auto frames = host.HandleDisconnected(second.connectionId, 106.0);
		AssertTeamSnapshot(RequirePacket(frames, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
		AssertTeamSnapshot(RequirePacket(frames, third.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
	}

	void TestLegacyScopeReplyOmitsFireteamKeys()
	{
		ProtocolHost host{ "Fireteam Legacy Policy" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto legacy = JoinClient(host, 20, "p20", false, 1.0, 100.0);
		SyncClient(host, legacy, 1.2, 100.2);
		const auto policy = RequirePacket(Send(host, 20, ScopeRequestPacket(kQuest, "fireteam"), 2.2, 102.2), 20, "questPolicy");
		assert(!policy.contains("fireteam"));
		assert(!policy.contains("fireteamQuests"));
	}

	void TestDisbandDropsFireteamTracks()
	{
		ProtocolHost host{ "Fireteam Disband" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		SyncClient(host, leader, 1.2, 100.2);
		SyncClient(host, member, 1.3, 100.3);

		const auto invite = Invite(host, leader, member.playerId, 2.0, 102.0);
		Respond(host, member, InviteIdFor(invite, 20), true, 2.1, 102.1);
		assert(HasPacket(Send(host, 10, ScopeRequestPacket(kQuest, "fireteam"), 2.2, 102.2), 10, "questPolicy"));
		Send(host, 10, Report(kQuest, 30), 2.3, 102.3);
		const auto before = host.QuestStatus().at("version").get<std::uint32_t>();

		SendPacket(host, member, Json{ { "type", std::string{ F4T::QuestProtocol::kFireteamLeaveType } } }, 2.4, 102.4);
		assert(host.QuestStatus().at("version").get<std::uint32_t>() == before);

		SendPacket(host, leader, Json{ { "type", std::string{ F4T::QuestProtocol::kFireteamLeaveType } } }, 2.5, 102.5);
		assert(host.QuestStatus().at("version").get<std::uint32_t>() == before + 1);
	}

	void TestSupersedeRemovesOldSessionFromTeam()
	{
		const auto directory = TempDirectory("fireteam-supersede");
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(directory / "durable-state.json", clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Fireteam Supersede" };
		assert(host.EnableCharacterSlots(&gate, 3));
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto second = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto third = JoinClient(host, 30, "p30", true, 1.2, 100.2);

		std::uint32_t slot = 2;
		for (const auto& joiner : { second, third }) {
			const auto invited = Invite(host, leader, joiner.playerId, static_cast<double>(slot), 100.0 + static_cast<double>(slot));
			Respond(host, joiner, InviteIdFor(invited, joiner.connectionId), true, static_cast<double>(slot) + 0.4, 100.4 + static_cast<double>(slot));
			slot += 1;
		}

		const auto replacement = JoinClient(host, 40, "p20", true, 6.0, 106.0);
		assert(RequirePacket(replacement.hello, 20, "sessionEnded").at("code") == "session_superseded");
		AssertTeamSnapshot(RequirePacket(replacement.hello, 10, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
		AssertTeamSnapshot(RequirePacket(replacement.hello, 30, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, third.playerId });
		AssertEmptySnapshot(RequirePacket(replacement.hello, 40, F4T::QuestProtocol::kFireteamSnapshotType));
		assert(host.ActiveCount() == 3);
		std::filesystem::remove_all(directory);
	}

	void TestSupersedeDisbandsTeamAndDropsItsTracks()
	{
		const auto directory = TempDirectory("fireteam-supersede-disband");
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(directory / "durable-state.json", clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Fireteam Supersede Disband" };
		assert(host.EnableCharacterSlots(&gate, 3));
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinClient(host, 10, "profile-10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "profile-20", true, 1.1, 100.1);
		SyncClient(host, leader, 1.2, 100.2);
		SyncClient(host, member, 1.3, 100.3);

		const auto invite = Invite(host, leader, member.playerId, 2.0, 102.0);
		Respond(host, member, InviteIdFor(invite, 20), true, 2.1, 102.1);
		assert(HasPacket(Send(host, 10, ScopeRequestPacket(kQuest, "fireteam"), 2.2, 102.2), 10, "questPolicy"));
		Send(host, 10, Report(kQuest, 30), 2.3, 102.3);
		SendPacket(host, member, Json{ { "type", std::string{ F4T::QuestProtocol::kFireteamLeaveType } } }, 2.4, 102.4);
		const auto before = host.QuestStatus().at("version").get<std::uint32_t>();

		const auto replacement = JoinClient(host, 30, "profile-10", true, 3.0, 103.0);
		assert(RequirePacket(replacement.hello, 10, "sessionEnded").at("code") == "session_superseded");
		assert(host.QuestStatus().at("version").get<std::uint32_t>() == before + 1);
		assert(host.ActiveCount() == 2);
		std::filesystem::remove_all(directory);
	}

	void TestUnrelatedResponseCannotBypassDisconnectCleanup()
	{
		ProtocolHost host{ "Fireteam Forged Response" };
		const auto inviter = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto target = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto outsider = JoinClient(host, 30, "p30", true, 1.2, 100.2);
		const auto id = InviteIdFor(Invite(host, inviter, target.playerId, 2.0, 102.0), target.connectionId);
		assert(ReasonFor(Respond(host, outsider, id, true, 3.0, 103.0), outsider.connectionId) == F4T::QuestProtocol::kFireteamReasonNoInvite);
		host.HandleDisconnected(inviter.connectionId, 104.0);
		assert(ReasonFor(Respond(host, target, id, true, 5.0, 105.0), target.connectionId) == F4T::QuestProtocol::kFireteamReasonNoInvite);
	}

	void TestUnrelatedResponseKeepsInviteUsable()
	{
		ProtocolHost host{ "Fireteam Forged Response Preserves Invite" };
		const auto inviter = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto target = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto outsider = JoinClient(host, 30, "p30", true, 1.2, 100.2);
		const auto id = InviteIdFor(Invite(host, inviter, target.playerId, 2.0, 102.0), target.connectionId);
		assert(ReasonFor(Respond(host, outsider, id, true, 3.0, 103.0), outsider.connectionId) == F4T::QuestProtocol::kFireteamReasonNoInvite);
		const auto accepted = Respond(host, target, id, true, 4.0, 104.0);
		AssertTeamSnapshot(RequirePacket(accepted, target.connectionId, F4T::QuestProtocol::kFireteamSnapshotType), inviter.playerId, { inviter.playerId, target.playerId });
	}

	void TestFailedSnapshotDeliveryIsRetriedWithCurrentState()
	{
		ProtocolHost host{ "Fireteam Snapshot Retry" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		const auto invited = Invite(host, leader, member.playerId, 2.0, 102.0);
		const auto accepted = Respond(host, member, InviteIdFor(invited, member.connectionId), true, 3.0, 103.0);
		const auto* lost = FindFrame(accepted, 10, F4T::QuestProtocol::kFireteamSnapshotType);
		assert(lost != nullptr);
		host.FrameDeliveryCompleted(*lost, false);

		const auto retried = host.PollFireteamInvites(104.0);
		AssertTeamSnapshot(RequirePacket(retried, 10, F4T::QuestProtocol::kFireteamSnapshotType), leader.playerId, { leader.playerId, member.playerId });
		assert(!HasPacket(host.PollFireteamInvites(105.0), 10, F4T::QuestProtocol::kFireteamSnapshotType));
	}

	std::string MemberName(const Json& a_snapshot, std::uint32_t a_playerId)
	{
		for (const auto& member : a_snapshot.at("members")) {
			if (member.at("playerId").get<std::uint32_t>() == a_playerId) return member.at("characterName").get<std::string>();
		}
		assert(false);
		return {};
	}

	Client JoinFriendClient(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Client client;
		client.connectionId = a_connectionId;
		client.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		client.hello = Send(a_host, a_connectionId, Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", Json::array({
				"social-names-v1",
				"friends-v1",
				std::string{ F4T::QuestProtocol::kSyncCapability },
				std::string{ F4T::QuestProtocol::kReadyCapability },
				std::string{ F4T::QuestProtocol::kFireteamCapability } }) } }, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(client.hello, a_connectionId, "sessionReady"));
		return client;
	}

	void TestFireteamMembersSeeEachOthersNameInSnapshot()
	{
		ProtocolHost host{ "Fireteam Teammate Name" };
		const auto leader = JoinClient(host, 10, "p10", true, 1.0, 100.0);
		const auto member = JoinClient(host, 20, "p20", true, 1.1, 100.1);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 1.2, 100.2);
		Send(host, 20, Json{ { "type", "playerState" }, { "characterName", "Ranger" } }, 1.3, 100.3);

		const auto invited = Invite(host, leader, member.playerId, 2.0, 102.0);
		const auto accepted = Respond(host, member, InviteIdFor(invited, member.connectionId), true, 3.0, 103.0);
		const auto leaderSnapshot = RequirePacket(accepted, leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType);
		const auto memberSnapshot = RequirePacket(accepted, member.connectionId, F4T::QuestProtocol::kFireteamSnapshotType);
		assert(MemberName(leaderSnapshot, leader.playerId) == "Nomad" && MemberName(leaderSnapshot, member.playerId) == "Ranger");
		assert(MemberName(memberSnapshot, leader.playerId) == "Nomad" && MemberName(memberSnapshot, member.playerId) == "Ranger");
	}

	void TestFriendInviteShowsRealName()
	{
		ProtocolHost host{ "Fireteam Friend Invite" };
		assert(host.EnableSocial(nullptr));
		const auto leader = JoinFriendClient(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinFriendClient(host, 20, "p20", 1.1, 100.1);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 1.2, 100.2);
		const auto sent = Send(host, 20, Json{ { "type", "friendRequest" }, { "targetPlayerId", leader.playerId } }, 1.3, 100.3);
		assert(RequirePacket(sent, 20, "friendResult").at("ok") == true);
		const auto requestId = RequirePacket(sent, 10, "friendRequestReceived").at("requestId").get<std::string>();
		const auto accepted = Send(host, 10, Json{ { "type", "friendRequestResponse" }, { "requestId", requestId }, { "accept", true } }, 1.4, 100.4);
		assert(RequirePacket(accepted, 20, "friendState").at("friends").size() >= 1);

		const auto invited = Invite(host, leader, member.playerId, 2.0, 102.0);
		assert(RequirePacket(invited, member.connectionId, F4T::QuestProtocol::kFireteamInviteType).at("fromName") == "Nomad");
	}
}

int main()
{
	TestWelcomeSnapshotIsEmpty();
	TestInviteEchoAndAcceptSnapshot();
	TestUnrelatedResponseCannotBypassDisconnectCleanup();
	TestUnrelatedResponseKeepsInviteUsable();
	TestFailedSnapshotDeliveryIsRetriedWithCurrentState();
	TestTeamCapacityIsFour();
	TestDeclineNotifiesOnlyInviter();
	TestLeaveSendsEmptySnapshotToLeaver();
	TestKickLeaderOnly();
	TestLeaderDisconnectPromotesLowestMember();
	TestIncapableClientIsNeitherTargetedNorServed();
	TestFireteamQuestStageIsNotRelayed();
	TestFireteamMemberSharedScopeIsApplied();
	TestExpiredInviteNotifiesTheInviter();
	TestRefusedAcceptStillExpiresTheInvite();
	TestMemberDisconnectRemovesItFromTeam();
	TestLegacyScopeReplyOmitsFireteamKeys();
	TestDisbandDropsFireteamTracks();
	TestSupersedeRemovesOldSessionFromTeam();
	TestSupersedeDisbandsTeamAndDropsItsTracks();
	TestFireteamMembersSeeEachOthersNameInSnapshot();
	TestFriendInviteShowsRealName();
	return 0;
}
