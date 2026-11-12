#include "ServerFactions.h"

#include "ServerFactionRules.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace FactionRules;
	}

	Json FactionService::FactionListPacket(const Peers& a_peers, const FactionPeer& a_viewer, std::optional<std::size_t> a_offset) const
	{
		struct Row
		{
			const Faction* faction;
			std::uint32_t online;
			std::string folded;
		};
		std::unordered_map<std::string, const FactionPeer*> peerByProfile;
		peerByProfile.reserve(a_peers.size());
		for (const auto& peer : a_peers) peerByProfile.emplace(peer.profileId, &peer);
		const auto visible = [&peerByProfile, &a_viewer](const std::string& a_profileId) -> const FactionPeer* {
			const auto found = peerByProfile.find(a_profileId);
			if (found == peerByProfile.end()) return nullptr;
			return found->second->hidesOnline && found->second->profileId != a_viewer.profileId ? nullptr : found->second;
		};
		std::vector<Row> rows;
		rows.reserve(factions.size());
		for (const auto& entry : factions) {
			std::uint32_t online{};
			for (const auto& profileId : entry.second.members) online += visible(profileId) ? 1U : 0U;
			rows.push_back(Row{ &entry.second, online, LowerAscii(entry.second.name) });
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a_left, const Row& a_right) {
			if (a_left.online != a_right.online) return a_left.online > a_right.online;
			if (a_left.folded != a_right.folded) return a_left.folded < a_right.folded;
			return a_left.faction->factionId < a_right.faction->factionId;
		});
		const auto ownFactionId = FactionIdOf(a_viewer.profileId);
		const auto total = rows.size();
		const auto own = std::find_if(rows.begin(), rows.end(), [&ownFactionId](const Row& a_row) { return a_row.faction->factionId == ownFactionId; });
		const bool ownPastCap = own != rows.end() && static_cast<std::size_t>(own - rows.begin()) >= kMaximumFactionListEntries;
		if (a_offset) {
			if (ownPastCap) std::rotate(rows.begin() + static_cast<std::ptrdiff_t>(kMaximumFactionListEntries - 1), own, own + 1);
			rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>((std::min)(*a_offset, total)));
		} else if (ownPastCap) {
			rows[kMaximumFactionListEntries - 1] = *own;
		}
		if (rows.size() > kMaximumFactionListEntries) rows.resize(kMaximumFactionListEntries);
		Json list = Json::array();
		for (const auto& row : rows) {
			const auto& faction = *row.faction;
			const auto* leader = visible(faction.leaderProfileId);
			// Constraint: leader is display text (Stranger unless entitled), never the raw name.
			const auto leaderView = PeerNameView(
				a_viewer.profileId, a_viewer.socialKey, faction.leaderProfileId, SocialKeyFor(a_peers, faction.leaderProfileId), leader ? leader->name : MemberName(faction, faction.leaderProfileId));
			list.push_back(Json{
				{ "id", faction.factionId },
				{ "name", faction.name },
				{ "tag", faction.tag },
				{ "color", faction.color },
				{ "description", faction.description },
				{ "leader", leaderView.displayName },
				{ "memberCount", faction.members.size() },
				{ "onlineCount", row.online },
				{ "yours", faction.factionId == ownFactionId }
			});
		}
		return Json{ { "type", "factionList" }, { "factions", std::move(list) }, { "total", total }, { "offset", a_offset.value_or(0) } };
	}
}
