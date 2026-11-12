#include "ServerFactions.h"

#include "ServerFactionRules.h"

#include <algorithm>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using namespace FactionRules;
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
			if (found == a_faction.memberNames.end() || found->second != peer->name) {
				a_faction.memberNames[profileId] = peer->name;
				changed = true;
			}
			const auto key = a_faction.memberSocialKeys.find(profileId);
			if (!SocialKeyValid(peer->socialKey)) {
				if (key != a_faction.memberSocialKeys.end()) {
					a_faction.memberSocialKeys.erase(key);
					changed = true;
				}
			} else if (key == a_faction.memberSocialKeys.end() || key->second != peer->socialKey) {
				a_faction.memberSocialKeys[profileId] = peer->socialKey;
				changed = true;
			}
		}
		return changed;
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
}
