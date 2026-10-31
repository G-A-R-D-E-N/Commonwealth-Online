#include "AdminCommandDispatcher.h"

#include "AdminControlServer.h"
#include "BanStore.h"
#include "F4TSteamId.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server
{
	std::optional<std::string> ResolveProfileSelector(
		std::string_view a_selector,
		const std::function<std::optional<std::string>(std::uint32_t)>& a_lookupByPlayerId,
		const KnownProfileLookup& a_isKnownProfile)
	{
		if (a_selector.substr(0, kProfileSelectorPrefix.size()) == kProfileSelectorPrefix) {
			const auto profileId = a_selector.substr(kProfileSelectorPrefix.size());
			if (profileId.empty()) return std::nullopt;
			auto resolved = std::string{ profileId };
			if (a_isKnownProfile && !a_isKnownProfile(resolved)) return std::nullopt;
			return resolved;
		}
		if (a_selector.empty() || a_selector.size() > 10) return std::nullopt;
		std::uint64_t playerId = 0;
		for (const auto character : a_selector) {
			if (character < '0' || character > '9') return std::nullopt;
			playerId = playerId * 10 + static_cast<std::uint64_t>(character - '0');
		}
		if (playerId > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
		return a_lookupByPlayerId(static_cast<std::uint32_t>(playerId));
	}

	namespace
	{
		using Json = nlohmann::json;

		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

		std::string Lower(std::string a_value)
		{
			std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return a_value;
		}

		const Json* Coalesce(const Json& a_request, std::string_view a_primary, std::string_view a_alias = {})
		{
			if (const auto primary = a_request.find(a_primary); primary != a_request.end() && !primary->is_null()) {
				return std::addressof(*primary);
			}
			if (!a_alias.empty()) {
				if (const auto alias = a_request.find(a_alias); alias != a_request.end() && !alias->is_null()) {
					return std::addressof(*alias);
				}
			}
			return nullptr;
		}

		std::optional<std::string> ReadString(const Json& a_request, std::string_view a_primary, std::string_view a_alias = {})
		{
			const auto* node = Coalesce(a_request, a_primary, a_alias);
			if (node == nullptr || !node->is_string()) {
				return std::nullopt;
			}
			return node->get<std::string>();
		}

		std::optional<std::string> ReadProfileId(const Json& a_request, std::string_view a_primary, std::string_view a_alias = {})
		{
			const auto raw = ReadString(a_request, a_primary, a_alias);
			return raw ? BanStore::NormalizeProfileId(*raw) : std::nullopt;
		}

		// A string only: SteamID64s exceed the integers JSON numbers carry exactly.
		std::optional<std::string> ReadSteamId(const Json& a_request)
		{
			const auto raw = ReadString(a_request, "steamId", "steam_id");
			if (!raw) return std::nullopt;
			auto value = Trim(*raw);
			return F4T::SteamId::ParseIndividual(value) ? std::optional{ std::move(value) } : std::nullopt;
		}

		std::optional<std::uint32_t> ReadUInt32(
			const Json& a_request,
			std::string_view a_primary,
			std::string_view a_alias = {})
		{
			const auto* node = Coalesce(a_request, a_primary, a_alias);
			if (node == nullptr || node->is_boolean()) {
				return std::nullopt;
			}
			try {
				if (node->is_number_unsigned()) {
					const auto value = node->get<std::uint64_t>();
					if (value >= 1 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (node->is_number_integer()) {
					const auto value = node->get<std::int64_t>();
					if (value >= 1 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (node->is_number_float()) {
					const auto value = node->get<double>();
					if (std::isfinite(value) && value == std::trunc(value) && value >= 1.0 &&
						value <= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::string> ReadPlayerSelector(const Json& a_request)
		{
			if (const auto playerId = ReadUInt32(a_request, "playerId", "player_id")) {
				return std::to_string(*playerId);
			}
			const auto profileId = ReadString(a_request, "profileId", "profile_id");
			if (!profileId) {
				return std::nullopt;
			}
			const auto trimmed = Trim(*profileId);
			return trimmed.empty() ? std::nullopt : std::optional<std::string>{ std::string{ kProfileSelectorPrefix } + trimmed };
		}

		std::optional<F4T::QuestProtocol::FormKey> ReadQuestKey(const Json& a_request)
		{
			const auto value = ReadString(a_request, "quest", "formKey");
			if (!value) {
				return std::nullopt;
			}
			return F4T::QuestProtocol::ParseFormKey(Trim(*value));
		}

		std::optional<std::uint16_t> ReadQuestStage(const Json& a_request)
		{
			const auto* node = Coalesce(a_request, "stage");
			return node == nullptr ? std::nullopt : F4T::QuestProtocol::ParseStage(*node);
		}

		std::string Command(const Json& a_request)
		{
			if (const auto command = ReadString(a_request, "cmd", "command")) {
				return Lower(Trim(*command));
			}
			return {};
		}
	}

	AdminCommandDispatcher::AdminCommandDispatcher(AdminCommandHooks a_hooks) :
		_hooks(std::move(a_hooks))
	{}

	nlohmann::json AdminCommandDispatcher::Dispatch(const nlohmann::json& a_request) const
	{
		if (!a_request.is_object()) {
			return AdminControlServer::Failure("Admin request must be an object.");
		}
		const auto command = Command(a_request);

		if (command == "shutdown") {
			if (!_hooks.shutdown) {
				return AdminControlServer::Failure("Shutdown command is unavailable.", &a_request);
			}
			return Action(_hooks.shutdown(), a_request);
		}

		if (command == "ping") {
			return AdminControlServer::Success(Json{ { "pong", true } }, {}, &a_request);
		}
		if (command == "status" || command == "stats") {
			if (!_hooks.status) {
				return AdminControlServer::Failure("Status command is unavailable.", &a_request);
			}
			return AdminControlServer::Success(_hooks.status(), {}, &a_request);
		}
		if (command == "clients" || command == "users") {
			if (!_hooks.clients) {
				return AdminControlServer::Failure("Clients command is unavailable.", &a_request);
			}
			return AdminControlServer::Success(_hooks.clients(), {}, &a_request);
		}
		if (command == "bans") {
			if (!_hooks.bans) {
				return AdminControlServer::Failure("Bans command is unavailable.", &a_request);
			}
			return AdminControlServer::Success(_hooks.bans(), {}, &a_request);
		}
		if (command == "kick") {
			const auto playerId = ReadUInt32(a_request, "playerId", "player_id");
			if (!playerId) {
				return AdminControlServer::Failure("kick requires playerId", &a_request);
			}
			if (!_hooks.kick) {
				return AdminControlServer::Failure("Kick command is unavailable.", &a_request);
			}
			return Action(_hooks.kick(*playerId, ReadString(a_request, "reason").value_or(std::string{})), a_request);
		}
		if (command == "revive") {
			const auto playerId = ReadUInt32(a_request, "playerId", "player_id");
			if (!playerId) {
				return AdminControlServer::Failure("revive requires playerId", &a_request);
			}
			if (!_hooks.revive) {
				return AdminControlServer::Failure("Revive command is unavailable.", &a_request);
			}
			return Action(_hooks.revive(*playerId), a_request);
		}
		if (command == "kill") {
			const auto playerId = ReadUInt32(a_request, "playerId", "player_id");
			if (!playerId) {
				return AdminControlServer::Failure("kill requires playerId", &a_request);
			}
			if (!_hooks.killPlayer) {
				return AdminControlServer::Failure("Kill command is unavailable.", &a_request);
			}
			return Action(_hooks.killPlayer(*playerId), a_request);
		}
		if (command == "voice_mute" || command == "voice_unmute") {
			const bool muted = command == "voice_mute";
			const auto playerId = ReadUInt32(a_request, "playerId", "player_id");
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (!playerId && !profileId) {
				return AdminControlServer::Failure(command + " requires playerId or a valid profileId", &a_request);
			}
			std::optional<double> seconds;
			if (muted) {
				if (const auto* node = Coalesce(a_request, "seconds")) {
					if (!node->is_number() || !std::isfinite(node->get<double>()) || node->get<double>() <= 0.0) {
						return AdminControlServer::Failure("voice_mute seconds must be a positive number", &a_request);
					}
					seconds = node->get<double>();
				}
			}
			if (!_hooks.voiceMute) {
				return AdminControlServer::Failure(
					muted ? "Voice mute command is unavailable." : "Voice unmute command is unavailable.", &a_request);
			}
			return Action(_hooks.voiceMute(playerId, profileId.value_or(std::string{}), muted, seconds), a_request);
		}
		if (command == "ban") {
			const auto reason = ReadString(a_request, "reason").value_or(std::string{});
			if (const auto playerId = ReadUInt32(a_request, "playerId", "player_id")) {
				if (!_hooks.banPlayer) {
					return AdminControlServer::Failure("Ban-by-player command is unavailable.", &a_request);
				}
				return Action(_hooks.banPlayer(*playerId, reason), a_request);
			}
			const auto ip = ReadString(a_request, "ip");
			if (!ip || Trim(*ip).empty()) {
				return AdminControlServer::Failure("ban requires playerId or ip", &a_request);
			}
			if (!_hooks.banIp) {
				return AdminControlServer::Failure("Ban-by-IP command is unavailable.", &a_request);
			}
			return Action(_hooks.banIp(*ip, reason), a_request);
		}
		if (command == "unban") {
			const auto ip = ReadString(a_request, "ip");
			if (!ip || Trim(*ip).empty()) {
				return AdminControlServer::Failure("unban requires ip", &a_request);
			}
			if (!_hooks.unban) {
				return AdminControlServer::Failure("Unban command is unavailable.", &a_request);
			}
			return Action(_hooks.unban(*ip), a_request);
		}
		if (command == "identity_reset" || command == "resume_reset") {
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (!profileId) {
				return AdminControlServer::Failure("identity_reset requires a valid profileId", &a_request);
			}
			if (!_hooks.resetIdentity) {
				return AdminControlServer::Failure("Protocol V2 identity enforcement is not enabled.", &a_request);
			}
			bool ban = false;
			if (const auto* banNode = Coalesce(a_request, "ban")) {
				if (!banNode->is_boolean()) return AdminControlServer::Failure("identity_reset ban must be boolean", &a_request);
				ban = banNode->get<bool>();
			}
			if (!ban) return Action(_hooks.resetIdentity(*profileId), a_request);
			if (!_hooks.banProfile) return AdminControlServer::Failure("Profile banning is not available.", &a_request);
			const auto banResult = _hooks.banProfile(*profileId, ReadString(a_request, "reason").value_or(std::string{}));
			if (!banResult.ok) return Action(banResult, a_request);
			const auto reset = _hooks.resetIdentity(*profileId);
			const auto sessionsEnded = banResult.data.is_object() ? banResult.data.value("sessionsEnded", 0) : 0;
			if (!reset.ok) {
				return AdminControlServer::Success(
					nlohmann::json{
						{ "profileId", *profileId },
						{ "banned", true },
						{ "credentialReset", false },
						{ "credentialResetReason", reset.message },
						{ "sessionsEnded", sessionsEnded }
					},
					banResult.message + " Resume credential was not reset: " + reset.message, &a_request);
			}
			return AdminControlServer::Success(
				nlohmann::json{ { "profileId", *profileId }, { "banned", true }, { "credentialReset", true }, { "sessionsEnded", sessionsEnded } },
				reset.message + (reset.message.empty() ? "" : " ") + banResult.message, &a_request);
		}
		if (command == "profile_unban") {
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (!profileId) return AdminControlServer::Failure("profile_unban requires a valid profileId", &a_request);
			if (!_hooks.unbanProfile) return AdminControlServer::Failure("Profile unban command is unavailable.", &a_request);
			return Action(_hooks.unbanProfile(*profileId), a_request);
		}
		if (command == "world_time") {
			const auto value = ReadString(a_request, "hhmm", "time");
			if (!value || Trim(*value).empty()) {
				return AdminControlServer::Failure("world_time requires hhmm", &a_request);
			}
			if (!_hooks.setWorldTime) {
				return AdminControlServer::Failure("World time command is unavailable.", &a_request);
			}
			return Action(_hooks.setWorldTime(*value), a_request);
		}
		if (command == "world_weather") {
			const auto value = ReadString(a_request, "weather", "fw");
			if (!value || Trim(*value).empty()) {
				return AdminControlServer::Failure("world_weather requires weather", &a_request);
			}
			if (!_hooks.setWorldWeather) {
				return AdminControlServer::Failure("World weather command is unavailable.", &a_request);
			}
			return Action(_hooks.setWorldWeather(*value), a_request);
		}

		if (command == "world_mode" || command == "world_policy") {
			const auto mode = ReadString(a_request, "mode");
			if (!mode || Trim(*mode).empty()) {
				if (!_hooks.worldMode) {
					return AdminControlServer::Failure("World mode command is unavailable.", &a_request);
				}
				return AdminControlServer::Success(_hooks.worldMode(), {}, &a_request);
			}
			if (!_hooks.setWorldMode) {
				return AdminControlServer::Failure("World mode command is unavailable.", &a_request);
			}
			return Action(_hooks.setWorldMode(Trim(*mode)), a_request);
		}
		if (command == "world_policy_reload") {
			if (!_hooks.reloadWorldPolicy) {
				return AdminControlServer::Failure("World policy reload is unavailable.", &a_request);
			}
			return Action(_hooks.reloadWorldPolicy(), a_request);
		}
		if (command == "factions") {
			if (!_hooks.factions) {
				return AdminControlServer::Failure("Factions command is unavailable.", &a_request);
			}
			return AdminControlServer::Success(_hooks.factions(), {}, &a_request);
		}
		if (command == "faction_disband") {
			const auto factionId = ReadString(a_request, "factionId", "faction_id");
			if (!factionId || Trim(*factionId).empty()) {
				return AdminControlServer::Failure("faction_disband requires factionId", &a_request);
			}
			if (!_hooks.disbandFaction) {
				return AdminControlServer::Failure("Faction disband command is unavailable.", &a_request);
			}
			return Action(_hooks.disbandFaction(Trim(*factionId)), a_request);
		}
		if (command == "faction_promote") {
			const auto factionId = ReadString(a_request, "factionId", "faction_id");
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (!factionId || Trim(*factionId).empty() || !profileId) {
				return AdminControlServer::Failure("faction_promote requires factionId and a valid profileId", &a_request);
			}
			if (!_hooks.promoteFaction) {
				return AdminControlServer::Failure("Faction promote command is unavailable.", &a_request);
			}
			return Action(_hooks.promoteFaction(Trim(*factionId), *profileId), a_request);
		}
		if (command == "faction_kick") {
			const auto factionId = ReadString(a_request, "factionId", "faction_id");
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (!factionId || Trim(*factionId).empty() || !profileId) {
				return AdminControlServer::Failure("faction_kick requires factionId and a valid profileId", &a_request);
			}
			if (!_hooks.kickFaction) {
				return AdminControlServer::Failure("Faction kick command is unavailable.", &a_request);
			}
			return Action(_hooks.kickFaction(Trim(*factionId), *profileId), a_request);
		}
		if (command == "steam_lookup") {
			const auto steamId = ReadSteamId(a_request);
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			if (steamId.has_value() == profileId.has_value()) {
				return AdminControlServer::Failure("steam_lookup requires either a steamId string (17 digits) or a valid profileId", &a_request);
			}
			if (!_hooks.steamLookup) return AdminControlServer::Failure("Steam lookup is unavailable.", &a_request);
			return Action(_hooks.steamLookup(steamId.value_or(std::string{}), profileId.value_or(std::string{})), a_request);
		}
		if (command == "steam_ban") {
			const auto reason = ReadString(a_request, "reason").value_or(std::string{});
			if (const auto playerId = ReadUInt32(a_request, "playerId", "player_id")) {
				if (!_hooks.banPlayerSteamId) return AdminControlServer::Failure("Steam ban by player is unavailable.", &a_request);
				return Action(_hooks.banPlayerSteamId(*playerId, reason), a_request);
			}
			const auto steamId = ReadSteamId(a_request);
			if (!steamId) return AdminControlServer::Failure("steam_ban requires playerId or a steamId string (17 digits)", &a_request);
			if (!_hooks.banSteamId) return AdminControlServer::Failure("Steam ban is unavailable.", &a_request);
			return Action(_hooks.banSteamId(*steamId, reason), a_request);
		}
		if (command == "steam_unban") {
			const auto steamId = ReadSteamId(a_request);
			if (!steamId) return AdminControlServer::Failure("steam_unban requires a steamId string (17 digits)", &a_request);
			if (!_hooks.unbanSteamId) return AdminControlServer::Failure("Steam unban is unavailable.", &a_request);
			return Action(_hooks.unbanSteamId(*steamId), a_request);
		}
		if (command == "steam_flags") {
			if (!_hooks.steamFlags) return AdminControlServer::Failure("Steam identity flags are unavailable.", &a_request);
			return AdminControlServer::Success(_hooks.steamFlags(), {}, &a_request);
		}
		if (command == "steam_flag_clear") {
			const auto profileId = ReadProfileId(a_request, "profileId", "profile_id");
			const auto steamId = ReadSteamId(a_request);
			if (!profileId || !steamId) {
				return AdminControlServer::Failure("steam_flag_clear requires a valid profileId and a steamId string (17 digits)", &a_request);
			}
			if (!_hooks.clearSteamFlags) return AdminControlServer::Failure("Steam identity flags are unavailable.", &a_request);
			return Action(_hooks.clearSteamFlags(*profileId, *steamId), a_request);
		}
		if (command == "quest_mode") {
			if (!_hooks.questMode) {
				return AdminControlServer::Failure("Quest mode command is unavailable.", &a_request);
			}
			const auto mode = ReadString(a_request, "mode", "quest_mode");
			if (!mode) {
				if (Coalesce(a_request, "mode", "quest_mode") != nullptr) {
					return AdminControlServer::Failure("quest_mode mode must be a string", &a_request);
				}
				return Action(_hooks.questMode(std::string_view{}), a_request);
			}
			const auto normalized = Lower(Trim(*mode));
			if (!F4T::QuestProtocol::ParseMode(normalized)) {
				return AdminControlServer::Failure(
					"quest_mode mode must be coop-story, faction-war, instanced-story or free-roam",
					&a_request);
			}
			return Action(_hooks.questMode(normalized), a_request);
		}
		if (command == "quest_gate") {
			const auto quest = ReadQuestKey(a_request);
			if (!quest) {
				return AdminControlServer::Failure("quest_gate requires quest (LLLLLL:Plugin.esm)", &a_request);
			}
			const auto stage = ReadQuestStage(a_request);
			if (!stage) {
				return AdminControlServer::Failure("quest_gate requires stage (0-65535)", &a_request);
			}
			if (!_hooks.questGate) {
				return AdminControlServer::Failure("Quest gate command is unavailable.", &a_request);
			}
			bool release = false;
			if (const auto* node = Coalesce(a_request, "release"); node != nullptr) {
				if (!node->is_boolean()) {
					return AdminControlServer::Failure("quest_gate release must be boolean", &a_request);
				}
				release = node->get<bool>();
			}
			return Action(_hooks.questGate(*quest, *stage, release), a_request);
		}
		if (command == "quest_solo") {
			const auto player = ReadPlayerSelector(a_request);
			if (!player) {
				return AdminControlServer::Failure("quest_solo requires playerId or profileId", &a_request);
			}
			const auto quest = ReadQuestKey(a_request);
			if (!quest) {
				return AdminControlServer::Failure("quest_solo requires quest (LLLLLL:Plugin.esm)", &a_request);
			}
			if (!_hooks.questSolo) {
				return AdminControlServer::Failure("Quest solo command is unavailable.", &a_request);
			}
			bool solo = true;
			if (const auto* node = Coalesce(a_request, "solo"); node != nullptr) {
				if (!node->is_boolean()) {
					return AdminControlServer::Failure("quest_solo solo must be boolean", &a_request);
				}
				solo = node->get<bool>();
			}
			return Action(_hooks.questSolo(*player, *quest, solo), a_request);
		}

		return AdminControlServer::Failure(
			"Unknown admin command: " + (command.empty() ? std::string{ "(empty)" } : command),
			&a_request);
	}

	nlohmann::json AdminCommandDispatcher::Action(const AdminActionResult& a_result, const nlohmann::json& a_request) const
	{
		return a_result.ok
			? AdminControlServer::Success(a_result.data, a_result.message, &a_request)
			: AdminControlServer::Failure(a_result.message, &a_request);
	}
}
