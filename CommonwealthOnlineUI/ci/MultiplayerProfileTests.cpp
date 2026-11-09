#include "F4TMultiplayerProfile.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main()
{
	using namespace F4T::MultiplayerProfile;
	const auto root = std::filesystem::temp_directory_path() /
		("coui-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	const auto path = root / "profile.json";
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	std::string error;
	assert(SaveDisplayName("Vault Dweller", error));
	char buffer[129]{};
	assert(CopyDisplayName(buffer, sizeof(buffer)));
	assert(std::string(buffer) == "Vault Dweller");
	assert(!CopyDisplayName(buffer, 4));
	assert(buffer[0] == '\0');
	assert(!SaveDisplayName(std::string(129, 'x'), error));
	assert(!SaveDisplayName("bad\nname", error));
	assert(!SaveDisplayName(std::string("bad\0name", 8), error));
	assert(!SaveDisplayName(" trailing ", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName() == "Vault Dweller");
	assert(SaveDisplayName(std::string(128, 'x'), error));
	assert(CopyDisplayName(buffer, sizeof(buffer)));
	assert(std::string(buffer).size() == 128);
	assert(!SaveDisplayName(std::string(1, '\xff'), error));
	assert(error == "Display name must be valid UTF-8.");
	assert(!std::filesystem::exists(std::filesystem::path(path.string() + ".tmp")));
	assert(SaveDisplayName("Ren\xc3\xa9" "e", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName() == "Ren\xc3\xa9" "e");
	assert(SaveDisplayName("", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	{
		std::ofstream output(path);
		output << "not json";
	}
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	const auto blocked = root / "blocked";
	std::ofstream(blocked).put('x');
	SetStorePathForTesting((blocked / "profile.json").string());
	assert(!SaveDisplayName("Cannot Save", error));
	assert(!error.empty());
	const auto occupied = root / "occupied.json";
	std::filesystem::create_directories(occupied);
	SetStorePathForTesting(occupied.string());
	assert(!SaveDisplayName("Blocked Move", error));
	assert(!std::filesystem::exists(std::filesystem::path(occupied.string() + ".tmp")));

	const auto prefsPath = root / "prefs.json";
	SetStorePathForTesting(prefsPath.string());
	const auto defaults = GetPreferences();
	assert(defaults.onlineStatus == 1 && defaults.allowFriendJoin == 0 && defaults.mapMarkerSize == 1);
	assert(defaults.friendRequests == 0 && defaults.regionPreference == 0 && defaults.interpolationQuality == 2);
	assert(defaults.showLocationToFriends && defaults.showPlayerMarkers && defaults.showFriendMarkers);
	assert(defaults.showNameplates && defaults.networkSmoothing && defaults.showPing && !defaults.networkStatsOverlay);
	assert(PreferencesJson().find("\"profile.allowFriendJoin\":\"Always\"") != std::string::npos);
	assert(SetPreference("profile.onlineStatus", "\"Nobody\"", error));
	assert(SetPreference("profile.allowFriendJoin", "\"Never\"", error));
	assert(SetPreference("profile.showLocationToFriends", "false", error));
	assert(SetPreference("map.showPlayerMarkers", "false", error));
	assert(SetPreference("map.showFriendMarkers", "false", error));
	assert(SetPreference("map.showNameplates", "false", error));
	assert(SetPreference("map.mapMarkerSize", "\"Large\"", error));
	assert(SetPreference("map.friendRequests", "\"Friends of Friends\"", error));
	assert(SetPreference("network.regionPreference", "\"Oceania\"", error));
	assert(SetPreference("network.networkSmoothing", "false", error));
	assert(SetPreference("network.interpolationQuality", "\"Low\"", error));
	assert(SetPreference("network.showPing", "false", error));
	assert(SetPreference("network.networkStatsOverlay", "true", error));
	const auto saved = PreferencesJson();
	SetStorePathForTesting(prefsPath.string());
	assert(PreferencesJson() == saved);
	const auto restored = GetPreferences();
	assert(restored.onlineStatus == 2 && restored.allowFriendJoin == 2 && restored.mapMarkerSize == 2);
	assert(restored.friendRequests == 1 && restored.regionPreference == 4 && restored.interpolationQuality == 0);
	assert(!restored.showLocationToFriends && !restored.showPlayerMarkers && !restored.showFriendMarkers);
	assert(!restored.showNameplates && !restored.networkSmoothing && !restored.showPing && restored.networkStatsOverlay);

	const auto beforeInvalid = PreferencesJson();
	assert(!SetPreference("profile.onlineStatus", "\"Invisible\"", error) && !error.empty());
	assert(!SetPreference("profile.onlineStatus", "\"nobody\"", error));
	assert(!SetPreference("profile.onlineStatus", "1", error));
	assert(!SetPreference("map.showPlayerMarkers", "\"yes\"", error));
	assert(!SetPreference("map.showPlayerMarkers", "not json", error));
	assert(!SetPreference("map.mapMarkerSize", "\"Huge\"", error));
	assert(!SetPreference("map.friendRequests", "\"Friends of friends\"", error));
	assert(!SetPreference("map.unknown", "true", error));
	assert(!SetPreference("", "true", error));
	assert(PreferencesJson() == beforeInvalid);
	SetStorePathForTesting(prefsPath.string());
	assert(PreferencesJson() == beforeInvalid);

	assert(SaveDisplayName("Keeper", error));
	SetStorePathForTesting(prefsPath.string());
	assert(GetDisplayName() == "Keeper");
	assert(GetPreferences().networkStatsOverlay && !GetPreferences().showPing);
	assert(SetPreference("map.showNameplates", "true", error));
	SetStorePathForTesting(prefsPath.string());
	assert(GetDisplayName() == "Keeper" && GetPreferences().showNameplates && GetPreferences().networkStatsOverlay);

	{
		std::ofstream output(prefsPath, std::ios::trunc);
		output << R"json({"version":1,"displayName":"Kept","prefs":{"map.showNameplates":"maybe","map.mapMarkerSize":"Large","network.showPing":false,"map.unknown":1}})json";
	}
	SetStorePathForTesting(prefsPath.string());
	assert(GetDisplayName() == "Kept");
	const auto corrupt = GetPreferences();
	assert(corrupt.showNameplates && corrupt.mapMarkerSize == 2 && !corrupt.showPing && !corrupt.networkStatsOverlay);

	{
		std::ofstream output(prefsPath, std::ios::trunc);
		output << R"json({"version":1,"displayName":"X"})json";
	}
	SetStorePathForTesting(prefsPath.string());
	assert(GetDisplayName() == "X");
	assert(GetPreferences().showPlayerMarkers && GetPreferences().mapMarkerSize == 1);

	{
		std::ofstream output(prefsPath, std::ios::trunc);
		output << R"json({"version":1,"displayName":5,"prefs":{"map.showPlayerMarkers":false}})json";
	}
	SetStorePathForTesting(prefsPath.string());
	assert(GetDisplayName().empty());
	assert(!GetPreferences().showPlayerMarkers);

	SetStorePathForTesting((blocked / "prefs.json").string());
	assert(!SetPreference("map.showPlayerMarkers", "false", error));
	assert(GetPreferences().showPlayerMarkers);
	std::filesystem::remove_all(root);
}
