#pragma once

#include "ServerFactionRules.h"
#include "ServerProtocol.h"
#include "ServerOutboundFrames.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	class PersistenceGate;

	class SocialGraph
	{
	public:
		using Json = nlohmann::json;
		using AdminCheck = std::function<bool(const std::string&)>;

		static constexpr std::string_view kStrangerName{ "Stranger" };
		static constexpr std::size_t kMaximumKnownPerProfile{ 256 };
		static constexpr std::size_t kMaximumFriendsPerProfile{ 200 };
		static constexpr std::size_t kMaximumFriendNameBytes{ 128 };

		explicit SocialGraph(ProtocolStats& a_stats) :
			stats(a_stats)
		{}

		[[nodiscard]] bool Enabled() const noexcept { return enabled; }
		bool Enable(PersistenceGate* a_persistence);
		bool Disable();
		bool SaveNow();
		void SetAdminCheck(AdminCheck a_check) { isAdmin = std::move(a_check); }

		struct NameView
		{
			std::string displayName;
			std::optional<std::string> disclosedName;
		};

		struct FriendEntry
		{
			std::string profileId;
			std::string name;
		};

		[[nodiscard]] bool Knows(const std::string& a_viewerProfileId, const std::string& a_subjectProfileId) const;
		void NoteKnown(const std::string& a_firstProfileId, const std::string& a_secondProfileId);
		[[nodiscard]] bool AreFriends(const std::string& a_firstProfileId, const std::string& a_secondProfileId) const;
		[[nodiscard]] const std::vector<FriendEntry>* FriendsOf(const std::string& a_profileId) const;
		[[nodiscard]] std::size_t FriendCount(const std::string& a_profileId) const;
		[[nodiscard]] std::vector<FriendEntry> FriendListCopy(const std::string& a_profileId) const;
		void RestoreFriendList(const std::string& a_profileId, std::vector<FriendEntry> a_list);
		[[nodiscard]] std::vector<std::string> KnownListCopy(const std::string& a_profileId) const;
		void RestoreKnownList(const std::string& a_profileId, std::vector<std::string> a_list);
		bool AddFriend(
			const std::string& a_firstProfileId,
			std::string_view a_firstName,
			const std::string& a_secondProfileId,
			std::string_view a_secondName);
		bool RemoveFriend(const std::string& a_firstProfileId, const std::string& a_secondProfileId);
		bool NoteFriendName(const std::string& a_profileId, std::string_view a_name);
		void ForgetProfile(const std::string& a_profileId);
		[[nodiscard]] NameView NameViewFor(
			const std::string& a_viewerProfileId,
			const std::string& a_subjectProfileId,
			std::string_view a_realName,
			bool a_knownByAssociation = false) const;

	private:
		void TrimKnown(const std::string& a_profileId, std::vector<std::string>& a_list) const;
		[[nodiscard]] Json KnownJson() const;
		void LoadKnown(const Json& a_stored);
		void LoadFriends(const Json& a_stored);
		bool SaveKnown();

		ProtocolStats& stats;
		bool enabled{};
		PersistenceGate* persistence{};
		AdminCheck isAdmin;
		std::map<std::string, std::vector<std::string>> known;
		std::map<std::string, std::vector<FriendEntry>> friends;
	};
}
