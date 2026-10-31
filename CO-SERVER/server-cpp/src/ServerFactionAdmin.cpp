#include "ServerFactions.h"

#include <algorithm>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	FactionService::Json FactionService::AdminList() const
	{
		Json list = Json::array();
		for (const auto& [factionId, faction] : factions) {
			list.push_back(Json{
				{ "factionId", factionId },
				{ "name", faction.name },
				{ "tag", faction.tag },
				{ "description", faction.description },
				{ "motd", faction.motd },
				{ "color", faction.color },
				{ "createdAt", faction.createdAt },
				{ "leaderProfileId", faction.leaderProfileId },
				{ "members", faction.members },
				{ "memberNames", faction.memberNames },
				{ "officers", faction.officers },
				{ "memberCount", faction.members.size() }
			});
		}
		return Json{ { "factions", std::move(list) }, { "enabled", enabled } };
	}

	std::vector<OutboundFrame> FactionService::AdminDisband(const Peers& a_peers, std::string_view a_factionId, double a_unixTimeSeconds, std::string& a_failure)
	{
		const auto factionId = std::string{ a_factionId };
		if (!enabled) {
			a_failure = "factionsDisabled";
			return {};
		}
		if (!factions.contains(factionId)) {
			a_failure = "factionNotFound";
			return {};
		}
		const auto snapshot = SnapshotFactions();
		const auto notified = EraseFaction(factionId);
		if (!SaveFactions()) {
			RestoreFactions(snapshot);
			a_failure = "persistenceUnavailable";
			return {};
		}
		return ProfileStateFrames(a_peers, notified, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> FactionService::AdminPromote(const Peers& a_peers, std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure)
	{
		const auto factionId = std::string{ a_factionId };
		const auto targetProfileId = std::string{ a_targetProfileId };
		if (!enabled) {
			a_failure = "factionsDisabled";
			return {};
		}
		if (!factions.contains(factionId)) {
			a_failure = "factionNotFound";
			return {};
		}
		auto& faction = factions.at(factionId);
		if (targetProfileId == faction.leaderProfileId) {
			a_failure = "alreadyLeader";
			return {};
		}
		if (std::find(faction.members.begin(), faction.members.end(), targetProfileId) == faction.members.end()) {
			a_failure = "invalidTarget";
			return {};
		}
		const auto snapshot = SnapshotFactions();
		faction.leaderProfileId = targetProfileId;
		faction.officers.erase(targetProfileId);
		if (!SaveFactions()) {
			RestoreFactions(snapshot);
			a_failure = "persistenceUnavailable";
			return {};
		}
		return FactionStateFrames(a_peers, factionId, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> FactionService::AdminKick(const Peers& a_peers, std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure)
	{
		const auto factionId = std::string{ a_factionId };
		const auto targetProfileId = std::string{ a_targetProfileId };
		if (!enabled) {
			a_failure = "factionsDisabled";
			return {};
		}
		if (!factions.contains(factionId)) {
			a_failure = "factionNotFound";
			return {};
		}
		auto& faction = factions.at(factionId);
		if (targetProfileId == faction.leaderProfileId) {
			a_failure = "leaderKick";
			return {};
		}
		if (std::find(faction.members.begin(), faction.members.end(), targetProfileId) == faction.members.end()) {
			a_failure = "invalidTarget";
			return {};
		}
		const auto snapshot = SnapshotFactions();
		std::erase(faction.members, targetProfileId);
		faction.memberNames.erase(targetProfileId);
		faction.officers.erase(targetProfileId);
		factionByProfile.erase(targetProfileId);
		std::vector<std::string> invitees;
		for (const auto& [inviteId, invite] : factionInvites) {
			if (invite.inviterProfileId == targetProfileId && invite.expiresAtUnixSeconds > a_unixTimeSeconds) invitees.push_back(invite.targetProfileId);
		}
		std::erase_if(factionInvites, [&targetProfileId](const auto& a_entry) { return a_entry.second.inviterProfileId == targetProfileId; });
		if (!SaveFactions()) {
			RestoreFactions(snapshot);
			a_failure = "persistenceUnavailable";
			return {};
		}
		std::vector<OutboundFrame> frames = FactionStateFrames(a_peers, factionId, a_unixTimeSeconds);
		AppendFrames(frames, ProfileStateFrames(a_peers, invitees, a_unixTimeSeconds));
		if (const auto* target = PeerForProfile(a_peers, targetProfileId)) {
			frames.push_back(ReliableFrame(target->connectionId, FactionResultPacket("kick", false, "kicked", a_unixTimeSeconds)));
			frames.push_back(ReliableFrame(target->connectionId, FactionStatePacket(a_peers, *target, a_unixTimeSeconds)));
		}
		return frames;
	}
}
