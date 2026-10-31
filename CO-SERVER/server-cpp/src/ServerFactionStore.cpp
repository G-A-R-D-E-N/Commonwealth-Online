#include "ServerFactions.h"

#include "PersistenceGate.h"
#include "ServerFactionRules.h"
#include "ServerPacketRead.h"

#include "F4TFactionValidation.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

namespace CommonwealthOnline::Server
{
	using Json = nlohmann::json;
	using namespace FactionRules;

	bool FactionService::Enable(PersistenceGate* a_persistence)
	{
		if (enabled) return false;
		if (a_persistence != nullptr) LoadFactions(a_persistence->ReadStoredFactions());
		persistence = a_persistence;
		enabled = true;
		return true;
	}

	bool FactionService::Disable()
	{
		if (!enabled) return false;
		enabled = false;
		factionInvites.clear();
		return true;
	}

	bool FactionService::SaveNow()
	{
		if (!enabled) return false;
		return SaveFactions();
	}

	Json FactionService::FactionsJson() const
	{
		Json stored = Json::object();
		for (const auto& [factionId, faction] : factions) {
			Json memberNames = Json::object();
			for (const auto& [profileId, name] : faction.memberNames) memberNames[profileId] = name;
			stored[factionId] = Json{
				{ "name", faction.name },
				{ "tag", faction.tag },
				{ "leaderProfileId", faction.leaderProfileId },
				{ "members", faction.members },
				{ "memberNames", std::move(memberNames) },
				{ "officers", faction.officers },
				{ "description", faction.description },
				{ "motd", faction.motd },
				{ "color", faction.color },
				{ "createdAt", faction.createdAt }
			};
		}
		Json json{ { "nextFactionNumber", nextFactionNumber }, { "factions", std::move(stored) } };
		if (!quarantinedFactions.empty()) json["quarantinedFactions"] = quarantinedFactions;
		return json;
	}

	void FactionService::LoadFactions(const Json& a_stored)
	{
		const auto found = a_stored.find("factions");
		std::map<std::string, Faction> loaded;
		std::map<std::string, std::string> byProfile;
		std::uint32_t highest = 0;
		const auto stashed = a_stored.find("quarantinedFactions");
		Json quarantined = stashed != a_stored.end() && stashed->is_object() ? *stashed : Json::object();
		const auto quarantine = [&quarantined](const std::string& a_factionId, const Json& a_item) {
			auto key = a_factionId;
			for (std::uint32_t suffix = 2; quarantined.contains(key); ++suffix) key = a_factionId + "#" + std::to_string(suffix);
			quarantined[key] = a_item;
			std::cerr << "Faction " << a_factionId << " could not be loaded and was kept aside in quarantinedFactions.\n";
		};
		if (!a_stored.is_object()) quarantine("factions-root", a_stored);
		if (stashed != a_stored.end() && !stashed->is_object()) quarantine("quarantinedFactions", *stashed);
		if (found != a_stored.end() && !found->is_object()) quarantine("factions", *found);
		if (found != a_stored.end() && found->is_object()) {
			for (const auto& [factionId, item] : found->items()) {
				if (FactionNumber(factionId) == 0) { quarantine(factionId, item); continue; }
				const auto name = F4T::FactionValidation::NormalizeFactionName(ReadString(item, "name").value_or(""));
				const auto tag = F4T::FactionValidation::NormalizeFactionTag(ReadString(item, "tag").value_or(""));
				const auto leader = ReadString(item, "leaderProfileId");
				const auto createdAt = ReadDouble(item, "createdAt");
				const auto members = item.find("members");
				const auto names = item.find("memberNames");
				const auto officerList = item.find("officers");
				if (!name || !tag || !leader || !createdAt || members == item.end() || !members->is_array() ||
					members->size() > F4T::ServerConfiguration::kFactionCeilingMembers || names == item.end() || !names->is_object() ||
					loaded.size() >= F4T::ServerConfiguration::kFactionCeilingFactions) {
					quarantine(factionId, item);
					continue;
				}
				if (!ProfileIdValid(*leader) || std::any_of(members->begin(), members->end(), [](const auto& a_member) {
						return !a_member.is_string() || !ProfileIdValid(a_member.template get<std::string>());
					})) {
					quarantine(factionId, item);
					continue;
				}
				std::vector<std::string> accepted;
				for (const auto& member : *members) {
					auto profileId = member.get<std::string>();
					if (byProfile.contains(profileId)) {
						std::cerr << "Faction " << factionId << " dropped member " << profileId << " because it already belongs to another faction.\n";
						continue;
					}
					if (std::find(accepted.begin(), accepted.end(), profileId) != accepted.end()) {
						std::cerr << "Faction " << factionId << " dropped duplicate member " << profileId << ".\n";
						continue;
					}
					accepted.push_back(std::move(profileId));
				}
				if (std::find(accepted.begin(), accepted.end(), *leader) == accepted.end()) {
					quarantine(factionId, item);
					continue;
				}
				if (std::any_of(loaded.begin(), loaded.end(), [&name, &tag](const auto& a_entry) {
						return FactionNameKey(a_entry.second.name) == FactionNameKey(*name) || (!tag->empty() && a_entry.second.tag == *tag);
					})) {
					quarantine(factionId, item);
					continue;
				}
				Faction faction{ factionId, *name, *tag, *leader, accepted, {}, *createdAt };
				for (const auto& [profileId, memberName] : names->items()) {
					if (memberName.is_string() && std::find(accepted.begin(), accepted.end(), profileId) != accepted.end()) {
						faction.memberNames[profileId] = memberName.get<std::string>();
					}
				}
				if (officerList != item.end() && officerList->is_array()) {
					for (const auto& officer : *officerList) {
						if (!officer.is_string()) continue;
						auto profileId = officer.get<std::string>();
						if (profileId != *leader && std::find(accepted.begin(), accepted.end(), profileId) != accepted.end()) faction.officers.insert(std::move(profileId));
					}
				}
				faction.description = F4T::FactionValidation::NormalizeFactionDescription(ReadString(item, "description").value_or("")).value_or("");
				faction.motd = F4T::FactionValidation::NormalizeFactionMotd(ReadString(item, "motd").value_or("")).value_or("");
				faction.color = F4T::FactionValidation::NormalizeFactionColor(ReadString(item, "color").value_or("")).value_or("");
				for (const auto& profileId : accepted) byProfile.emplace(profileId, factionId);
				highest = (std::max)(highest, FactionNumber(factionId));
				loaded.emplace(factionId, std::move(faction));
			}
		}
		for (const auto& [key, item] : quarantined.items()) highest = (std::max)(highest, FactionNumber(key.substr(0, key.find('#'))));
		factions = std::move(loaded);
		factionByProfile = std::move(byProfile);
		quarantinedFactions = std::move(quarantined);
		if (!quarantinedFactions.empty()) std::cerr << "Faction store holds " << quarantinedFactions.size() << " quarantined entries; they are kept in the save file and never pruned.\n";
		nextFactionNumber = (std::min)((std::max)(ReadUInt32(a_stored, "nextFactionNumber").value_or(1), highest + 1), kMaximumFactionNumber);
	}

	bool FactionService::SaveFactions()
	{
		if (persistence == nullptr) return true;
		try {
			persistence->SaveFactions(FactionsJson());
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Faction persistence unavailable: " << error.what() << '\n';
			return false;
		}
	}
}
