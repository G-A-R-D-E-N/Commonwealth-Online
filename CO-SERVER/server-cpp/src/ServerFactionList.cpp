#include "ServerFactions.h"

#include "ServerFactionRules.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace FactionRules;

		const FactionPeer* VisiblePeer(const FactionService::Peers& a_peers, const FactionPeer& a_viewer, const std::string& a_profileId)
		{
			for (const auto& peer : a_peers) {
				if (peer.profileId == a_profileId) return peer.hidesOnline && peer.profileId != a_viewer.profileId ? nullptr : &peer;
			}
			return nullptr;
		}
	}

	Json FactionService::FactionListPacket(const Peers& a_peers, const FactionPeer& a_viewer) const
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
			for (const auto& profileId : entry.second.members) online += VisiblePeer(a_peers, a_viewer, profileId) ? 1U : 0U;
			rows.push_back(Row{ &entry.second, online, LowerAscii(entry.second.name) });
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a_left, const Row& a_right) {
			if (a_left.online != a_right.online) return a_left.online > a_right.online;
			return a_left.folded < a_right.folded;
		});
		const auto ownFactionId = FactionIdOf(a_viewer.profileId);
		if (rows.size() > kMaximumFactionListEntries) {
			const auto own = std::find_if(rows.begin(), rows.end(), [&ownFactionId](const Row& a_row) { return a_row.faction->factionId == ownFactionId; });
			if (own != rows.end() && static_cast<std::size_t>(own - rows.begin()) >= kMaximumFactionListEntries) rows[kMaximumFactionListEntries - 1] = *own;
			rows.resize(kMaximumFactionListEntries);
		}
		Json list = Json::array();
		for (const auto& row : rows) {
			const auto& faction = *row.faction;
			const auto* leader = VisiblePeer(a_peers, a_viewer, faction.leaderProfileId);
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
		return Json{ { "type", "factionList" }, { "factions", std::move(list) } };
	}
}
