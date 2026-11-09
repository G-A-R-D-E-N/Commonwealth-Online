#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace F4T::UIPrefs
{
	using MultiplayerPreferences = COMMONWEALTH_ONLINE_UI_API::MultiplayerPreferences;

	inline constexpr std::int64_t kRefreshIntervalMs = 1000;
	inline constexpr std::array<const char*, 3> kOnlineStatusNames{ "Everyone", "Friends", "Nobody" };
	inline constexpr std::array<const char*, 3> kFriendRequestNames{ "Everyone", "FriendsOfFriends", "Nobody" };

	// Constraint: option indices follow the settings option order used by the native store.
	[[nodiscard]] inline MultiplayerPreferences DefaultPreferences()
	{
		MultiplayerPreferences prefs{};
		prefs.onlineStatus = 1;
		prefs.allowFriendJoin = 0;
		prefs.mapMarkerSize = 1;
		prefs.friendRequests = 0;
		prefs.regionPreference = 0;
		prefs.interpolationQuality = 2;
		prefs.showLocationToFriends = 1;
		prefs.showPlayerMarkers = 1;
		prefs.showFriendMarkers = 1;
		prefs.showNameplates = 1;
		prefs.networkSmoothing = 1;
		prefs.showPing = 1;
		prefs.networkStatsOverlay = 0;
		return prefs;
	}

	[[nodiscard]] inline bool PreferencesStale(
		bool a_hasCached,
		std::uint64_t a_cachedJoin,
		std::uint64_t a_join,
		std::int64_t a_cachedAtMs,
		std::int64_t a_nowMs)
	{
		return !a_hasCached || a_cachedJoin != a_join || a_nowMs - a_cachedAtMs >= kRefreshIntervalMs;
	}

	[[nodiscard]] inline std::array<std::uint8_t, 13> ValuesOf(const MultiplayerPreferences& a_prefs)
	{
		return { a_prefs.onlineStatus, a_prefs.allowFriendJoin, a_prefs.mapMarkerSize, a_prefs.friendRequests,
			a_prefs.regionPreference, a_prefs.interpolationQuality, a_prefs.showLocationToFriends,
			a_prefs.showPlayerMarkers, a_prefs.showFriendMarkers, a_prefs.showNameplates, a_prefs.networkSmoothing,
			a_prefs.showPing, a_prefs.networkStatsOverlay };
	}

	template <std::size_t N>
	[[nodiscard]] inline const char* OptionName(
		const std::array<const char*, N>& a_options,
		std::uint8_t a_index,
		std::size_t a_fallback)
	{
		return static_cast<std::size_t>(a_index) < N ? a_options[a_index] : a_options[a_fallback];
	}

	// Constraint: the wire object has exactly the three keys the server's NormalizePrivacy requires.
	[[nodiscard]] inline nlohmann::json PrivacyJson(const MultiplayerPreferences& a_prefs)
	{
		return nlohmann::json{
			{ "onlineStatus", OptionName(kOnlineStatusNames, a_prefs.onlineStatus, 1) },
			{ "showLocation", a_prefs.showLocationToFriends != 0 },
			{ "friendRequests", OptionName(kFriendRequestNames, a_prefs.friendRequests, 0) }
		};
	}

	[[nodiscard]] inline nlohmann::json PrivacySetPacket(const MultiplayerPreferences& a_prefs)
	{
		return nlohmann::json{ { "type", "privacySet" }, { "privacy", PrivacyJson(a_prefs) } };
	}

	MultiplayerPreferences Get();
	void PushNetStats(const std::string& a_json);
	bool ConsumeChanged(MultiplayerPreferences& a_out);
	void SendPrivacyIfChanged(std::uint64_t a_generation);
}
