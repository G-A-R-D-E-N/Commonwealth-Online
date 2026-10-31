#include "ServerSocialGraph.h"

#include "F4TCharacterSlots.h"
#include "PersistenceGate.h"
#include "ServerProtocolImpl.h"

#include <algorithm>
#include <exception>
#include <iostream>

namespace CommonwealthOnline::Server
{
	using Json = nlohmann::json;
	using namespace FactionRules;

	namespace
	{
		std::string TrimFriendName(std::string_view a_name)
		{
			// Constraint: the cut must land on a code point boundary or dump() throws for every later save.
			const auto limit = std::min(a_name.size(), SocialGraph::kMaximumFriendNameBytes);
			std::size_t index = 0;
			while (index < limit) {
				const auto lead = static_cast<unsigned char>(a_name[index]);
				std::size_t width = 1;
				if (lead >= 0xF5) break;
				if (lead >= 0xF0) width = 4;
				else if (lead >= 0xE0) width = 3;
				else if (lead >= 0xC0) width = 2;
				else if (lead >= 0x80) break;
				if (index + width > limit) break;
				bool complete = true;
				for (std::size_t offset = 1; offset < width; ++offset) {
					if ((static_cast<unsigned char>(a_name[index + offset]) & 0xC0) != 0x80) {
						complete = false;
						break;
					}
				}
				if (!complete) break;
				index += width;
			}
			return index == 0 ? std::string{ SocialGraph::kStrangerName } : std::string{ a_name.substr(0, index) };
		}
	}

	bool SocialGraph::Enable(PersistenceGate* a_persistence)
	{
		if (enabled) return false;
		if (a_persistence != nullptr) LoadKnown(a_persistence->ReadStoredKnownPeople());
		persistence = a_persistence;
		enabled = true;
		return true;
	}

	bool SocialGraph::Disable()
	{
		if (!enabled) return false;
		enabled = false;
		return true;
	}

	bool SocialGraph::SaveNow()
	{
		if (!enabled) return false;
		return SaveKnown();
	}

	bool SocialGraph::Knows(const std::string& a_viewerProfileId, const std::string& a_subjectProfileId) const
	{
		if (a_viewerProfileId.empty() || a_subjectProfileId.empty()) return false;
		if (a_viewerProfileId == a_subjectProfileId) return true;
		if (AreFriends(a_viewerProfileId, a_subjectProfileId)) return true;
		const auto found = known.find(a_viewerProfileId);
		if (found == known.end()) return false;
		return std::find(found->second.begin(), found->second.end(), a_subjectProfileId) != found->second.end();
	}

	void SocialGraph::TrimKnown(const std::string& a_profileId, std::vector<std::string>& a_list) const
	{
		while (a_list.size() > kMaximumKnownPerProfile) {
			const auto victim = std::find_if(a_list.begin(), a_list.end(), [this, &a_profileId](const std::string& a_other) {
				return !AreFriends(a_profileId, a_other);
			});
			if (victim == a_list.end()) return;
			a_list.erase(victim);
		}
	}

	void SocialGraph::NoteKnown(const std::string& a_firstProfileId, const std::string& a_secondProfileId)
	{
		if (a_firstProfileId.empty() || a_secondProfileId.empty() || a_firstProfileId == a_secondProfileId) return;
		const std::string order[2][2]{ { a_firstProfileId, a_secondProfileId }, { a_secondProfileId, a_firstProfileId } };
		for (const auto& pair : order) {
			auto& list = known[pair[0]];
			if (std::find(list.begin(), list.end(), pair[1]) != list.end()) continue;
			list.push_back(pair[1]);
			TrimKnown(pair[0], list);
		}
	}

	bool SocialGraph::AreFriends(const std::string& a_firstProfileId, const std::string& a_secondProfileId) const
	{
		if (a_firstProfileId.empty() || a_secondProfileId.empty() || a_firstProfileId == a_secondProfileId) return false;
		const auto found = friends.find(a_firstProfileId);
		if (found == friends.end()) return false;
		return std::any_of(found->second.begin(), found->second.end(), [&a_secondProfileId](const FriendEntry& a_entry) {
			return a_entry.profileId == a_secondProfileId;
		});
	}

	const std::vector<SocialGraph::FriendEntry>* SocialGraph::FriendsOf(const std::string& a_profileId) const
	{
		const auto found = friends.find(a_profileId);
		return found == friends.end() ? nullptr : &found->second;
	}

	std::size_t SocialGraph::FriendCount(const std::string& a_profileId) const
	{
		const auto found = friends.find(a_profileId);
		return found == friends.end() ? 0 : found->second.size();
	}

	bool SocialGraph::AddFriend(
		const std::string& a_firstProfileId,
		std::string_view a_firstName,
		const std::string& a_secondProfileId,
		std::string_view a_secondName)
	{
		if (a_firstProfileId.empty() || a_secondProfileId.empty() || a_firstProfileId == a_secondProfileId) return false;
		if (AreFriends(a_firstProfileId, a_secondProfileId)) return true;
		if (FriendCount(a_firstProfileId) >= kMaximumFriendsPerProfile || FriendCount(a_secondProfileId) >= kMaximumFriendsPerProfile) return false;
		friends[a_firstProfileId].push_back(FriendEntry{ a_secondProfileId, TrimFriendName(a_secondName) });
		friends[a_secondProfileId].push_back(FriendEntry{ a_firstProfileId, TrimFriendName(a_firstName) });
		return true;
	}

	bool SocialGraph::RemoveFriend(const std::string& a_firstProfileId, const std::string& a_secondProfileId)
	{
		if (!AreFriends(a_firstProfileId, a_secondProfileId)) return false;
		const auto remove = [this](const std::string& a_owner, const std::string& a_other) {
			const auto found = friends.find(a_owner);
			if (found == friends.end()) return;
			std::erase_if(found->second, [&a_other](const FriendEntry& a_entry) { return a_entry.profileId == a_other; });
			if (found->second.empty()) friends.erase(found);
		};
		remove(a_firstProfileId, a_secondProfileId);
		remove(a_secondProfileId, a_firstProfileId);
		return true;
	}

	bool SocialGraph::NoteFriendName(const std::string& a_profileId, std::string_view a_name)
	{
		if (a_profileId.empty() || a_name.empty()) return false;
		const auto trimmed = TrimFriendName(a_name);
		bool changed = false;
		for (auto& [owner, list] : friends) {
			if (owner == a_profileId) continue;
			for (auto& entry : list) {
				if (entry.profileId != a_profileId || entry.name == trimmed) continue;
				entry.name = trimmed;
				changed = true;
			}
		}
		return changed;
	}

	std::vector<SocialGraph::FriendEntry> SocialGraph::FriendListCopy(const std::string& a_profileId) const
	{
		const auto* list = FriendsOf(a_profileId);
		return list ? *list : std::vector<FriendEntry>{};
	}

	void SocialGraph::RestoreFriendList(const std::string& a_profileId, std::vector<FriendEntry> a_list)
	{
		if (a_list.empty()) friends.erase(a_profileId);
		else friends[a_profileId] = std::move(a_list);
	}

	std::vector<std::string> SocialGraph::KnownListCopy(const std::string& a_profileId) const
	{
		const auto found = known.find(a_profileId);
		return found == known.end() ? std::vector<std::string>{} : found->second;
	}

	void SocialGraph::RestoreKnownList(const std::string& a_profileId, std::vector<std::string> a_list)
	{
		if (a_list.empty()) known.erase(a_profileId);
		else known[a_profileId] = std::move(a_list);
	}

	void SocialGraph::ForgetProfile(const std::string& a_profileId)
	{
		if (a_profileId.empty()) return;
		std::string parent{ a_profileId };
		if (const auto slotKey = F4T::CharacterSlots::ParseSlotKey(a_profileId)) parent = slotKey->first;
		known.erase(a_profileId);
		friends.erase(a_profileId);
		if (parent != a_profileId) {
			known.erase(parent);
			friends.erase(parent);
		}
		for (auto entry = known.begin(); entry != known.end();) {
			std::erase(entry->second, a_profileId);
			if (parent != a_profileId) std::erase(entry->second, parent);
			if (entry->second.empty()) entry = known.erase(entry);
			else ++entry;
		}
		for (auto entry = friends.begin(); entry != friends.end();) {
			std::erase_if(entry->second, [&](const FriendEntry& a_friend) {
				return a_friend.profileId == a_profileId || (parent != a_profileId && a_friend.profileId == parent);
			});
			if (entry->second.empty()) entry = friends.erase(entry);
			else ++entry;
		}
	}

	SocialGraph::NameView SocialGraph::NameViewFor(
		const std::string& a_viewerProfileId,
		const std::string& a_subjectProfileId,
		std::string_view a_realName,
		bool a_knownByAssociation) const
	{
		NameView view;
		if (a_realName.empty()) return view;
		if (a_knownByAssociation || Knows(a_viewerProfileId, a_subjectProfileId) || (isAdmin && isAdmin(a_viewerProfileId))) {
			view.displayName = std::string{ a_realName };
			view.disclosedName = view.displayName;
			return view;
		}
		view.displayName = std::string{ kStrangerName };
		return view;
	}

	Json SocialGraph::KnownJson() const
	{
		Json stored = Json::object();
		for (const auto& [profileId, others] : known) {
			Json list = Json::array();
			for (const auto& other : others) list.push_back(other);
			stored[profileId] = std::move(list);
		}
		Json friendStore = Json::object();
		for (const auto& [profileId, list] : friends) {
			Json entries = Json::array();
			for (const auto& entry : list) {
				entries.push_back(Json{ { "profileId", entry.profileId }, { "name", entry.name } });
			}
			friendStore[profileId] = std::move(entries);
		}
		return Json{ { "knownPeople", std::move(stored) }, { "friends", std::move(friendStore) } };
	}

	void SocialGraph::LoadKnown(const Json& a_stored)
	{
		known.clear();
		friends.clear();
		// Constraint: a non-object store must reach the unreadable log, not return silent.
		if (!a_stored.is_object()) {
			std::cerr << "Known-people store is unreadable; starting empty until the next save replaces it.\n";
			return;
		}
		const auto found = a_stored.find("knownPeople");
		if (found != a_stored.end()) {
			if (!found->is_object()) {
				std::cerr << "Known-people store is unreadable; starting empty until the next save replaces it.\n";
			} else {
				for (const auto& [profileId, list] : found->items()) {
					if (!ProfileIdValid(profileId) || !list.is_array()) {
						std::cerr << "Known-people entry for an invalid profile was dropped.\n";
						continue;
					}
					auto& knownList = known[profileId];
					for (const auto& other : list) {
						if (!other.is_string() || !ProfileIdValid(other.get<std::string>()) || other.get<std::string>() == profileId) continue;
						if (std::find(knownList.begin(), knownList.end(), other.get<std::string>()) != knownList.end()) continue;
						knownList.push_back(other.get<std::string>());
						TrimKnown(profileId, knownList);
					}
					if (knownList.empty()) known.erase(profileId);
				}
			}
		}
		LoadFriends(a_stored);
	}

	void SocialGraph::LoadFriends(const Json& a_stored)
	{
		const auto found = a_stored.find("friends");
		if (found == a_stored.end()) return;
		if (!found->is_object()) {
			std::cerr << "Friend store is unreadable; starting without friends until the next save replaces it.\n";
			return;
		}
		for (const auto& [profileId, list] : found->items()) {
			if (!ProfileIdValid(profileId) || !list.is_array()) {
				std::cerr << "Friend entry for an invalid profile was dropped.\n";
				continue;
			}
			auto& friendList = friends[profileId];
			for (const auto& item : list) {
				const auto other = item.is_object() ? item.find("profileId") : item.end();
				const auto name = item.is_object() ? item.find("name") : item.end();
				if (other == item.end() || !other->is_string() || !ProfileIdValid(other->get<std::string>()) || other->get<std::string>() == profileId) {
					std::cerr << "Friend entry for an invalid profile was dropped.\n";
					continue;
				}
				if (name == item.end() || !name->is_string() || name->get<std::string>().empty() || name->get<std::string>().size() > kMaximumFriendNameBytes) {
					std::cerr << "Friend entry for an invalid name was dropped.\n";
					continue;
				}
				if (friendList.size() >= kMaximumFriendsPerProfile) {
					std::cerr << "Friend entry over the cap was dropped.\n";
					continue;
				}
				const auto otherId = other->get<std::string>();
				if (std::any_of(friendList.begin(), friendList.end(), [&otherId](const FriendEntry& a_entry) { return a_entry.profileId == otherId; })) continue;
				friendList.push_back(FriendEntry{ otherId, name->get<std::string>() });
			}
			if (friendList.empty()) friends.erase(profileId);
		}
		std::vector<std::pair<std::string, std::string>> oneSided;
		for (const auto& [profileId, list] : friends) {
			for (const auto& entry : list) {
				if (!AreFriends(entry.profileId, profileId)) oneSided.push_back({ profileId, entry.profileId });
			}
		}
		for (const auto& [owner, other] : oneSided) {
			std::cerr << "One-sided stored friendship was dropped.\n";
			const auto list = friends.find(owner);
			if (list == friends.end()) continue;
			std::erase_if(list->second, [&other](const FriendEntry& a_entry) { return a_entry.profileId == other; });
			if (list->second.empty()) friends.erase(list);
		}
	}

	bool SocialGraph::SaveKnown()
	{
		if (persistence == nullptr) return true;
		try {
			persistence->SaveKnownPeople(KnownJson());
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Known-people persistence unavailable: " << error.what() << '\n';
			return false;
		}
	}

	bool ProtocolHost::Impl::EnableSocial(PersistenceGate* a_persistence)
	{
		if (hasEverActivatedSession) return false;
		socialGraph.SetAdminCheck([this](const std::string& a_profileId) { return IsConsoleAdmin(a_profileId); });
		const bool enabled = socialGraph.Enable(a_persistence);
		if (enabled) friendService.Enable();
		return enabled;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PairRefreshFrames(
		std::uint32_t a_leftConnectionId,
		const Session& a_left,
		std::uint32_t a_rightConnectionId,
		const Session& a_right,
		double a_unixTimeSeconds)
	{
		// Constraint: the pair refresh must re-mask cached states exactly like the relay.
		const auto maskFor = [this, a_unixTimeSeconds](Json a_packet, const std::string& a_viewerProfileId, const std::string& a_subjectProfileId, bool a_viewerSocialNames) {
			if (const auto cachedName = a_packet.find("characterName"); cachedName != a_packet.end() && cachedName->is_string()) {
				const auto view = socialGraph.NameViewFor(a_viewerProfileId, a_subjectProfileId, cachedName->get<std::string>(), factionService.SameFaction(a_viewerProfileId, a_subjectProfileId));
				a_packet["displayName"] = view.displayName;
				if (!view.disclosedName) {
					// Constraint: legacy clients get display text in characterName instead of no characterName.
					if (!a_viewerSocialNames) a_packet["characterName"] = view.displayName;
					else a_packet.erase("characterName");
				}
			}
			a_packet["serverTime"] = a_unixTimeSeconds;
			return a_packet;
		};
		std::vector<OutboundFrame> frames;
		if (a_right.lastDurablePlayerState) {
			frames.push_back(ReliableFrame(a_leftConnectionId, maskFor(*a_right.lastDurablePlayerState, a_left.profileId, a_right.profileId, SessionHasSocialNames(a_left))));
		}
		if (a_left.lastDurablePlayerState) {
			frames.push_back(ReliableFrame(a_rightConnectionId, maskFor(*a_left.lastDurablePlayerState, a_right.profileId, a_left.profileId, SessionHasSocialNames(a_right))));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::FactionJoinRefreshFrames(
		std::uint32_t a_connectionId,
		const Session& a_learner,
		double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, peer] : sessions) {
			if (connectionId == a_connectionId || !peer.active) continue;
			if (!factionService.SameFaction(a_learner.profileId, peer.profileId)) continue;
			AppendFrames(frames, PairRefreshFrames(a_connectionId, a_learner, connectionId, peer, a_unixTimeSeconds));
		}
		AppendFrames(frames, factionService.RosterFrames(FactionPeers(), a_unixTimeSeconds));
		return frames;
	}
}
