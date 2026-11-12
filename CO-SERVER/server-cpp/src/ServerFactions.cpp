#include "ServerFactions.h"

#include "ServerFactionRules.h"
#include "ServerFactionVetoScope.h"
#include "ServerPacketRead.h"

#include "F4TFactionValidation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace FactionRules;

		const FactionPeer* OnlineVisibleTo(const FactionPeer* a_peer, const FactionPeer& a_viewer)
		{
			return a_peer && a_peer->hidesOnline && a_peer->profileId != a_viewer.profileId ? nullptr : a_peer;
		}
	}

	Json FactionService::FactionResultPacket(std::string_view a_op, bool a_ok, std::string_view a_reason, double a_unixTimeSeconds)
	{
		Json packet{ { "type", "factionResult" }, { "op", std::string{ a_op } }, { "ok", a_ok }, { "serverTime", a_unixTimeSeconds } };
		if (!a_reason.empty()) packet["reason"] = std::string{ a_reason };
		return packet;
	}

	const FactionPeer* FactionService::PeerForProfile(const Peers& a_peers, const std::string& a_profileId)
	{
		if (a_profileId.empty()) return nullptr;
		const auto found = std::find_if(a_peers.begin(), a_peers.end(), [&a_profileId](const auto& a_peer) { return a_peer.profileId == a_profileId; });
		return found == a_peers.end() ? nullptr : &*found;
	}

	const FactionPeer* FactionService::PeerForPlayer(const Peers& a_peers, std::uint32_t a_playerId)
	{
		const auto found = std::find_if(a_peers.begin(), a_peers.end(), [a_playerId](const auto& a_peer) { return a_peer.playerId == a_playerId; });
		return found == a_peers.end() ? nullptr : &*found;
	}
	const FactionPeer* FactionService::PeerForConnection(const Peers& a_peers, std::uint32_t a_connectionId)
	{
		const auto found = std::find_if(a_peers.begin(), a_peers.end(), [a_connectionId](const auto& a_peer) { return a_peer.connectionId == a_connectionId; });
		return found == a_peers.end() ? nullptr : &*found;
	}

	bool FactionService::FactionMutationAllows(FactionSessionState& a_state, double a_monotonicSeconds)
	{
		return F4T::PacketRateLimit::TakeToken(a_state.mutations, a_monotonicSeconds, limits.mutationRefillPerSecond, limits.mutationCapacity);
	}

	bool FactionService::SameFaction(const std::string& a_profileId, const std::string& a_otherProfileId) const
	{
		const auto mine = factionByProfile.find(a_profileId);
		if (mine == factionByProfile.end()) return false;
		const auto theirs = factionByProfile.find(a_otherProfileId);
		return theirs != factionByProfile.end() && theirs->second == mine->second;
	}

	bool FactionService::VitalsChanged(const FactionVitals& a_now, const PushedVitals& a_pushed, bool& a_flagsChanged)
	{
		if (!a_now.known) return false;
		if (!a_pushed.known) return true;
		if (a_now.downed != a_pushed.downed || a_now.dead != a_pushed.dead) a_flagsChanged = true;
		const auto delta = static_cast<std::int32_t>(a_now.healthPercent) - static_cast<std::int32_t>(a_pushed.healthPercent);
		return std::abs(delta) >= static_cast<std::int32_t>(kFactionVitalsHealthStep);
	}

	void FactionService::ForgetVitals(const std::string& a_profileId)
	{
		pushedVitals.erase(a_profileId);
	}

	Json FactionService::FactionJson(const Peers& a_peers, const Faction& a_faction, const FactionPeer& a_viewer) const
	{
		Json faction{ { "factionId", a_faction.factionId }, { "name", a_faction.name }, { "tag", a_faction.tag }, { "description", a_faction.description }, { "motd", a_faction.motd }, { "color", a_faction.color } };
		if (const auto* leader = OnlineVisibleTo(PeerForProfile(a_peers, a_faction.leaderProfileId), a_viewer)) faction["leaderPlayerId"] = leader->playerId;
		const bool viewerSocialNames = a_viewer.socialNames;
		Json members = Json::array();
		for (const auto& profileId : a_faction.members) {
			const auto* peer = OnlineVisibleTo(PeerForProfile(a_peers, profileId), a_viewer);
			const auto view = PeerNameView(a_viewer.profileId, a_viewer.socialKey, profileId, SocialKeyFor(a_peers, profileId), MemberName(a_faction, profileId));
			Json member{
				{ "displayName", view.displayName },
				{ "online", peer != nullptr },
				{ "leader", profileId == a_faction.leaderProfileId },
				{ "officer", a_faction.officers.contains(profileId) }
			};
			if (view.disclosedName) {
				member["profileId"] = profileId;
				member["name"] = *view.disclosedName;
			}
			// Constraint: legacy clients get display text in name instead of no name.
			else if (!viewerSocialNames) member["name"] = view.displayName;
			if (peer) {
				member["playerId"] = peer->playerId;
				if (peer->vitals.known) {
					member["health"] = peer->vitals.healthPercent;
					member["downed"] = peer->vitals.downed;
					member["dead"] = peer->vitals.dead;
				}
			}
			members.push_back(std::move(member));
		}
		faction["members"] = std::move(members);
		return faction;
	}

	std::string FactionService::SuccessorProfileId(const Peers& a_peers, const Faction& a_faction)
	{
		const auto rank = [&a_peers, &a_faction](const std::string& a_profileId) {
			return (PeerForProfile(a_peers, a_profileId) ? 0 : 2) + (a_faction.officers.contains(a_profileId) ? 0 : 1);
		};
		return *std::min_element(a_faction.members.begin(), a_faction.members.end(), [&rank](const auto& a_left, const auto& a_right) {
			return rank(a_left) < rank(a_right);
		});
	}

	const std::vector<std::string>* FactionService::MembersOf(const std::string& a_profileId) const
	{
		const auto membership = factionByProfile.find(a_profileId);
		return membership == factionByProfile.end() ? nullptr : &factions.at(membership->second).members;
	}

	std::string FactionService::FactionIdOf(const std::string& a_profileId) const
	{
		const auto membership = factionByProfile.find(a_profileId);
		return membership == factionByProfile.end() ? std::string{} : membership->second;
	}

	std::string FactionService::TagForProfile(const std::string& a_profileId) const
	{
		const auto membership = factionByProfile.find(a_profileId);
		return membership == factionByProfile.end() ? std::string{} : factions.at(membership->second).tag;
	}

	FactionService::Faction* FactionService::FactionOfProfile(const std::string& a_profileId)
	{
		const auto membership = factionByProfile.find(a_profileId);
		return membership == factionByProfile.end() ? nullptr : &factions.at(membership->second);
	}

	FactionService::FactionSnapshot FactionService::SnapshotFactions() const
	{
		return FactionSnapshot{ factions, factionByProfile, nextFactionNumber, factionInvites };
	}

	void FactionService::RestoreFactions(FactionSnapshot a_snapshot)
	{
		factions = std::move(a_snapshot.factions);
		factionByProfile = std::move(a_snapshot.factionByProfile);
		nextFactionNumber = a_snapshot.nextFactionNumber;
		factionInvites = std::move(a_snapshot.factionInvites);
	}

	std::vector<OutboundFrame> FactionService::RestoreAndFail(FactionSnapshot a_snapshot, std::uint32_t a_connectionId, std::string_view a_op, double a_unixTimeSeconds)
	{
		RestoreFactions(std::move(a_snapshot));
		return FactionFailure(a_connectionId, a_op, "persistenceUnavailable", a_unixTimeSeconds);
	}

	std::vector<std::string> FactionService::EraseFaction(std::string a_factionId)
	{
		auto notified = factions.at(a_factionId).members;
		for (const auto& profileId : notified) {
			factionByProfile.erase(profileId);
			ForgetVitals(profileId);
		}
		for (const auto& entry : factionInvites) {
			if (entry.second.factionId == a_factionId) notified.push_back(entry.second.targetProfileId);
		}
		factions.erase(a_factionId);
		std::erase_if(factionInvites, [&a_factionId](const auto& a_entry) { return a_entry.second.factionId == a_factionId; });
		factionVitalsPushMonotonic.erase(a_factionId);
		return notified;
	}

	std::vector<std::string> FactionService::EraseInvitesFrom(const std::string& a_profileId)
	{
		std::vector<std::string> invitees;
		for (const auto& [inviteId, invite] : factionInvites) {
			if (invite.inviterProfileId == a_profileId) invitees.push_back(invite.targetProfileId);
		}
		std::erase_if(factionInvites, [&a_profileId](const auto& a_entry) { return a_entry.second.inviterProfileId == a_profileId; });
		return invitees;
	}

	Json FactionService::FactionStatePacket(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds)
	{
		Json faction;
		if (auto* membership = FactionOfProfile(a_viewer.profileId)) {
			if (RefreshFactionNames(a_peers, *membership)) SaveFactions();
			faction = FactionJson(a_peers, *membership, a_viewer);
		}
		return Json{
			{ "type", "factionState" },
			{ "faction", std::move(faction) },
			{ "pendingInvites", PendingInvitesJson(a_peers, a_viewer, a_unixTimeSeconds) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	std::vector<OutboundFrame> FactionService::VitalsFrames(
		const Peers& a_peers,
		const std::string& a_profileId,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto* faction = FactionOfProfile(a_profileId);
		if (!faction) return {};

		bool flagsChanged = false;
		bool healthMoved = false;
		for (const auto& profileId : faction->members) {
			const auto* peer = PeerForProfile(a_peers, profileId);
			const auto now = peer ? peer->vitals : FactionVitals{};
			const auto pushed = pushedVitals.find(profileId);
			if (VitalsChanged(now, pushed == pushedVitals.end() ? PushedVitals{} : pushed->second, flagsChanged)) healthMoved = true;
		}
		if (!flagsChanged && !healthMoved) return {};

		if (!flagsChanged) {
			const auto last = factionVitalsPushMonotonic.find(faction->factionId);
			if (last != factionVitalsPushMonotonic.end() && a_monotonicSeconds - last->second < kFactionVitalsPushSeconds) return {};
		}

		for (const auto& profileId : faction->members) {
			const auto* peer = PeerForProfile(a_peers, profileId);
			pushedVitals[profileId] = peer && peer->vitals.known ?
				PushedVitals{ peer->vitals.healthPercent, peer->vitals.downed, peer->vitals.dead, true } :
				PushedVitals{};
		}
		factionVitalsPushMonotonic[faction->factionId] = a_monotonicSeconds;
		return FactionStateFrames(a_peers, faction->factionId, a_unixTimeSeconds);
	}

	std::string FactionService::SocialKeyFor(const Peers& a_peers, const std::string& a_profileId) const
	{
		// Constraint: an online peer keys by its own social key, even when empty.
		if (const auto* peer = PeerForProfile(a_peers, a_profileId)) return peer->socialKey;
		if (const auto membership = factionByProfile.find(a_profileId); membership != factionByProfile.end()) {
			const auto& keys = factions.at(membership->second).memberSocialKeys;
			const auto stored = keys.find(a_profileId);
			if (stored != keys.end() && SocialKeyValid(stored->second)) return stored->second;
		}
		return a_profileId;
	}

	SocialGraph::NameView FactionService::PeerNameView(
		const std::string& a_viewerProfileId,
		const std::string& a_viewerSocialKey,
		const std::string& a_subjectProfileId,
		const std::string& a_subjectSocialKey,
		std::string_view a_realName) const
	{
		const std::string& viewerKey = a_viewerSocialKey;
		const std::string& subjectKey = a_subjectSocialKey;
		return social.NameViewFor(viewerKey, a_viewerProfileId, subjectKey, a_realName, SameFaction(a_viewerProfileId, a_subjectProfileId));
	}

	Json FactionService::RosterPacketFor(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds) const
	{
		const bool viewerSocialNames = a_viewer.socialNames;
		Json players = Json::array();
		for (const auto& peer : a_peers) {
			if (peer.hidesOnline && peer.profileId != a_viewer.profileId) continue;
			const auto view = PeerNameView(a_viewer.profileId, a_viewer.socialKey, peer.profileId, peer.socialKey, peer.name);
			Json player{ { "playerId", peer.playerId }, { "displayName", view.displayName }, { "tag", TagForProfile(peer.profileId) } };
			if (view.disclosedName) player["name"] = *view.disclosedName;
			// Constraint: legacy clients get display text in name instead of no name.
			else if (!viewerSocialNames) player["name"] = view.displayName;
			players.push_back(std::move(player));
		}
		return Json{ { "type", "playerRoster" }, { "players", std::move(players) }, { "serverTime", a_unixTimeSeconds } };
	}

	std::vector<OutboundFrame> FactionService::RejectMalformed()
	{
		stats.packetsRejected++;
		stats.factionPacketsRejected++;
		return {};
	}

	std::vector<OutboundFrame> FactionService::FactionFailure(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, double a_unixTimeSeconds)
	{
		stats.factionPacketsRejected++;
		return { ReliableFrame(a_connectionId, FactionResultPacket(a_op, false, a_reason, a_unixTimeSeconds)) };
	}

	std::vector<OutboundFrame> FactionService::ProfileStateFrames(const Peers& a_peers, const std::vector<std::string>& a_profileIds, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& profileId : a_profileIds) {
			if (const auto* peer = PeerForProfile(a_peers, profileId)) {
				frames.push_back(ReliableFrame(peer->connectionId, FactionStatePacket(a_peers, *peer, a_unixTimeSeconds)));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> FactionService::FactionStateFrames(const Peers& a_peers, const std::string& a_factionId, double a_unixTimeSeconds)
	{
		const auto faction = factions.find(a_factionId);
		if (faction == factions.end()) return {};
		return ProfileStateFrames(a_peers, faction->second.members, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> FactionService::HandleFactionCreate(
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
		auto normalized = F4T::FactionValidation::NormalizeFactionCreate(a_packet);
		if (!normalized) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "create", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto name = F4T::FactionValidation::NormalizeFactionName(ReadString(*normalized, "name").value_or(""));
		if (!name || !F4T::FactionValidation::IsFactionTextAllowed(*name) || FactionNameKey(*name).empty()) return FactionFailure(connectionId, "create", "invalidName", a_unixTimeSeconds);
		const auto tag = F4T::FactionValidation::NormalizeFactionTag(ReadString(*normalized, "tag").value_or(""));
		if (!tag || !F4T::FactionValidation::IsFactionTextAllowed(*tag)) return FactionFailure(connectionId, "create", "invalidTag", a_unixTimeSeconds);
		if (a_self.profileId.empty()) return FactionFailure(connectionId, "create", "identityUnavailable", a_unixTimeSeconds);
		if (factionByProfile.contains(a_self.profileId)) return FactionFailure(connectionId, "create", "alreadyInFaction", a_unixTimeSeconds);
		if (FactionNameTaken(*name)) return FactionFailure(connectionId, "create", "nameTaken", a_unixTimeSeconds);
		if (FactionTagTaken(*tag)) return FactionFailure(connectionId, "create", "tagTaken", a_unixTimeSeconds);
		if (factions.size() >= limits.maximumFactions) return FactionFailure(connectionId, "create", "factionCapacity", a_unixTimeSeconds);

		const auto snapshot = SnapshotFactions();
		while (factions.contains("f-" + std::to_string(nextFactionNumber)) || quarantinedFactions.contains("f-" + std::to_string(nextFactionNumber))) nextFactionNumber = nextFactionNumber % kMaximumFactionNumber + 1;
		const auto factionId = "f-" + std::to_string(nextFactionNumber);
		nextFactionNumber = nextFactionNumber % kMaximumFactionNumber + 1;
		factions.emplace(factionId, Faction{ factionId, *name, *tag, a_self.profileId, { a_self.profileId }, {}, {}, a_unixTimeSeconds });
		factionByProfile[a_self.profileId] = factionId;
		RefreshFactionNames(a_peers, factions.at(factionId));
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "create", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("create", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionLeave(
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
		if (!F4T::FactionValidation::NormalizeFactionLeave(a_packet)) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "leave", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto membership = factionByProfile.find(a_self.profileId);
		if (membership == factionByProfile.end()) return FactionFailure(connectionId, "leave", "notMember", a_unixTimeSeconds);
		const auto snapshot = SnapshotFactions();
		const auto factionId = membership->second;
		factionByProfile.erase(membership);
		auto& faction = factions.at(factionId);
		std::erase(faction.members, a_self.profileId);
		ForgetVitals(a_self.profileId);
		faction.officers.erase(a_self.profileId);
		faction.memberNames.erase(a_self.profileId);
		faction.memberSocialKeys.erase(a_self.profileId);
		const auto invitees = EraseInvitesFrom(a_self.profileId);
		std::vector<OutboundFrame> frames{
			ReliableFrame(connectionId, FactionResultPacket("leave", true, {}, a_unixTimeSeconds)),
			ReliableFrame(connectionId, FactionStatePacket(a_peers, a_self, a_unixTimeSeconds))
		};
		if (faction.members.empty()) {
			AppendFrames(frames, ProfileStateFrames(a_peers, EraseFaction(factionId), a_unixTimeSeconds));
		} else {
			if (faction.leaderProfileId == a_self.profileId) {
				faction.leaderProfileId = SuccessorProfileId(a_peers, faction);
				faction.officers.erase(faction.leaderProfileId);
			}
			AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		}
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "leave", a_unixTimeSeconds);
		AppendFrames(frames, ProfileStateFrames(a_peers, invitees, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionKick(
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
		auto normalized = F4T::FactionValidation::NormalizeFactionKick(a_packet);
		if (!normalized) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "kick", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto targetProfileId = ReadString(*normalized, "targetProfileId").value_or("");
		const auto faction = FactionOfProfile(a_self.profileId);
		const auto leader = faction && faction->leaderProfileId == a_self.profileId;
		if (!faction || (!leader && !faction->officers.contains(a_self.profileId))) return FactionFailure(connectionId, "kick", "notLeader", a_unixTimeSeconds);
		if (targetProfileId == a_self.profileId) return FactionFailure(connectionId, "kick", "invalidTarget", a_unixTimeSeconds);
		if (!leader && (targetProfileId == faction->leaderProfileId || faction->officers.contains(targetProfileId))) return FactionFailure(connectionId, "kick", "notLeader", a_unixTimeSeconds);
		if (std::find(faction->members.begin(), faction->members.end(), targetProfileId) == faction->members.end()) {
			return FactionFailure(connectionId, "kick", "invalidTarget", a_unixTimeSeconds);
		}
		const auto snapshot = SnapshotFactions();
		const auto factionId = faction->factionId;
		std::erase(faction->members, targetProfileId);
		ForgetVitals(targetProfileId);
		faction->memberNames.erase(targetProfileId);
		faction->memberSocialKeys.erase(targetProfileId);
		faction->officers.erase(targetProfileId);
		factionByProfile.erase(targetProfileId);
		std::vector<std::string> invitees;
		for (const auto& [inviteId, invite] : factionInvites) {
			if (invite.inviterProfileId == targetProfileId) invitees.push_back(invite.targetProfileId);
		}
		std::erase_if(factionInvites, [&targetProfileId](const auto& a_entry) { return a_entry.second.inviterProfileId == targetProfileId; });
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "kick", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("kick", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		AppendFrames(frames, ProfileStateFrames(a_peers, invitees, a_unixTimeSeconds));
		if (const auto* target = PeerForProfile(a_peers, targetProfileId)) {
			frames.push_back(ReliableFrame(target->connectionId, FactionResultPacket("kick", false, "kicked", a_unixTimeSeconds)));
			frames.push_back(ReliableFrame(target->connectionId, FactionStatePacket(a_peers, *target, a_unixTimeSeconds)));
		}
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionDisband(
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
		if (!F4T::FactionValidation::NormalizeFactionDisband(a_packet)) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "disband", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto faction = FactionOfProfile(a_self.profileId);
		if (!faction || faction->leaderProfileId != a_self.profileId) return FactionFailure(connectionId, "disband", "notLeader", a_unixTimeSeconds);
		const auto snapshot = SnapshotFactions();
		const auto notified = EraseFaction(faction->factionId);
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "disband", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("disband", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, ProfileStateFrames(a_peers, notified, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionPromote(
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
		auto normalized = F4T::FactionValidation::NormalizeFactionPromote(a_packet);
		if (!normalized) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "promote", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto targetProfileId = ReadString(*normalized, "targetProfileId").value_or("");
		const auto faction = FactionOfProfile(a_self.profileId);
		if (!faction || faction->leaderProfileId != a_self.profileId) return FactionFailure(connectionId, "promote", "notLeader", a_unixTimeSeconds);
		if (targetProfileId == a_self.profileId) return FactionFailure(connectionId, "promote", "invalidTarget", a_unixTimeSeconds);
		if (std::find(faction->members.begin(), faction->members.end(), targetProfileId) == faction->members.end()) {
			return FactionFailure(connectionId, "promote", "invalidTarget", a_unixTimeSeconds);
		}
		const auto snapshot = SnapshotFactions();
		const auto factionId = faction->factionId;
		faction->leaderProfileId = targetProfileId;
		faction->officers.erase(targetProfileId);
		const auto invitees = EraseInvitesFrom(a_self.profileId);
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "promote", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("promote", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		AppendFrames(frames, ProfileStateFrames(a_peers, invitees, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionSetRank(
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
		auto normalized = F4T::FactionValidation::NormalizeFactionSetRank(a_packet);
		if (!normalized) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "setRank", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		const auto targetProfileId = ReadString(*normalized, "targetProfileId").value_or("");
		const auto officer = ReadString(*normalized, "rank").value_or("") == "officer";
		const auto faction = FactionOfProfile(a_self.profileId);
		if (!faction || faction->leaderProfileId != a_self.profileId) return FactionFailure(connectionId, "setRank", "notLeader", a_unixTimeSeconds);
		if (targetProfileId == a_self.profileId) return FactionFailure(connectionId, "setRank", "invalidTarget", a_unixTimeSeconds);
		if (std::find(faction->members.begin(), faction->members.end(), targetProfileId) == faction->members.end()) {
			return FactionFailure(connectionId, "setRank", "invalidTarget", a_unixTimeSeconds);
		}
		const auto snapshot = SnapshotFactions();
		const auto factionId = faction->factionId;
		std::vector<std::string> invitees;
		if (officer) {
			faction->officers.insert(targetProfileId);
		} else {
			faction->officers.erase(targetProfileId);
			invitees = EraseInvitesFrom(targetProfileId);
		}
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "setRank", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("setRank", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		AppendFrames(frames, ProfileStateFrames(a_peers, invitees, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionSetProfile(
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
		auto normalized = F4T::FactionValidation::NormalizeFactionSetProfile(a_packet);
		if (!normalized) return RejectMalformed();
		if (auto refused = MutationRefused(a_self, a_state, a_peers, "setProfile", a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*refused);
		if (!F4T::FactionValidation::IsFactionTextAllowed(ReadString(*normalized, "description").value_or(""))) return FactionFailure(connectionId, "setProfile", "invalidDescription", a_unixTimeSeconds);
		if (!F4T::FactionValidation::IsFactionTextAllowed(ReadString(*normalized, "motd").value_or(""))) return FactionFailure(connectionId, "setProfile", "invalidMotd", a_unixTimeSeconds);
		const auto faction = FactionOfProfile(a_self.profileId);
		if (!faction || faction->leaderProfileId != a_self.profileId) return FactionFailure(connectionId, "setProfile", "notLeader", a_unixTimeSeconds);
		const auto snapshot = SnapshotFactions();
		const auto factionId = faction->factionId;
		faction->description = ReadString(*normalized, "description").value_or("");
		if (const auto motd = ReadString(*normalized, "motd")) faction->motd = *motd;
		faction->color = ReadString(*normalized, "color").value_or("");
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "setProfile", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("setProfile", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::HandleFactionRosterRequest(
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		stats.factionPacketsReceived++;
		const auto last = a_state.lastRosterRequestMonotonic;
		if (!F4T::FactionValidation::NormalizePlayerRosterRequest(a_packet) ||
			(last && !(a_monotonicSeconds - *last >= limits.rosterRequestIntervalSeconds))) {
			return RejectMalformed();
		}
		a_state.lastRosterRequestMonotonic = a_monotonicSeconds;
		return { ReliableFrame(a_self.connectionId, RosterPacketFor(a_peers, a_self, a_unixTimeSeconds)) };
	}

	std::vector<OutboundFrame> FactionService::HandleFactionList(
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		stats.factionPacketsReceived++;
		const auto last = a_state.lastFactionListRequestMonotonic;
		const auto normalized = F4T::FactionValidation::NormalizeFactionListRequest(a_packet);
		if (!normalized || (last && !(a_monotonicSeconds - *last >= limits.listRequestIntervalSeconds))) {
			return RejectMalformed();
		}
		a_state.lastFactionListRequestMonotonic = a_monotonicSeconds;
		std::optional<std::size_t> offset;
		if (normalized->contains("offset")) offset = normalized->at("offset").get<std::size_t>();
		return { ReliableFrame(a_self.connectionId, FactionListPacket(a_peers, a_self, offset)) };
	}

	std::optional<std::vector<OutboundFrame>> FactionService::Dispatch(
		std::string_view a_type,
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const std::function<bool()>& a_allow)
	{
		if (!enabled) return std::nullopt;
		std::optional<std::vector<OutboundFrame>> frames;
		{
			MutationAllowScope scope{ mutationAllow, a_allow };
			frames = DispatchType(a_type, a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (frames) AppendFrames(*frames, InviteExpiryFrames(a_peers, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FactionService::PollInviteExpiry(const Peers& a_peers, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!enabled) return {};
		ExpireFactionInvites(a_monotonicSeconds);
		return InviteExpiryFrames(a_peers, a_unixTimeSeconds);
	}

	std::optional<std::vector<OutboundFrame>> FactionService::DispatchType(
		std::string_view a_type,
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		if (a_type == "playerRosterRequest") {
			return HandleFactionRosterRequest(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionList") {
			return HandleFactionList(a_self, a_state, a_peers, a_packet, a_monotonicSeconds);
		}
		if (a_type == "factionCreate") {
			return HandleFactionCreate(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionInvite") {
			return HandleFactionInvite(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionInviteResponse") {
			return HandleFactionInviteResponse(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionLeave") {
			return HandleFactionLeave(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionKick") {
			return HandleFactionKick(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionDisband") {
			return HandleFactionDisband(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionPromote") {
			return HandleFactionPromote(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionSetRank") {
			return HandleFactionSetRank(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "factionSetProfile") {
			return HandleFactionSetProfile(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		return std::nullopt;
	}

	std::vector<OutboundFrame> FactionService::ExistingFrames(const Peers& a_peers, std::uint32_t a_connectionId, const std::string& a_profileId, double a_unixTimeSeconds)
	{
		if (!enabled) {
			return {};
		}
		auto frames = RosterFrames(a_peers, a_unixTimeSeconds);
		if (const auto faction = FactionOfProfile(a_profileId)) {
			AppendFrames(frames, FactionStateFrames(a_peers, faction->factionId, a_unixTimeSeconds));
		} else if (const auto* viewer = PeerForConnection(a_peers, a_connectionId)) {
			frames.push_back(ReliableFrame(a_connectionId, FactionStatePacket(a_peers, *viewer, a_unixTimeSeconds)));
		}
		return frames;
	}

	std::optional<std::vector<OutboundFrame>> FactionService::MutationRefused(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, std::string_view a_op, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(a_self.connectionId, a_op, "rateLimited", a_unixTimeSeconds);
		if (mutationAllow == nullptr || !*mutationAllow || (*mutationAllow)()) return std::nullopt;
		stats.packetsRejected++;
		auto frames = FactionFailure(a_self.connectionId, a_op, "vetoed", a_unixTimeSeconds);
		frames.push_back(ReliableFrame(a_self.connectionId, FactionStatePacket(a_peers, a_self, a_unixTimeSeconds)));
		return frames;
	}

	bool FactionService::RosterNameChanged(std::string_view a_name, FactionSessionState& a_state, double a_monotonicSeconds)
	{
		if (!a_state.lastRosterName) {
			a_state.lastRosterName = std::string{ a_name };
			a_state.lastRosterPushMonotonic = a_monotonicSeconds;
			return false;
		}
		if (*a_state.lastRosterName == a_name || a_monotonicSeconds - *a_state.lastRosterPushMonotonic < limits.nameRosterIntervalSeconds) return false;
		a_state.lastRosterName = std::string{ a_name };
		a_state.lastRosterPushMonotonic = a_monotonicSeconds;
		return true;
	}

	std::vector<OutboundFrame> FactionService::RosterFrames(const Peers& a_peers, double a_unixTimeSeconds) const
	{
		std::vector<OutboundFrame> frames;
		for (const auto& peer : a_peers) {
			frames.push_back(ReliableFrame(peer.connectionId, RosterPacketFor(a_peers, peer, a_unixTimeSeconds)));
		}
		return frames;
	}

	std::vector<OutboundFrame> FactionService::NameListFrames(const Peers& a_peers, const std::vector<std::uint32_t>& a_connectionIds, double a_unixTimeSeconds) const
	{
		std::vector<OutboundFrame> frames;
		if (!enabled) return frames;
		// Constraint: only the connections named here may receive a name-bearing list refresh; others never see it.
		for (const auto& peer : a_peers) {
			if (std::find(a_connectionIds.begin(), a_connectionIds.end(), peer.connectionId) == a_connectionIds.end()) continue;
			frames.push_back(ReliableFrame(peer.connectionId, RosterPacketFor(a_peers, peer, a_unixTimeSeconds)));
			frames.push_back(ReliableFrame(peer.connectionId, FactionListPacket(a_peers, peer)));
		}
		return frames;
	}

	std::vector<OutboundFrame> FactionService::DisconnectFrames(const Peers& a_peers, const std::string& a_profileId, double a_unixTimeSeconds)
	{
		if (!enabled) {
			return {};
		}
		auto frames = RosterFrames(a_peers, a_unixTimeSeconds);
		if (const auto faction = FactionOfProfile(a_profileId)) {
			AppendFrames(frames, FactionStateFrames(a_peers, faction->factionId, a_unixTimeSeconds));
		}
		return frames;
	}
}
