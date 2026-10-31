#include "ServerFactions.h"

#include "ServerFactionRules.h"
#include "ServerPacketRead.h"

#include "F4TFactionValidation.h"

#include <algorithm>
#include <cmath>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace FactionRules;

		std::uint32_t WholeSecondsRemaining(double a_expiresAtUnixSeconds, double a_unixTimeSeconds, double a_lifetimeSeconds)
		{
			return static_cast<std::uint32_t>(std::clamp(std::ceil(a_expiresAtUnixSeconds - a_unixTimeSeconds), 0.0, a_lifetimeSeconds));
		}
	}

	void FactionService::ExpireFactionInvites(double a_monotonicSeconds)
	{
		std::erase_if(factionInvites, [this, a_monotonicSeconds](const auto& a_entry) {
			const auto age = a_monotonicSeconds - a_entry.second.createdMonotonicSeconds;
			const auto expired = !(age >= 0.0) || age > limits.inviteLifetimeSeconds;
			if (expired) expiredInviters.insert(a_entry.second.inviterProfileId);
			return expired;
		});
		std::erase_if(factionInviteWindows, [this, a_monotonicSeconds](auto& a_entry) {
			std::erase_if(a_entry.second, [this, a_monotonicSeconds](double a_sentSeconds) {
				const auto age = a_monotonicSeconds - a_sentSeconds;
				return !(age >= 0.0) || age > limits.inviteWindowSeconds;
			});
			return a_entry.second.empty();
		});
	}

	std::vector<OutboundFrame> FactionService::InviteExpiryFrames(const Peers& a_peers, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& inviterProfileId : expiredInviters) {
			if (const auto* inviter = PeerForProfile(a_peers, inviterProfileId)) {
				frames.push_back(ReliableFrame(inviter->connectionId, FactionResultPacket("invite", false, "inviteExpired", a_unixTimeSeconds)));
			}
		}
		expiredInviters.clear();
		return frames;
	}

	Json FactionService::PendingInvitesJson(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds) const
	{
		const bool viewerSocialNames = a_viewer.socialNames;
		Json invites = Json::array();
		for (const auto& [inviteId, invite] : factionInvites) {
			const auto faction = factions.find(invite.factionId);
			if (invite.targetProfileId != a_viewer.profileId || faction == factions.end() || invite.expiresAtUnixSeconds <= a_unixTimeSeconds) continue;
			const auto inviter = MemberName(faction->second, invite.inviterProfileId);
			const auto view = PeerNameView(a_viewer.profileId, invite.inviterProfileId, inviter);
			Json pending{
				{ "inviteId", inviteId },
				{ "factionName", faction->second.name },
				{ "factionTag", faction->second.tag },
				{ "displayName", view.displayName },
				{ "expiresInSeconds", WholeSecondsRemaining(invite.expiresAtUnixSeconds, a_unixTimeSeconds, limits.inviteLifetimeSeconds) }
			};
			if (view.disclosedName) pending["fromName"] = *view.disclosedName;
			// Constraint: legacy clients get display text in fromName instead of no fromName.
			else if (!viewerSocialNames) pending["fromName"] = view.displayName;
			invites.push_back(std::move(pending));
		}
		return invites;
	}

	bool FactionService::FactionInviteRateAllows(const std::string& a_profileId) const
	{
		const auto recent = factionInviteWindows.find(a_profileId);
		return recent == factionInviteWindows.end() || recent->second.size() < limits.invitesPerWindow;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionInvite(
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto connectionId = a_self.connectionId;
		ExpireFactionInvites(a_monotonicSeconds);
		stats.factionPacketsReceived++;
		auto normalized = F4T::FactionValidation::NormalizeFactionInvite(a_packet);
		if (!normalized) return RejectMalformed();
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "invite", "rateLimited", a_unixTimeSeconds);
		const auto targetPlayerId = ReadUInt32(*normalized, "targetPlayerId").value_or(0);
		const auto faction = FactionOfProfile(a_self.profileId);
		if (!faction || (faction->leaderProfileId != a_self.profileId && !faction->officers.contains(a_self.profileId))) return FactionFailure(connectionId, "invite", "notLeader", a_unixTimeSeconds);
		if (faction->members.size() >= limits.maximumMembers) return FactionFailure(connectionId, "invite", "factionFull", a_unixTimeSeconds);
		if (targetPlayerId == a_self.playerId) return FactionFailure(connectionId, "invite", "invalidTarget", a_unixTimeSeconds);
		const auto* target = PeerForPlayer(a_peers, targetPlayerId);
		if (!target || target->profileId.empty()) return FactionFailure(connectionId, "invite", "targetUnavailable", a_unixTimeSeconds);
		const auto targetProfileId = target->profileId;
		if (factionByProfile.contains(targetProfileId)) return FactionFailure(connectionId, "invite", "targetInFaction", a_unixTimeSeconds);
		if (std::any_of(factionInvites.begin(), factionInvites.end(), [&faction, &targetProfileId](const auto& a_entry) {
				return a_entry.second.factionId == faction->factionId && a_entry.second.targetProfileId == targetProfileId;
			})) {
			return FactionFailure(connectionId, "invite", "inviteExists", a_unixTimeSeconds);
		}
		if (!FactionInviteRateAllows(a_self.profileId)) return FactionFailure(connectionId, "invite", "rateLimited", a_unixTimeSeconds);
		if (factionInvites.size() >= limits.pendingInvites) return FactionFailure(connectionId, "invite", "inviteCapacity", a_unixTimeSeconds);
		const auto pendingForTarget = std::count_if(factionInvites.begin(), factionInvites.end(), [&targetProfileId](const auto& a_entry) {
			return a_entry.second.targetProfileId == targetProfileId;
		});
		if (static_cast<std::size_t>(pendingForTarget) >= limits.pendingInvitesPerTarget) return FactionFailure(connectionId, "invite", "inviteCapacity", a_unixTimeSeconds);

		const auto inviteId = "i-" + std::to_string(nextFactionInviteNumber++);
		factionInvites.emplace(inviteId, FactionInvite{
			inviteId,
			faction->factionId,
			a_self.profileId,
			targetProfileId,
			targetPlayerId,
			a_monotonicSeconds,
			a_unixTimeSeconds + limits.inviteLifetimeSeconds
		});
		factionInviteWindows[a_self.profileId].push_back(a_monotonicSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("invite", true, {}, a_unixTimeSeconds)) };
		const auto inviteView = PeerNameView(targetProfileId, a_self.profileId, a_self.name);
		Json received{
			{ "type", "factionInviteReceived" },
			{ "inviteId", inviteId },
			{ "factionId", faction->factionId },
			{ "factionName", faction->name },
			{ "factionTag", faction->tag },
			{ "fromPlayerId", a_self.playerId },
			{ "displayName", inviteView.displayName },
			{ "expiresInSeconds", WholeSecondsRemaining(a_unixTimeSeconds + limits.inviteLifetimeSeconds, a_unixTimeSeconds, limits.inviteLifetimeSeconds) },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (inviteView.disclosedName) received["fromName"] = *inviteView.disclosedName;
		// Constraint: legacy clients get display text in fromName instead of no fromName.
		else if (!target->socialNames) received["fromName"] = inviteView.displayName;
		frames.push_back(ReliableFrame(target->connectionId, std::move(received)));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionInviteResponse(
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto connectionId = a_self.connectionId;
		ExpireFactionInvites(a_monotonicSeconds);
		stats.factionPacketsReceived++;
		auto normalized = F4T::FactionValidation::NormalizeFactionInviteResponse(a_packet);
		if (!normalized) return RejectMalformed();
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "inviteResponse", "rateLimited", a_unixTimeSeconds);
		const auto invite = factionInvites.find(ReadString(*normalized, "inviteId").value_or(""));
		if (invite == factionInvites.end()) return FactionFailure(connectionId, "inviteResponse", "inviteNotFound", a_unixTimeSeconds);
		if (invite->second.targetProfileId != a_self.profileId) return FactionFailure(connectionId, "inviteResponse", "notInvited", a_unixTimeSeconds);
		const auto factionId = invite->second.factionId;
		if (!normalized->at("accept").get<bool>()) {
			const auto inviterProfileId = invite->second.inviterProfileId;
			factionInvites.erase(invite);
			std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("inviteResponse", true, {}, a_unixTimeSeconds)) };
			if (const auto* inviter = PeerForProfile(a_peers, inviterProfileId)) {
				frames.push_back(ReliableFrame(inviter->connectionId, FactionResultPacket("invite", false, "declined", a_unixTimeSeconds)));
			}
			return frames;
		}
		const auto faction = factions.find(factionId);
		if (faction == factions.end()) return FactionFailure(connectionId, "inviteResponse", "factionNotFound", a_unixTimeSeconds);
		if (faction->second.members.size() >= limits.maximumMembers) return FactionFailure(connectionId, "inviteResponse", "factionFull", a_unixTimeSeconds);
		if (factionByProfile.contains(a_self.profileId)) return FactionFailure(connectionId, "inviteResponse", "alreadyInFaction", a_unixTimeSeconds);

		const auto snapshot = SnapshotFactions();
		const auto existingMembers = faction->second.members;
		faction->second.members.push_back(a_self.profileId);
		factionByProfile[a_self.profileId] = factionId;
		std::erase_if(factionInvites, [&a_self](const auto& a_entry) { return a_entry.second.targetProfileId == a_self.profileId; });
		RefreshFactionNames(a_peers, faction->second);
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "inviteResponse", a_unixTimeSeconds);
		for (const auto& member : existingMembers) social.NoteKnown(a_self.profileId, member);
		(void)social.SaveNow();
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("inviteResponse", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		return frames;
	}
}
