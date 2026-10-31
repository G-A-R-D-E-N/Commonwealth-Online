#include "ServerFactions.h"

#include "ServerFactionRules.h"
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

	std::string FactionService::MemberName(const Faction& a_faction, const std::string& a_profileId)
	{
		const auto found = a_faction.memberNames.find(a_profileId);
		return found == a_faction.memberNames.end() || found->second.empty() ? "Unknown" : found->second;
	}

	bool FactionService::RefreshFactionNames(const Peers& a_peers, Faction& a_faction)
	{
		bool changed = false;
		for (const auto& profileId : a_faction.members) {
			const auto* peer = PeerForProfile(a_peers, profileId);
			if (peer == nullptr) continue;
			const auto found = a_faction.memberNames.find(profileId);
			if (found != a_faction.memberNames.end() && found->second == peer->name) continue;
			a_faction.memberNames[profileId] = peer->name;
			changed = true;
		}
		return changed;
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

	Json FactionService::FactionJson(const Peers& a_peers, const Faction& a_faction)
	{
		Json faction{ { "factionId", a_faction.factionId }, { "name", a_faction.name }, { "tag", a_faction.tag }, { "description", a_faction.description }, { "motd", a_faction.motd }, { "color", a_faction.color } };
		if (const auto* leader = PeerForProfile(a_peers, a_faction.leaderProfileId)) faction["leaderPlayerId"] = leader->playerId;
		Json members = Json::array();
		for (const auto& profileId : a_faction.members) {
			const auto* peer = PeerForProfile(a_peers, profileId);
			Json member{
				{ "profileId", profileId },
				{ "name", MemberName(a_faction, profileId) },
				{ "online", peer != nullptr },
				{ "leader", profileId == a_faction.leaderProfileId },
				{ "officer", a_faction.officers.contains(profileId) }
			};
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

	FactionService::Faction* FactionService::FactionOfProfile(const std::string& a_profileId)
	{
		const auto membership = factionByProfile.find(a_profileId);
		return membership == factionByProfile.end() ? nullptr : &factions.at(membership->second);
	}

	bool FactionService::FactionNameTaken(const std::string& a_name) const
	{
		const auto wanted = FactionNameKey(a_name);
		return std::any_of(factions.begin(), factions.end(), [&wanted](const auto& a_entry) {
			return FactionNameKey(a_entry.second.name) == wanted;
		});
	}

	bool FactionService::FactionTagTaken(const std::string& a_tag) const
	{
		return !a_tag.empty() && std::any_of(factions.begin(), factions.end(), [&a_tag](const auto& a_entry) {
			return a_entry.second.tag == a_tag;
		});
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

	Json FactionService::FactionStatePacket(const Peers& a_peers, const std::string& a_profileId, double a_unixTimeSeconds)
	{
		Json faction;
		if (auto* membership = FactionOfProfile(a_profileId)) {
			if (RefreshFactionNames(a_peers, *membership)) SaveFactions();
			faction = FactionJson(a_peers, *membership);
		}
		return Json{
			{ "type", "factionState" },
			{ "faction", std::move(faction) },
			{ "pendingInvites", PendingInvitesJson(a_profileId, a_unixTimeSeconds) },
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

	Json FactionService::RosterPacket(const Peers& a_peers, double a_unixTimeSeconds) const
	{
		Json players = Json::array();
		for (const auto& peer : a_peers) {
			std::string tag;
			if (const auto membership = factionByProfile.find(peer.profileId); membership != factionByProfile.end()) {
				tag = factions.at(membership->second).tag;
			}
			players.push_back(Json{ { "playerId", peer.playerId }, { "name", peer.name }, { "tag", std::move(tag) } });
		}
		return Json{ { "type", "playerRoster" }, { "players", std::move(players) }, { "serverTime", a_unixTimeSeconds } };
	}

	Json FactionService::FactionListPacket(const Peers& a_peers, const std::string& a_profileId) const
	{
		struct Row
		{
			const Faction* faction;
			std::uint32_t online;
			std::string folded;
		};
		std::vector<Row> rows;
		rows.reserve(factions.size());
		for (const auto& entry : factions) {
			std::uint32_t online{};
			for (const auto& profileId : entry.second.members) online += PeerForProfile(a_peers, profileId) ? 1U : 0U;
			rows.push_back(Row{ &entry.second, online, LowerAscii(entry.second.name) });
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a_left, const Row& a_right) {
			if (a_left.online != a_right.online) return a_left.online > a_right.online;
			return a_left.folded < a_right.folded;
		});
		const auto ownFactionId = FactionIdOf(a_profileId);
		if (rows.size() > kMaximumFactionListEntries) {
			const auto own = std::find_if(rows.begin(), rows.end(), [&ownFactionId](const Row& a_row) { return a_row.faction->factionId == ownFactionId; });
			if (own != rows.end() && static_cast<std::size_t>(own - rows.begin()) >= kMaximumFactionListEntries) rows[kMaximumFactionListEntries - 1] = *own;
			rows.resize(kMaximumFactionListEntries);
		}
		Json list = Json::array();
		for (const auto& row : rows) {
			const auto& faction = *row.faction;
			const auto* leader = PeerForProfile(a_peers, faction.leaderProfileId);
			list.push_back(Json{
				{ "id", faction.factionId },
				{ "name", faction.name },
				{ "tag", faction.tag },
				{ "color", faction.color },
				{ "description", faction.description },
				{ "leader", leader ? leader->name : MemberName(faction, faction.leaderProfileId) },
				{ "memberCount", faction.members.size() },
				{ "onlineCount", row.online },
				{ "yours", faction.factionId == ownFactionId }
			});
		}
		return Json{ { "type", "factionList" }, { "factions", std::move(list) } };
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
				frames.push_back(ReliableFrame(peer->connectionId, FactionStatePacket(a_peers, profileId, a_unixTimeSeconds)));
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "create", "rateLimited", a_unixTimeSeconds);
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
		factions.emplace(factionId, Faction{ factionId, *name, *tag, a_self.profileId, { a_self.profileId }, {}, a_unixTimeSeconds });
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "leave", "rateLimited", a_unixTimeSeconds);
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
		std::vector<OutboundFrame> frames{
			ReliableFrame(connectionId, FactionResultPacket("leave", true, {}, a_unixTimeSeconds)),
			ReliableFrame(connectionId, FactionStatePacket(a_peers, a_self.profileId, a_unixTimeSeconds))
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "kick", "rateLimited", a_unixTimeSeconds);
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
			frames.push_back(ReliableFrame(target->connectionId, FactionStatePacket(a_peers, targetProfileId, a_unixTimeSeconds)));
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "disband", "rateLimited", a_unixTimeSeconds);
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "promote", "rateLimited", a_unixTimeSeconds);
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
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "promote", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("promote", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "setRank", "rateLimited", a_unixTimeSeconds);
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
		if (officer) {
			faction->officers.insert(targetProfileId);
		} else {
			faction->officers.erase(targetProfileId);
		}
		if (!SaveFactions()) return RestoreAndFail(snapshot, connectionId, "setRank", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames{ ReliableFrame(connectionId, FactionResultPacket("setRank", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, FactionStateFrames(a_peers, factionId, a_unixTimeSeconds));
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
		if (!FactionMutationAllows(a_state, a_monotonicSeconds)) return FactionFailure(connectionId, "setProfile", "rateLimited", a_unixTimeSeconds);
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
		return { ReliableFrame(a_self.connectionId, RosterPacket(a_peers, a_unixTimeSeconds)) };
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
		if (!F4T::FactionValidation::NormalizeFactionListRequest(a_packet) ||
			(last && !(a_monotonicSeconds - *last >= limits.listRequestIntervalSeconds))) {
			return RejectMalformed();
		}
		a_state.lastFactionListRequestMonotonic = a_monotonicSeconds;
		return { ReliableFrame(a_self.connectionId, FactionListPacket(a_peers, a_self.profileId)) };
	}

	std::optional<std::vector<OutboundFrame>> FactionService::Dispatch(
		std::string_view a_type,
		const FactionPeer& a_self,
		FactionSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		if (!enabled) return std::nullopt;
		auto frames = DispatchType(a_type, a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
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
		} else {
			frames.push_back(ReliableFrame(a_connectionId, FactionStatePacket(a_peers, a_profileId, a_unixTimeSeconds)));
		}
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
		const auto roster = RosterPacket(a_peers, a_unixTimeSeconds);
		std::vector<OutboundFrame> frames;
		for (const auto& peer : a_peers) frames.push_back(ReliableFrame(peer.connectionId, roster));
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
