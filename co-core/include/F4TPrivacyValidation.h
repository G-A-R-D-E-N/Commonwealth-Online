#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace F4T::PrivacyValidation
{
	enum class OnlineStatus : std::uint8_t
	{
		Everyone,
		Friends,
		Nobody
	};

	enum class FriendRequests : std::uint8_t
	{
		Everyone,
		FriendsOfFriends,
		Nobody
	};

	struct Settings
	{
		OnlineStatus onlineStatus{ OnlineStatus::Friends };
		bool showLocation{ true };
		FriendRequests friendRequests{ FriendRequests::Everyone };
	};

	[[nodiscard]] std::optional<Settings> NormalizePrivacy(const nlohmann::json& a_privacy);
}
