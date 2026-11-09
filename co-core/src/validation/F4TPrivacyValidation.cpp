#include "F4TPrivacyValidation.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace F4T::PrivacyValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kFieldCount = 3;

		std::optional<OnlineStatus> ParseOnlineStatus(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto& text = a_value.get_ref<const std::string&>();
			if (text == "Everyone") return OnlineStatus::Everyone;
			if (text == "Friends") return OnlineStatus::Friends;
			if (text == "Nobody") return OnlineStatus::Nobody;
			return std::nullopt;
		}

		std::optional<FriendRequests> ParseFriendRequests(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto& text = a_value.get_ref<const std::string&>();
			if (text == "Everyone") return FriendRequests::Everyone;
			if (text == "FriendsOfFriends") return FriendRequests::FriendsOfFriends;
			if (text == "Nobody") return FriendRequests::Nobody;
			return std::nullopt;
		}
	}

	std::optional<Settings> NormalizePrivacy(const Json& a_privacy)
	{
		if (!a_privacy.is_object() || a_privacy.size() != kFieldCount) {
			return std::nullopt;
		}
		const auto status = a_privacy.find("onlineStatus");
		const auto location = a_privacy.find("showLocation");
		const auto requests = a_privacy.find("friendRequests");
		if (status == a_privacy.end() || location == a_privacy.end() || requests == a_privacy.end() || !location->is_boolean()) {
			return std::nullopt;
		}
		const auto onlineStatus = ParseOnlineStatus(*status);
		const auto friendRequests = ParseFriendRequests(*requests);
		if (!onlineStatus || !friendRequests) {
			return std::nullopt;
		}
		return Settings{ *onlineStatus, location->get<bool>(), *friendRequests };
	}
}
