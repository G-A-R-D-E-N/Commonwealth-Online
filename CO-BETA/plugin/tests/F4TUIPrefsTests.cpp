#include "F4TUIPrefs.h"

#include <cassert>
#include <cstddef>
#include <string>

int main()
{
	using namespace F4T::UIPrefs;

	const auto defaults = DefaultPreferences();
	assert(static_cast<std::size_t>(defaults.size) == sizeof(MultiplayerPreferences));
	assert(defaults.onlineStatus == 1);
	assert(defaults.allowFriendJoin == 0);
	assert(defaults.mapMarkerSize == 1);
	assert(defaults.friendRequests == 0);
	assert(defaults.regionPreference == 0);
	assert(defaults.interpolationQuality == 2);
	assert(defaults.showLocationToFriends == 1);
	assert(defaults.showPlayerMarkers == 1);
	assert(defaults.showFriendMarkers == 1);
	assert(defaults.showNameplates == 1);
	assert(defaults.networkSmoothing == 1);
	assert(defaults.showPing == 1);
	assert(defaults.networkStatsOverlay == 0);

	assert(PreferencesStale(false, 0, 0, 0, 0));
	assert(!PreferencesStale(true, 7, 7, 5000, 5999));
	assert(PreferencesStale(true, 7, 7, 5000, 6000));
	assert(PreferencesStale(true, 7, 8, 5000, 5001));

	const nlohmann::json expectedDefault{ { "onlineStatus", "Friends" }, { "showLocation", true }, { "friendRequests", "Everyone" } };
	assert(PrivacyJson(defaults) == expectedDefault);

	auto changed = defaults;
	changed.onlineStatus = 2;
	changed.showLocationToFriends = 0;
	changed.friendRequests = 1;
	const nlohmann::json expectedChanged{ { "onlineStatus", "Nobody" }, { "showLocation", false }, { "friendRequests", "FriendsOfFriends" } };
	assert(PrivacyJson(changed) == expectedChanged);

	auto unknown = defaults;
	unknown.onlineStatus = 9;
	unknown.friendRequests = 9;
	assert(PrivacyJson(unknown)["onlineStatus"] == "Friends");
	assert(PrivacyJson(unknown)["friendRequests"] == "Everyone");

	const auto packet = PrivacySetPacket(changed);
	assert(packet["type"] == "privacySet");
	assert(packet["privacy"] == expectedChanged);
	assert(packet["privacy"].size() == 3);

	assert(ValuesOf(defaults) == ValuesOf(DefaultPreferences()));
	auto nameplates = defaults;
	nameplates.showNameplates = 0;
	assert(ValuesOf(nameplates) != ValuesOf(defaults));

	return 0;
}
