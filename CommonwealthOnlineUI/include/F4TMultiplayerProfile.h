#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::MultiplayerProfile
{
	struct Preferences
	{
		std::uint8_t onlineStatus{ 1 };
		std::uint8_t allowFriendJoin{ 0 };
		std::uint8_t mapMarkerSize{ 1 };
		std::uint8_t friendRequests{ 0 };
		std::uint8_t regionPreference{ 0 };
		std::uint8_t interpolationQuality{ 2 };
		bool showLocationToFriends{ true };
		bool showPlayerMarkers{ true };
		bool showFriendMarkers{ true };
		bool showNameplates{ true };
		bool networkSmoothing{ true };
		bool showPing{ true };
		bool networkStatsOverlay{ false };
	};

	std::string GetDisplayName();
	bool CopyDisplayName(char* a_buffer, std::size_t a_capacity);
	bool SaveDisplayName(std::string_view a_name, std::string& a_error);
	Preferences GetPreferences();
	bool SetPreference(std::string_view a_key, std::string_view a_valueJson, std::string& a_error);
	std::string PreferencesJson();
#ifdef COUI_PROFILE_TESTING
	void SetStorePathForTesting(const std::string& a_path);
#endif
}
