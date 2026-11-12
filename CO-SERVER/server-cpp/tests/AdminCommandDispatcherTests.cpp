#include "AdminCommandDispatcher.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

int main()
{
	using namespace CommonwealthOnline::Server;
	using Json = nlohmann::json;

	std::uint32_t kicked = 0;
	std::uint32_t revived = 0;
	std::uint32_t killed = 0;
	std::string resetProfile;
	std::string bannedProfile;
	std::string unbannedProfile;
	std::string setTime;
	std::string setWeather;
	std::string questSelector;
	std::string setWorldMode;
	Json raidPatch;
	int raidSets = 0;
	bool reloaded = false;
	bool shutdownRequested = false;
	std::string lastItemOp;
	int itemCalls = 0;
	std::uint32_t inventoryRequestedFor = 0;
	int inventoryCalls = 0;
	std::string disbandedFaction;
	std::string promotedFaction;
	std::string promotedProfile;
	std::string kickedFaction;
	std::string kickedProfile;
	std::uint32_t voiceMutedPlayer = 0;
	std::string voiceMutedProfile;
	std::optional<double> voiceMuteSeconds;
	std::uint32_t voiceUnmutedPlayer = 0;
	std::string voiceUnmutedProfile;
	AdminCommandDispatcher dispatcher(AdminCommandHooks{
		.shutdown = [&] {
			shutdownRequested = true;
			return AdminActionResult{ true, "shutdown requested", nullptr };
		},
		.status = [] { return Json{ { "connected_clients", 2 }, { "is_running", true } }; },
		.clients = [] { return Json{ { "total_clients", 1 }, { "clients", Json::array({ Json{ { "player_id", 7 } } }) } }; },
		.bans = [] { return Json{ { "bans", Json::array() } }; },
		.kick = [&](std::uint32_t playerId, std::string_view reason) {
			kicked = playerId;
			return AdminActionResult{ true, "kicked " + std::string{ reason }, Json{ { "playerId", playerId } } };
		},
		.banPlayer = [](std::uint32_t playerId, std::string_view) {
			return AdminActionResult{ true, "banned player", Json{ { "playerId", playerId } } };
		},
		.banIp = [](std::string_view ip, std::string_view reason) {
			return AdminActionResult{ true, "banned ip", Json{ { "ip", ip }, { "reason", reason } } };
		},
		.unban = [](std::string_view ip) {
			return AdminActionResult{ true, "unbanned", Json{ { "ip", ip } } };
		},
		.banProfile = [&](std::string_view profileId, std::string_view reason) {
			bannedProfile = profileId;
			return AdminActionResult{ true, "profile banned", Json{ { "profileId", profileId }, { "reason", reason }, { "sessionsEnded", 2 } } };
		},
		.unbanProfile = [&](std::string_view profileId) {
			unbannedProfile = profileId;
			return AdminActionResult{ true, "profile unbanned", Json{ { "profileId", profileId } } };
		},
		.resetIdentity = [&](std::string_view profileId) {
			resetProfile = profileId;
			return AdminActionResult{ true, "reset", Json{ { "profileId", profileId } } };
		},
		.revive = [&](std::uint32_t playerId) {
			revived = playerId;
			return AdminActionResult{ true, "revived", Json{ { "playerId", playerId } } };
		},
		.killPlayer = [&](std::uint32_t playerId) {
			killed = playerId;
			return AdminActionResult{ true, "killed", Json{ { "playerId", playerId } } };
		},
		.voiceMute = [&](std::optional<std::uint32_t> playerId, std::string_view profileId, bool muted, std::optional<double> seconds) {
			if (!muted) {
				voiceUnmutedPlayer = playerId.value_or(0);
				voiceUnmutedProfile = profileId;
				return AdminActionResult{ true, "voice unmuted", Json{ { "profileId", profileId } } };
			}
			voiceMutedPlayer = playerId.value_or(0);
			voiceMutedProfile = profileId;
			voiceMuteSeconds = seconds;
			return AdminActionResult{ true, "voice muted", Json{ { "profileId", profileId } } };
		},
		.setWorldTime = [&](std::string_view value) {
			setTime = value;
			return AdminActionResult{ true, "time set", nullptr };
		},
		.setWorldWeather = [&](std::string_view value) {
			setWeather = value;
			return AdminActionResult{ true, "weather set", nullptr };
		},
		.questMode = [](std::string_view mode) {
			return AdminActionResult{ true, "mode", Json{ { "mode", mode } } };
		},
		.questGate = [](const F4T::QuestProtocol::FormKey&, std::uint16_t, bool) {
			return AdminActionResult{ true, "gate", nullptr };
		},
		.questSolo = [&](std::string_view selector, const F4T::QuestProtocol::FormKey&, bool) {
			questSelector = selector;
			return AdminActionResult{ true, "solo", Json{ { "selector", selector } } };
		},
		.worldMode = [] { return Json{ { "mode", "coop-story" }, { "doors", "shared" } }; },
		.setWorldMode = [&](std::string_view value) {
			setWorldMode = value;
			return AdminActionResult{ true, "world mode set", Json{ { "mode", value } } };
		},
		.reloadWorldPolicy = [&] {
			reloaded = true;
			return AdminActionResult{ true, "world policy reloaded", Json{ { "entries", 6 } } };
		},
		.raidSettings = [] { return Json{ { "mode", "open" }, { "windowStartHour", 22 }, { "windowDurationHours", 2 } }; },
		.setRaidSettings = [&](const Json& a_patch) {
			raidPatch = a_patch;
			++raidSets;
			return AdminActionResult{ true, "raid settings updated", a_patch };
		},
		.factions = [] { return Json{ { "factions", Json::array({ Json{ { "factionId", "f-1" } } }) }, { "enabled", true } }; },
		.disbandFaction = [&](std::string_view factionId) {
			disbandedFaction = factionId;
			if (factionId == "f-404") return AdminActionResult{ false, "No faction with id f-404.", nullptr };
			return AdminActionResult{ true, "Disbanded faction " + std::string{ factionId } + ".", Json{ { "factionId", factionId } } };
		},
		.promoteFaction = [&](std::string_view factionId, std::string_view profileId) {
			promotedFaction = factionId;
			promotedProfile = profileId;
			return AdminActionResult{ true, "promoted", Json{ { "factionId", factionId } } };
		},
		.kickFaction = [&](std::string_view factionId, std::string_view profileId) {
			kickedFaction = factionId;
			kickedProfile = profileId;
			return AdminActionResult{ true, "kicked", Json{ { "factionId", factionId } } };
		},
		.reports = [] { return Json{ { "reports", Json::array({ Json{ { "reportId", "rp-1" } } }) }, { "total", 1 } }; },
		.spawnObject = [&](std::uint32_t playerId, std::string_view itemId, std::uint32_t count) {
			++itemCalls;
			lastItemOp = "spawn:" + std::to_string(playerId) + ":" + std::string{ itemId } + ":" + std::to_string(count);
			return AdminActionResult{ true, "spawned", nullptr };
		},
		.giveItem = [&](std::uint32_t playerId, std::string_view itemId, std::uint32_t count) {
			++itemCalls;
			lastItemOp = "give:" + std::to_string(playerId) + ":" + std::string{ itemId } + ":" + std::to_string(count);
			return AdminActionResult{ true, "given", nullptr };
		},
		.takeItem = [&](std::uint32_t playerId, std::string_view itemId, std::uint32_t count) {
			++itemCalls;
			lastItemOp = "take:" + std::to_string(playerId) + ":" + std::string{ itemId } + ":" + std::to_string(count);
			return AdminActionResult{ true, "taken", nullptr };
		},
		.requestInventory = [&](std::uint32_t playerId) {
			++inventoryCalls;
			inventoryRequestedFor = playerId;
			return AdminActionResult{ true, "inventory requested", Json{ { "opId", 7 } } };
		},
		.getInventory = [](std::uint32_t playerId) {
			return AdminActionResult{ true, "inventory", Json{
				{ "playerId", playerId },
				{ "opId", 7 },
				{ "ageSeconds", 1.5 },
				{ "truncated", false },
				{ "items", Json::array() }
			} };
		},
		.getOpResult = [](std::uint32_t playerId) {
			if (playerId != 9) {
				return AdminActionResult{ false, "No result yet.", nullptr };
			}
			return AdminActionResult{ true, "result", Json{
				{ "playerId", playerId },
				{ "opId", 7 },
				{ "ok", true },
				{ "applied", 2 },
				{ "error", "" }
			} };
		}
	});

	auto response = dispatcher.Dispatch(Json{ { "cmd", "ping" }, { "id", 1 } });
	assert(response.value("ok", false));
	assert(response.value("id", 0) == 1);
	assert(response.at("data").value("pong", false));

	response = dispatcher.Dispatch(Json{ { "cmd", "shutdown" } });
	assert(response.value("ok", false));
	assert(shutdownRequested);

	response = dispatcher.Dispatch(Json{ { "command", " STATUS " } });
	assert(response.value("ok", false));
	assert(response.at("data").value("connected_clients", 0) == 2);

	response = dispatcher.Dispatch(Json{ { "cmd", "users" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("total_clients", 0) == 1);

	response = dispatcher.Dispatch(Json{ { "cmd", "reports" } });
	assert(response.value("ok", false));
	assert(response.at("data").at("reports").at(0).at("reportId") == "rp-1");
	assert(response.at("data").value("total", 0) == 1);

	response = dispatcher.Dispatch(Json{ { "cmd", "kick" }, { "player_id", 19 }, { "reason", "test" } });
	assert(response.value("ok", false));
	assert(kicked == 19);
	assert(response.at("data").value("playerId", 0) == 19);

	response = dispatcher.Dispatch(Json{ { "cmd", "kick" }, { "playerId", 20.0 } });
	assert(response.value("ok", false));
	assert(kicked == 20);

	response = dispatcher.Dispatch(Json{ { "cmd", "kick" }, { "playerId", nullptr }, { "player_id", 21 } });
	assert(response.value("ok", false));
	assert(kicked == 21);

	response = dispatcher.Dispatch(Json{ { "cmd", "kick" }, { "playerId", "invalid" }, { "player_id", 22 } });
	assert(!response.value("ok", true));
	assert(kicked == 21);

	response = dispatcher.Dispatch(Json{ { "cmd", 123 }, { "command", "ping" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("(empty)") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", nullptr }, { "command", "ping" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("pong", false));

	response = dispatcher.Dispatch(Json{ { "cmd", "ban" }, { "playerId", 4 } });
	assert(response.value("ok", false));
	assert(response.at("data").value("playerId", 0) == 4);

	response = dispatcher.Dispatch(Json{ { "cmd", "ban" }, { "ip", "127.0.0.2" }, { "reason", "bad" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("ip", std::string{}) == "127.0.0.2");

	response = dispatcher.Dispatch(Json{ { "cmd", "unban" }, { "ip", "127.0.0.2" } });
	assert(response.value("ok", false));

	response = dispatcher.Dispatch(Json{ { "cmd", "resume_reset" }, { "profile_id", "profile-a" } });
	assert(response.value("ok", false));
	assert(resetProfile == "profile-a");

	response = dispatcher.Dispatch(Json{ { "cmd", "resume_reset" }, { "profileId", nullptr }, { "profile_id", "profile-b" } });
	assert(response.value("ok", false));
	assert(resetProfile == "profile-b");

	response = dispatcher.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", "profile-c" }, { "ban", true }, { "reason", "test-ban" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("banned", false));
	assert(response.at("data").value("credentialReset", false));
	assert(response.at("data").value("sessionsEnded", 0) == 2);
	assert(bannedProfile == "profile-c");

	response = dispatcher.Dispatch(Json{ { "cmd", "profile_unban" }, { "profile_id", "profile-c" } });
	assert(response.value("ok", false));
	assert(unbannedProfile == "profile-c");

	response = dispatcher.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", "profile-c" }, { "ban", "true" } });
	assert(!response.value("ok", true));

	response = dispatcher.Dispatch(Json{ { "cmd", "resume_reset" }, { "profileId", 42 }, { "profile_id", "profile-c" } });
	assert(!response.value("ok", true));
	assert(resetProfile == "profile-c");

	response = dispatcher.Dispatch(Json{ { "cmd", "revive" }, { "player_id", 31 } });
	assert(response.value("ok", false));
	assert(revived == 31);
	assert(response.at("data").value("playerId", 0) == 31);

	response = dispatcher.Dispatch(Json{ { "cmd", "revive" }, { "playerId", 32 } });
	assert(response.value("ok", false));
	assert(revived == 32);

	response = dispatcher.Dispatch(Json{ { "cmd", "revive" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("playerId") != std::string::npos);
	assert(revived == 32);

	response = dispatcher.Dispatch(Json{ { "cmd", "kill" }, { "playerId", 33 } });
	assert(response.value("ok", false));
	assert(killed == 33);

	response = dispatcher.Dispatch(Json{ { "cmd", "kill" }, { "playerId", "abc" } });
	assert(!response.value("ok", true));
	assert(killed == 33);

	AdminCommandDispatcher noDowned(AdminCommandHooks{});
	response = noDowned.Dispatch(Json{ { "cmd", "revive" }, { "playerId", 34 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "world_time" }, { "time", "0630" } });
	assert(response.value("ok", false));
	assert(setTime == "0630");

	response = dispatcher.Dispatch(Json{ { "cmd", "world_weather" }, { "fw", "0002b52a" } });
	assert(response.value("ok", false));
	assert(setWeather == "0002b52a");

	response = dispatcher.Dispatch(Json{ { "cmd", "world_mode" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("mode", std::string{}) == "coop-story");

	response = dispatcher.Dispatch(Json{ { "cmd", "world_mode" }, { "mode", nullptr } });
	assert(response.value("ok", false));
	assert(response.at("data").value("mode", std::string{}) == "coop-story");

	response = dispatcher.Dispatch(Json{ { "cmd", "world_policy" }, { "mode", " free-roam " } });
	assert(response.value("ok", false));
	assert(setWorldMode == "free-roam");
	assert(response.at("data").value("mode", std::string{}) == "free-roam");

	response = dispatcher.Dispatch(Json{ { "cmd", "world_policy_reload" } });
	assert(response.value("ok", false));
	assert(reloaded);
	assert(response.at("data").value("entries", 0) == 6);

	response = dispatcher.Dispatch(Json{ { "cmd", "raid_settings" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("mode", std::string{}) == "open");
	assert(response.at("data").value("windowStartHour", 0) == 22);

	response = dispatcher.Dispatch(Json{ { "cmd", "raid_set" }, { "mode", "off" }, { "windowDurationHours", 6 } });
	assert(response.value("ok", false));
	assert(raidSets == 1);
	assert(raidPatch == (Json{ { "mode", "off" }, { "windowDurationHours", 6 } }));

	response = dispatcher.Dispatch(Json{ { "cmd", "raid_set" }, { "mode", nullptr }, { "bogus", 1 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("raid_set requires") != std::string::npos);
	assert(raidSets == 1);

	response = dispatcher.Dispatch(Json{ { "cmd", "factions" } });
	assert(response.value("ok", false));
	assert(response.at("data").at("factions").size() == 1);
	assert(response.at("data").value("enabled", false));

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_disband" }, { "factionId", "f-7" } });
	assert(response.value("ok", false));
	assert(disbandedFaction == "f-7");
	assert(response.at("data").value("factionId", std::string{}) == "f-7");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_disband" }, { "faction_id", " f-8 " } });
	assert(response.value("ok", false));
	assert(disbandedFaction == "f-8");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_disband" }, { "factionId", "f-404" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("No faction with id f-404.") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_disband" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("factionId") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_disband" }, { "factionId", "  " } });
	assert(!response.value("ok", true));
	assert(disbandedFaction == "f-404");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_promote" }, { "factionId", "f-7" }, { "profileId", "profile-leader-1" } });
	assert(response.value("ok", false));
	assert(promotedFaction == "f-7" && promotedProfile == "profile-leader-1");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_promote" }, { "faction_id", " f-8 " }, { "profile_id", " profile-leader-2 " } });
	assert(response.value("ok", false));
	assert(promotedFaction == "f-8" && promotedProfile == "profile-leader-2");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_promote" }, { "factionId", "  " }, { "profileId", "profile-leader-3" } });
	assert(!response.value("ok", true));
	assert(promotedFaction == "f-8");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_promote" }, { "factionId", "f-7" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("profileId") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_promote" }, { "factionId", "f-7" }, { "profileId", "bad id" } });
	assert(!response.value("ok", true));
	assert(promotedProfile == "profile-leader-2");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_kick" }, { "factionId", "f-7" }, { "profileId", "profile-member-1" } });
	assert(response.value("ok", false));
	assert(kickedFaction == "f-7" && kickedProfile == "profile-member-1");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_kick" }, { "faction_id", "f-9" }, { "profile_id", " profile-member-2 " } });
	assert(response.value("ok", false));
	assert(kickedFaction == "f-9" && kickedProfile == "profile-member-2");

	response = dispatcher.Dispatch(Json{ { "cmd", "faction_kick" }, { "factionId", "f-7" }, { "profileId", "x" } });
	assert(!response.value("ok", true));
	assert(kickedProfile == "profile-member-2");

	AdminCommandDispatcher noFactions(AdminCommandHooks{});
	response = noFactions.Dispatch(Json{ { "cmd", "factions" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);
	response = noFactions.Dispatch(Json{ { "cmd", "faction_disband" }, { "factionId", "f-1" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);
	response = noFactions.Dispatch(Json{ { "cmd", "faction_promote" }, { "factionId", "f-1" }, { "profileId", "profile-leader-1" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);
	response = noFactions.Dispatch(Json{ { "cmd", "faction_kick" }, { "factionId", "f-1" }, { "profileId", "profile-member-1" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	AdminCommandDispatcher unavailableWorld(AdminCommandHooks{});
	response = unavailableWorld.Dispatch(Json{ { "cmd", "world_mode" } });
	assert(!response.value("ok", true));
	response = unavailableWorld.Dispatch(Json{ { "cmd", "world_mode" }, { "mode", "free-roam" } });
	assert(!response.value("ok", true));
	response = unavailableWorld.Dispatch(Json{ { "cmd", "world_policy_reload" } });
	assert(!response.value("ok", true));

	AdminCommandDispatcher unavailableRaid(AdminCommandHooks{});
	response = unavailableRaid.Dispatch(Json{ { "cmd", "raid_settings" } });
	assert(!response.value("ok", true));
	response = unavailableRaid.Dispatch(Json{ { "cmd", "raid_set" }, { "mode", "off" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "kick" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("playerId") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "unknown" }, { "id", "abc" } });
	assert(!response.value("ok", true));
	assert(response.value("id", std::string{}) == "abc");

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "playerId", 12 }, { "quest", "0A1B2C:Fallout4.esm" }, { "solo", true } });
	assert(response.value("ok", false));
	assert(questSelector == "12");
	assert(response.at("data").value("selector", std::string{}) == "12");

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "profileId", "90210" }, { "quest", "0A1B2C:Fallout4.esm" } });
	assert(response.value("ok", false));
	assert(questSelector == "profile:90210");
	assert(response.at("data").value("selector", std::string{}) == "profile:90210");

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "profile_id", "  profile-a  " }, { "quest", "0a1b2c:fallout4.esm" } });
	assert(response.value("ok", false));
	assert(questSelector == "profile:profile-a");

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "playerId", nullptr }, { "profile_id", "profile-b" }, { "quest", "0A1B2C:Fallout4.esm" } });
	assert(response.value("ok", false));
	assert(questSelector == "profile:profile-b");

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "quest", "0A1B2C:Fallout4.esm" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("playerId or profileId") != std::string::npos);

	const auto selectorBeforeNegative = questSelector;
	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "playerId", -1 }, { "quest", "0A1B2C:Fallout4.esm" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("playerId or profileId") != std::string::npos);
	assert(questSelector == selectorBeforeNegative);

	response = dispatcher.Dispatch(Json{ { "cmd", "quest_solo" }, { "playerId", 0 }, { "quest", "0A1B2C:Fallout4.esm" } });
	assert(!response.value("ok", true));
	assert(questSelector == selectorBeforeNegative);

	assert(ResolveProfileSelector("profile:90210", [](std::uint32_t) { return std::optional<std::string>{ "wrong" }; }) == std::optional<std::string>{ "90210" });
	assert(ResolveProfileSelector("profile:", [](std::uint32_t) { return std::optional<std::string>{ "wrong" }; }) == std::nullopt);
	assert(ResolveProfileSelector("12", [](std::uint32_t a_playerId) {
		return a_playerId == 12 ? std::optional<std::string>{ "profile-a" } : std::nullopt;
	}) == std::optional<std::string>{ "profile-a" });
	assert(ResolveProfileSelector("13", [](std::uint32_t a_playerId) {
		return a_playerId == 12 ? std::optional<std::string>{ "profile-a" } : std::nullopt;
	}) == std::nullopt);
	assert(ResolveProfileSelector("profile-a", [](std::uint32_t) { return std::optional<std::string>{ "wrong" }; }) == std::nullopt);
	assert(ResolveProfileSelector("99999999999", [](std::uint32_t) { return std::optional<std::string>{ "wrong" }; }) == std::nullopt);
	assert(ResolveProfileSelector("", [](std::uint32_t) { return std::optional<std::string>{ "wrong" }; }) == std::nullopt);
	assert(ResolveProfileSelector(
		"profile:90210",
		[](std::uint32_t) { return std::optional<std::string>{ "wrong" }; },
		[](std::string_view a_profileId) { return a_profileId == "90210"; }) == std::optional<std::string>{ "90210" });
	assert(ResolveProfileSelector(
		"profile:90210",
		[](std::uint32_t) { return std::optional<std::string>{ "wrong" }; },
		[](std::string_view) { return false; }) == std::nullopt);
	assert(ResolveProfileSelector(
		"12",
		[](std::uint32_t a_playerId) -> std::optional<std::string> {
			return a_playerId == 12 ? std::optional<std::string>{ "profile-a" } : std::nullopt;
		},
		[](std::string_view) { return false; }) == std::optional<std::string>{ "profile-a" });

	{
		AdminCommandDispatcher unavailableRecovery(AdminCommandHooks{});
		assert(!unavailableRecovery.Dispatch(Json{ { "cmd", "save-recovery-resolve" } }).value("ok", true));
		AdminCommandHooks recoveryHooks;
		recoveryHooks.saveRecoveryInspect = [](const Json& request) {
			return AdminActionResult{ true, "inspected", Json{ { "profileId", request.at("profileId") } } };
		};
		recoveryHooks.saveRecoveryResolve = [](const Json& request) {
			return AdminActionResult{ request.value("confirm", std::string{}) == "save-and-inventory-verified", "resolved", nullptr };
		};
		AdminCommandDispatcher recovery(std::move(recoveryHooks));
		assert(recovery.Dispatch(Json{ { "cmd", "save-recovery-inspect" }, { "profileId", "p" } }).value("ok", false));
		assert(!recovery.Dispatch(Json{ { "cmd", "save-recovery-resolve" }, { "confirm", "no" } }).value("ok", true));
		assert(recovery.Dispatch(Json{ { "cmd", "save-recovery-resolve" }, { "confirm", "save-and-inventory-verified" } }).value("ok", false));
	}

	AdminCommandDispatcher unavailable(AdminCommandHooks{});
	response = unavailable.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", "profile-00" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("not enabled") != std::string::npos);

	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 3 }, { "itemId", "0002079e" }, { "count", 3 } });
	assert(response.value("ok", false));
	assert(lastItemOp == "spawn:3:0002079E:3");

	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "player_id", 4 }, { "itemId", "0002079E" } });
	assert(response.value("ok", false));
	assert(lastItemOp == "spawn:4:0002079E:1");

	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 4 }, { "itemId", "0002079E" }, { "count", 10 } });
	assert(response.value("ok", false));
	assert(lastItemOp == "spawn:4:0002079E:10");

	const int callsBeforeRejects = itemCalls;
	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 4 }, { "itemId", "0002079E" }, { "count", 11 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "give_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", 9999 } });
	assert(response.value("ok", false));
	assert(lastItemOp == "give:5:0000000F:9999");
	response = dispatcher.Dispatch(Json{ { "cmd", "give_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", 10000 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", 0 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", -1 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", true } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", "2" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", 2.5 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "playerId", 5 }, { "itemId", "0000000F" }, { "count", 7 } });
	assert(response.value("ok", false));
	assert(lastItemOp == "take:5:0000000F:7");
	assert(itemCalls == callsBeforeRejects + 2);

	const int callsBeforeItemIdRejects = itemCalls;
	const std::string lastBeforeItemIdRejects = lastItemOp;
	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 6 }, { "itemId", "0002079" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 6 }, { "itemId", "0002079G" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 6 }, { "itemId", "00000000" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 6 }, { "itemId", 2079 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "give_item" }, { "playerId", 6 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "take_item" }, { "itemId", "0002079E" }, { "count", 1 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("playerId") != std::string::npos);
	assert(itemCalls == callsBeforeItemIdRejects);
	assert(lastItemOp == lastBeforeItemIdRejects);

	response = dispatcher.Dispatch(Json{ { "cmd", "request_inventory" }, { "player_id", 9 } });
	assert(response.value("ok", false));
	assert(inventoryCalls == 1);
	assert(inventoryRequestedFor == 9);
	assert(response.at("data").value("opId", 0) == 7);

	response = dispatcher.Dispatch(Json{ { "cmd", "request_inventory" } });
	assert(!response.value("ok", true));
	assert(inventoryCalls == 1);

	response = dispatcher.Dispatch(Json{ { "cmd", "get_inventory" }, { "playerId", 9 } });
	assert(response.value("ok", false));
	assert(response.at("data").value("playerId", 0) == 9);
	assert(response.at("data").contains("ageSeconds"));
	assert(response.at("data").contains("truncated"));
	assert(response.at("data").at("items").is_array());
	assert(response.at("data").value("opId", 0) == 7);

	response = dispatcher.Dispatch(Json{ { "cmd", "get_op_result" }, { "playerId", 9 } });
	assert(response.value("ok", false));
	assert(response.at("data").value("opId", 0) == 7);
	assert(response.at("data").value("applied", 0) == 2);
	assert(response.at("data").value("ok", false));

	response = dispatcher.Dispatch(Json{ { "cmd", "get_op_result" }, { "playerId", 4 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}) == "No result yet.");

	AdminCommandDispatcher noItemHooks(AdminCommandHooks{});
	response = noItemHooks.Dispatch(Json{ { "cmd", "spawn_object" }, { "playerId", 3 }, { "itemId", "0002079E" } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);
	response = noItemHooks.Dispatch(Json{ { "cmd", "get_inventory" }, { "playerId", 3 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	response = noItemHooks.Dispatch(Json{ { "cmd", "get_op_result" }, { "playerId", 3 } });
	assert(!response.value("ok", true));
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	AdminCommandDispatcher partial(AdminCommandHooks{
		.banProfile = [](std::string_view profileId, std::string_view) {
			return AdminActionResult{ true, "profile banned", Json{ { "profileId", profileId }, { "sessionsEnded", 1 } } };
		},
		.resetIdentity = [](std::string_view) {
			return AdminActionResult{ false, "credential store unavailable", nullptr };
		}
	});
	response = partial.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", "profile-partial" }, { "ban", true }, { "reason", "partial-test" } });
	assert(response.value("ok", false));
	assert(response.at("data").value("banned", false));
	assert(!response.at("data").value("credentialReset", true));
	assert(response.at("data").value("credentialResetReason", std::string{}) == "credential store unavailable");
	assert(response.at("data").value("sessionsEnded", 0) == 1);

	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "playerId", 7 } });
	assert(response.value("ok", false));
	assert(voiceMutedPlayer == 7 && voiceMutedProfile.empty() && !voiceMuteSeconds);

	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "profile_id", " profile-voice " }, { "seconds", 30.0 } });
	assert(response.value("ok", false));
	assert(voiceMutedPlayer == 0 && voiceMutedProfile == "profile-voice" && voiceMuteSeconds == 30.0);

	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "playerId", "x" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "profileId", "bad profile!!" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "playerId", 8 }, { "seconds", 0 } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_mute" }, { "playerId", 8 }, { "seconds", "soon" } });
	assert(!response.value("ok", true));
	assert(voiceMutedPlayer == 0 && voiceMutedProfile == "profile-voice" && voiceMuteSeconds == 30.0);

	response = dispatcher.Dispatch(Json{ { "cmd", "voice_unmute" }, { "playerId", 9 } });
	assert(response.value("ok", false));
	assert(voiceUnmutedPlayer == 9);
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_unmute" }, { "profile_id", "profile-voice" } });
	assert(response.value("ok", false));
	assert(voiceUnmutedProfile == "profile-voice");
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_unmute" } });
	assert(!response.value("ok", true));
	response = dispatcher.Dispatch(Json{ { "cmd", "voice_unmute" }, { "playerId", "x" } });
	assert(!response.value("ok", true));
	assert(voiceUnmutedPlayer == 0 && voiceUnmutedProfile == "profile-voice");

	constexpr auto kSteamId = "76561198000000042";
	std::string lookedUpSteam;
	std::string lookedUpProfile;
	std::string steamBanned;
	std::string steamBanReason;
	std::uint32_t steamBannedPlayer = 0;
	std::string steamUnbanned;
	std::string clearedProfile;
	std::string clearedSteam;
	AdminCommandDispatcher steam(AdminCommandHooks{
		.steamLookup = [&](std::string_view steamId, std::string_view profileId) {
			lookedUpSteam = steamId;
			lookedUpProfile = profileId;
			return AdminActionResult{ true, "found", Json{ { "records", Json::array() } } };
		},
		.banSteamId = [&](std::string_view steamId, std::string_view reason) {
			steamBanned = steamId;
			steamBanReason = reason;
			return AdminActionResult{ true, "steam banned", Json{ { "steamId", steamId } } };
		},
		.banPlayerSteamId = [&](std::uint32_t playerId, std::string_view) {
			steamBannedPlayer = playerId;
			return AdminActionResult{ false, "Player has no verified Steam ID.", nullptr };
		},
		.unbanSteamId = [&](std::string_view steamId) {
			steamUnbanned = steamId;
			return AdminActionResult{ true, "steam unbanned", nullptr };
		},
		.steamFlags = [] { return Json{ { "total", 1 }, { "flags", Json::array({ Json{ { "kind", "shared_steam_id" } } }) } }; },
		.clearSteamFlags = [&](std::string_view profileId, std::string_view steamId) {
			clearedProfile = profileId;
			clearedSteam = steamId;
			return AdminActionResult{ true, "cleared", Json{ { "cleared", 1 } } };
		}
	});
	response = steam.Dispatch(Json{ { "cmd", "steam_lookup" }, { "steamId", std::string{ " " } + kSteamId + " " } });
	assert(response.value("ok", false) && lookedUpSteam == kSteamId && lookedUpProfile.empty());
	response = steam.Dispatch(Json{ { "cmd", "steam_lookup" }, { "profile_id", "profile-lookup" } });
	assert(response.value("ok", false) && lookedUpSteam.empty() && lookedUpProfile == "profile-lookup");
	for (const auto& bad : { Json{ { "cmd", "steam_lookup" } },
			 Json{ { "cmd", "steam_lookup" }, { "steamId", kSteamId }, { "profileId", "profile-lookup" } },
			 Json{ { "cmd", "steam_lookup" }, { "steamId", 76561198000000042ull } },
			 Json{ { "cmd", "steam_lookup" }, { "steamId", "76561197960265728" } },
			 Json{ { "cmd", "steam_ban" }, { "steamId", "7656119800000004" } },
			 Json{ { "cmd", "steam_unban" } },
			 Json{ { "cmd", "steam_flag_clear" }, { "steamId", kSteamId } },
			 Json{ { "cmd", "steam_flag_clear" }, { "profileId", "profile-lookup" }, { "steamId", "x" } } }) {
		lookedUpSteam = lookedUpProfile = "untouched";
		response = steam.Dispatch(bad);
		assert(!response.value("ok", true) && lookedUpSteam == "untouched" && lookedUpProfile == "untouched");
	}
	assert(steamBanned.empty() && steamUnbanned.empty() && clearedProfile.empty());

	response = steam.Dispatch(Json{ { "cmd", "steam_ban" }, { "steamId", kSteamId }, { "reason", "cheating" } });
	assert(response.value("ok", false) && steamBanned == kSteamId && steamBanReason == "cheating");
	response = steam.Dispatch(Json{ { "cmd", "steam_ban" }, { "playerId", 9 } });
	assert(!response.value("ok", true) && steamBannedPlayer == 9);
	assert(response.value("error", std::string{}).find("no verified Steam ID") != std::string::npos);
	response = steam.Dispatch(Json{ { "cmd", "steam_unban" }, { "steam_id", kSteamId } });
	assert(response.value("ok", false) && steamUnbanned == kSteamId);
	response = steam.Dispatch(Json{ { "cmd", "steam_flags" } });
	assert(response.value("ok", false) && response.at("data").value("total", 0) == 1);
	response = steam.Dispatch(Json{ { "cmd", "steam_flag_clear" }, { "profileId", "profile-flagged" }, { "steamId", kSteamId } });
	assert(response.value("ok", false) && clearedProfile == "profile-flagged" && clearedSteam == kSteamId);

	for (const auto* command : { "steam_lookup", "steam_ban", "steam_unban", "steam_flags", "steam_flag_clear" }) {
		response = unavailable.Dispatch(Json{ { "cmd", command }, { "steamId", kSteamId }, { "profileId", "profile-flagged" } });
		assert(!response.value("ok", true));
	}
	response = unavailable.Dispatch(Json{ { "cmd", "steam_ban" }, { "steamId", kSteamId } });
	assert(response.value("error", std::string{}).find("unavailable") != std::string::npos);

	std::string resourceCall;
	AdminCommandHooks resourceHooks;
	resourceHooks.resources = [] { return Json{ { "resources", Json::array({ "demo" }) } }; };
	resourceHooks.ensureResource = [&resourceCall](std::string_view a_name) { resourceCall = "ensure:" + std::string{ a_name }; return AdminActionResult{ true, "ensured", {} }; };
	resourceHooks.stopResource = [&resourceCall](std::string_view a_name) { resourceCall = "stop:" + std::string{ a_name }; return AdminActionResult{ true, "stopped", {} }; };
	resourceHooks.refreshResources = [&resourceCall] { resourceCall = "refresh"; return AdminActionResult{ true, "refreshed", {} }; };
	resourceHooks.approveResource = [&resourceCall](std::string_view a_name) { resourceCall = "approve:" + std::string{ a_name }; return AdminActionResult{ true, "approved", {} }; };
	AdminCommandDispatcher resourceDispatcher(resourceHooks);
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "resources" } });
	assert(response.value("ok", false) && response.at("data").at("resources").at(0) == "demo");
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "ensure" }, { "name", "demo" } });
	assert(response.value("ok", false) && resourceCall == "ensure:demo");
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "stop" }, { "name", "demo" } });
	assert(response.value("ok", false) && resourceCall == "stop:demo");
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "approve" }, { "name", "demo" } });
	assert(response.value("ok", false) && resourceCall == "approve:demo");
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "refresh" } });
	assert(response.value("ok", false) && resourceCall == "refresh");
	response = resourceDispatcher.Dispatch(Json{ { "cmd", "ensure" } });
	assert(!response.value("ok", true));

	{
		AdminCommandHooks archiveHooks;
		archiveHooks.deathBagArchive = [](std::uint32_t a_offset) { return Json{ { "total", 1u }, { "offset", a_offset }, { "bags", Json::array({ Json{ { "deathId", "0123456789abcdef0123456789abcdef" } } }) } }; };
		archiveHooks.deathBagArchiveRecover = [](std::string_view a_deathId) {
			return AdminActionResult{ a_deathId == "0123456789abcdef0123456789abcdef", "recovered", Json{ { "deathId", std::string{ a_deathId } } } };
		};
		archiveHooks.deathBagArchiveResolve = [](std::string_view a_deathId, bool a_noneReturned) {
			return AdminActionResult{ a_deathId == "0123456789abcdef0123456789abcdef" && a_noneReturned, "resolved", nullptr };
		};
		AdminCommandDispatcher archiveDispatcher(std::move(archiveHooks));
		const auto listed = archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_list" } });
		assert(listed.value("ok", false) && listed.at("data").at("bags").at(0).at("deathId") == "0123456789abcdef0123456789abcdef");
		assert(listed.at("data").at("offset") == 0u);
		assert(archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_list" }, { "offset", 3u } }).at("data").at("offset") == 3u);
		assert(!archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_recover" } }).value("ok", true));
		assert(archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_recover" }, { "deathId", "0123456789abcdef0123456789abcdef" } }).value("ok", false));
		assert(archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_recover" }, { "death_id", "0123456789abcdef0123456789abcdef" } }).value("ok", false));
		assert(!archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_recover" }, { "deathId", "bogus" } }).value("ok", true));
		assert(archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_resolve" }, { "deathId", "0123456789abcdef0123456789abcdef" }, { "returned", "none" } }).value("ok", false));
		assert(!archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_resolve" }, { "deathId", "0123456789abcdef0123456789abcdef" }, { "returned", "all" } }).value("ok", true));
		assert(!archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_resolve" }, { "deathId", "0123456789abcdef0123456789abcdef" }, { "returned", "some" } }).value("ok", true));
		assert(!archiveDispatcher.Dispatch(Json{ { "cmd", "death_bag_archive_resolve" }, { "deathId", "0123456789abcdef0123456789abcdef" } }).value("ok", true));
		assert(!AdminCommandDispatcher(AdminCommandHooks{}).Dispatch(Json{ { "cmd", "death_bag_archive_resolve" }, { "deathId", "0123456789abcdef0123456789abcdef" }, { "returned", "none" } }).value("ok", true));
	}

	return 0;
}
