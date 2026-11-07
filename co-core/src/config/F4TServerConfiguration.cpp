#include "F4TServerConfiguration.h"
#include "F4TServerConfigJson.h"
#include "F4TClientCompatibility.h"
#include "F4TConnectionPolicy.h"
#include "F4TDurableState.h"
#include "F4TContentManifest.h"
#include "F4TIdentityProtocol.h"
#include "F4TInventorySnapshot.h"
#include "F4TModpackContentPolicy.h"
#include "F4TModpackManifest.h"
#include "F4TModOrganizer.h"
#include "F4TUtf8Path.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorldPolicy.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace F4T::ServerConfiguration
{
	namespace
	{
		using namespace JsonRead;

		F4T::QuestProtocol::Mode ReadQuestMode(const Json* a_node)
		{
			if (a_node == nullptr) {
				return F4T::QuestProtocol::Mode::CoopStory;
			}
			if (!a_node->is_string()) {
				throw std::invalid_argument("quest_sync_mode must be a string");
			}
			const auto mode = F4T::QuestProtocol::ParseMode(Trim(a_node->get_ref<const std::string&>()));
			if (!mode) {
				throw std::invalid_argument("quest_sync_mode must be coop-story, faction-war, instanced-story or free-roam");
			}
			return *mode;
		}

		std::vector<F4T::QuestProtocol::OverrideEntry> ReadQuestOverrides(const Json* a_node)
		{
			if (a_node == nullptr) {
				return {};
			}
			if (!a_node->is_array()) {
				throw std::invalid_argument("quest_sync_overrides must be an array");
			}
			if (a_node->size() > F4T::QuestProtocol::kMaximumOverrideEntries) {
				throw std::invalid_argument("quest_sync_overrides must hold at most 256 entries");
			}
			std::vector<F4T::QuestProtocol::OverrideEntry> entries;
			entries.reserve(a_node->size());
			for (const auto& item : *a_node) {
				if (!item.is_object()) {
					throw std::invalid_argument("quest_sync_overrides entries must be objects");
				}
				const auto quest = item.find("quest");
				const auto policy = item.find("policy");
				if (quest == item.end() || !quest->is_string() || policy == item.end() || !policy->is_string()) {
					throw std::invalid_argument("quest_sync_overrides entries need a quest and a policy string");
				}
				const auto key = F4T::QuestProtocol::ParseFormKey(Trim(quest->get_ref<const std::string&>()));
				if (!key) {
					throw std::invalid_argument("quest_sync_overrides quest must be LLLLLL:Plugin.esm with a 6-digit hex local id");
				}
				const auto parsed = F4T::QuestProtocol::ParseOverride(Trim(policy->get_ref<const std::string&>()));
				if (!parsed) {
					throw std::invalid_argument("quest_sync_overrides policy must be shared, solo or blocked");
				}
				entries.push_back(F4T::QuestProtocol::OverrideEntry{ *key, *parsed });
			}
			return entries;
		}

		std::vector<std::string> ReadAdminProfiles(const Json* a_node)
		{
			return ReadStringArray(a_node, "admin_profiles");
		}

		bool IsPrintableBounded(std::string_view a_value, std::size_t a_minimum, std::size_t a_maximum)
		{
			if (a_value.size() < a_minimum || a_value.size() > a_maximum) {
				return false;
			}
			for (const unsigned char ch : a_value) {
				if (ch < 0x20 || ch == 0x7f) {
					return false;
				}
			}
			return true;
		}

		bool IsPrintableListedText(std::string_view a_value, std::size_t a_maximum)
		{
			if (a_value.size() > a_maximum) {
				return false;
			}
			for (std::size_t index = 0; index < a_value.size(); ++index) {
				const auto ch = static_cast<unsigned char>(a_value[index]);
				if (ch < 0x20 || ch == 0x7f) {
					return false;
				}
				if (ch == 0xc2 && index + 1 < a_value.size()) {
					const auto next = static_cast<unsigned char>(a_value[index + 1]);
					if (next >= 0x80 && next <= 0x9f) {
						return false;
					}
				}
			}
			return true;
		}

		enum class ListedTextFault
		{
			BlankName,
			UnprintableName,
			UnprintableDescription
		};

		std::string ListedTextSaveMessage(ListedTextFault a_fault)
		{
			switch (a_fault) {
			case ListedTextFault::BlankName:
				return "server_name is blank while the file's public_listing is on; type a printable server name and save again.";
			case ListedTextFault::UnprintableName:
				return "server_name must be 1-64 printable UTF-8 bytes while the file's public_listing is on; remove the control characters from the name and save again.";
			case ListedTextFault::UnprintableDescription:
				return "server_description must be at most 256 printable UTF-8 bytes while the file's public_listing is on; remove the control characters from the description and save again.";
			}
			return {};
		}

		void CollectListedTextSaveErrors(const Options& a_options, std::vector<std::string>& a_errors)
		{
			const auto listedName = TrimSpaces(a_options.serverName);
			if (listedName.empty()) {
				a_errors.push_back(ListedTextSaveMessage(ListedTextFault::BlankName));
			} else if (!IsPrintableListedText(listedName, 64)) {
				a_errors.push_back(ListedTextSaveMessage(ListedTextFault::UnprintableName));
			}
			if (!IsPrintableListedText(TrimSpaces(a_options.serverDescription), 256)) {
				a_errors.push_back(ListedTextSaveMessage(ListedTextFault::UnprintableDescription));
			}
		}

		bool IsHex(std::string_view a_value)
		{
			return std::all_of(a_value.begin(), a_value.end(), [](char ch) {
				return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
			});
		}

		bool IsPrintableAscii(std::string_view a_value)
		{
			return std::all_of(a_value.begin(), a_value.end(), [](char ch) {
				return ch >= 0x20 && ch <= 0x7e;
			});
		}

		bool IsExplicitIpv4(std::string_view a_value)
		{
			const auto value = Trim(a_value);
			std::size_t begin = 0;
			for (int part = 0; part < 4; ++part) {
				const auto end = part == 3 ? value.size() : value.find('.', begin);
				if (end == std::string::npos || end == begin) {
					return false;
				}
				unsigned int octet = 0;
				const auto result = std::from_chars(value.data() + begin, value.data() + end, octet);
				if (result.ec != std::errc{} || result.ptr != value.data() + end || octet > 255) {
					return false;
				}
				begin = end + 1;
			}
			return begin == value.size() + 1;
		}

		struct RegionName
		{
			std::string_view key;
			std::string_view directoryName;
		};

		constexpr RegionName kRegionNames[]{
			{ "north-america", "North America" },
			{ "europe", "Europe" },
			{ "asia", "Asia" },
			{ "oceania", "Oceania" }
		};

		const RegionName* FindRegion(std::string_view a_key)
		{
			for (const auto& region : kRegionNames) {
				if (region.key == a_key) {
					return &region;
				}
			}
			return nullptr;
		}
	}

	Options Parse(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory)
	{
		if (!a_root.is_object()) {
			throw std::invalid_argument("Server configuration root must be a JSON object");
		}

		Options options;
		if (const auto* node = Find(a_root, "host")) {
			options.bindHost = ReadRequiredString(node, "host");
		}
		options.port = ReadPort(Find(a_root, "port"), options.port, "port");
		options.adminPort = ReadPort(Find(a_root, "admin_port", "adminPort"), options.adminPort, "admin_port");
		if (const auto* node = Find(a_root, "server_name", "serverName")) {
			options.serverName = ReadRequiredString(node, "server_name");
		}
		if (const auto* node = Find(a_root, "server_description", "serverDescription")) {
			options.serverDescription = ReadRequiredString(node, "server_description");
		}
		options.appealLink = ReadOptionalString(Find(a_root, "appeal_link", "appealLink"), "appeal_link").value_or(std::string{});
		if (const auto* node = Find(a_root, "region"); node != nullptr && !node->is_null()) {
			const auto text = Trim(ReadRequiredString(node, "region"));
			if (FindRegion(text) == nullptr) {
				throw std::invalid_argument("region must be north-america, europe, asia or oceania");
			}
			options.region = text;
		}
		const auto maximumPlayers = ReadInteger(Find(a_root, "max_players", "maxPlayers"), static_cast<std::int64_t>(options.maximumPlayers), "max_players");
		options.maximumPlayers = maximumPlayers < 0 ? 0 : static_cast<std::size_t>(maximumPlayers);
		const auto maximumMessageBytes = ReadInteger(
			Find(a_root, "max_message_bytes", "maxMessageBytes"),
			static_cast<std::int64_t>(options.maximumMessageBytes),
			"max_message_bytes");
		options.maximumMessageBytes = maximumMessageBytes < 0 ? 0 : static_cast<std::size_t>(maximumMessageBytes);
		const auto inboundPackets = ReadInteger(Find(a_root, "inbound_packets_per_second", "inboundPacketsPerSecond"), static_cast<std::int64_t>(options.inboundBudget.maximumPacketsPerWindow), "inbound_packets_per_second");
		options.inboundBudget.maximumPacketsPerWindow = inboundPackets < 0 ? 0U : static_cast<std::uint32_t>(std::min<std::int64_t>(inboundPackets, std::numeric_limits<std::uint32_t>::max()));
		const auto inboundBytes = ReadInteger(Find(a_root, "inbound_bytes_per_second", "inboundBytesPerSecond"), static_cast<std::int64_t>(options.inboundBudget.maximumBytesPerWindow), "inbound_bytes_per_second");
		options.inboundBudget.maximumBytesPerWindow = inboundBytes < 0 ? 0U : static_cast<std::uint64_t>(inboundBytes);
		const auto inboundWindows = ReadInteger(Find(a_root, "inbound_violation_windows", "inboundViolationWindows"), static_cast<std::int64_t>(options.inboundBudget.maximumViolationWindows), "inbound_violation_windows");
		options.inboundBudget.maximumViolationWindows = inboundWindows < 0 ? 0U : static_cast<std::uint32_t>(std::min<std::int64_t>(inboundWindows, std::numeric_limits<std::uint32_t>::max()));
		const auto characterSlots = ReadInteger(Find(a_root, "character_slots", "characterSlots"), static_cast<std::int64_t>(options.characterSlots), "character_slots");
		options.characterSlots = characterSlots < 0 ? 0 : static_cast<std::size_t>(characterSlots);
		options.requireV2Identity = ReadBoolean(Find(a_root, "require_v2_identity", "requireV2Identity"), true, "require_v2_identity");
		options.requireKnownProfile = ReadBoolean(Find(a_root, "require_known_profile", "requireKnownProfile"), false, "require_known_profile");
		options.requireSteamIdentity = ReadBoolean(Find(a_root, "require_steam_identity", "requireSteamIdentity"), false, "require_steam_identity");
		options.questMode = ReadQuestMode(Find(a_root, "quest_sync_mode", "questSyncMode"));
		options.questOverrides = ReadQuestOverrides(Find(a_root, "quest_sync_overrides", "questSyncOverrides"));
		options.enablePersistence = ReadBoolean(Find(a_root, "enable_persistence", "enablePersistence"), true, "enable_persistence");
		options.enableChat = ReadBoolean(Find(a_root, "enable_chat", "enableChat"), true, "enable_chat");
		options.profanityFilter = ReadBoolean(Find(a_root, "profanity_filter", "profanityFilter"), false, "profanity_filter");
		options.steamFriendsOnly = ReadBoolean(Find(a_root, "steam_friends_only", "steamFriendsOnly"), true, "steam_friends_only");
		options.enableFactions = ReadBoolean(Find(a_root, "enable_factions", "enableFactions"), true, "enable_factions");
		options.enableObjectState = ReadBoolean(Find(a_root, "enable_object_state", "enableObjectState"), true, "enable_object_state");
		options.allowDeathBagLootByOthers = ReadBoolean(Find(a_root, "allow_death_bag_loot_by_others", "allowDeathBagLootByOthers"), false, "allow_death_bag_loot_by_others");
		options.enableQuestSync = ReadBoolean(Find(a_root, "enable_quest_sync", "enableQuestSync"), true, "enable_quest_sync");
		const auto* downed = Section(a_root, "downed");
		const auto downedSeconds = ReadInteger(
			Pick(Member(downed, "seconds"), Find(a_root, "downed_seconds", "downedSeconds")),
			static_cast<std::int64_t>(options.downedSeconds),
			"downed.seconds");
		options.downedSeconds = static_cast<std::uint32_t>(std::clamp<std::int64_t>(downedSeconds, 0, (std::numeric_limits<std::uint32_t>::max)()));
		const auto reviveChannelSeconds = ReadInteger(
			Pick(Member(downed, "revive_channel_seconds"), Find(a_root, "revive_channel_seconds", "reviveChannelSeconds")),
			static_cast<std::int64_t>(options.reviveChannelSeconds),
			"downed.revive_channel_seconds");
		options.reviveChannelSeconds = static_cast<std::uint32_t>(std::clamp<std::int64_t>(reviveChannelSeconds, 0, (std::numeric_limits<std::uint32_t>::max)()));
		options.reviveHealthFraction = ReadNumber(Member(downed, "revive_health_fraction"), options.reviveHealthFraction, "downed.revive_health_fraction");
		options.reviveRangeMeters = ReadNumber(Member(downed, "revive_range_meters"), options.reviveRangeMeters, "downed.revive_range_meters");
		options.respawnGraceSeconds = ReadNumber(Member(downed, "respawn_grace_seconds"), options.respawnGraceSeconds, "downed.respawn_grace_seconds");
		const auto stimpaks = ReadInteger(Member(downed, "stimpaks_per_revive"), static_cast<std::int64_t>(options.stimpaksPerRevive), "downed.stimpaks_per_revive");
		options.stimpaksPerRevive = stimpaks < 0 ? 0U : static_cast<std::uint32_t>(std::min<std::int64_t>(stimpaks, (std::numeric_limits<std::uint32_t>::max)()));
		const auto* factions = Section(a_root, "factions");
		const auto count = [factions](std::string_view a_key, std::size_t a_default, std::string_view a_field) {
			const auto value = ReadInteger(Member(factions, a_key), static_cast<std::int64_t>(a_default), a_field);
			return value < 0 ? std::size_t{ 0 } : static_cast<std::size_t>(value);
		};
		options.factions.maximumFactions = count("max_factions", options.factions.maximumFactions, "factions.max_factions");
		options.factions.maximumMembers = count("max_members", options.factions.maximumMembers, "factions.max_members");
		options.factions.inviteLifetimeSeconds = ReadNumber(Member(factions, "invite_lifetime_seconds"), options.factions.inviteLifetimeSeconds, "factions.invite_lifetime_seconds");
		options.factions.inviteWindowSeconds = ReadNumber(Member(factions, "invite_window_seconds"), options.factions.inviteWindowSeconds, "factions.invite_window_seconds");
		options.factions.invitesPerWindow = count("invites_per_window", options.factions.invitesPerWindow, "factions.invites_per_window");
		options.factions.pendingInvites = count("pending_invites", options.factions.pendingInvites, "factions.pending_invites");
		options.factions.pendingInvitesPerTarget = count("pending_invites_per_target", options.factions.pendingInvitesPerTarget, "factions.pending_invites_per_target");
		options.factions.rosterRequestIntervalSeconds = ReadNumber(Member(factions, "roster_request_interval_seconds"), options.factions.rosterRequestIntervalSeconds, "factions.roster_request_interval_seconds");
		options.factions.listRequestIntervalSeconds = ReadNumber(Member(factions, "list_request_interval_seconds"), options.factions.listRequestIntervalSeconds, "factions.list_request_interval_seconds");
		options.factions.nameRosterIntervalSeconds = ReadNumber(Member(factions, "name_roster_interval_seconds"), options.factions.nameRosterIntervalSeconds, "factions.name_roster_interval_seconds");
		options.factions.mutationCapacity = ReadNumber(Member(factions, "mutation_capacity"), options.factions.mutationCapacity, "factions.mutation_capacity");
		options.factions.mutationRefillPerSecond = ReadNumber(Member(factions, "mutation_refill_per_second"), options.factions.mutationRefillPerSecond, "factions.mutation_refill_per_second");
		options.questStoryOrderPath = ReadOptionalString(Find(a_root, "questStoryOrderPath", "quest_story_order_path"), "questStoryOrderPath");
		options.survivalMode = ReadBoolean(Find(a_root, "survival_mode", "survivalMode"), options.survivalMode, "survival_mode");
		options.survivalNeeds = ReadBoolean(Find(a_root, "survival_needs", "survivalNeeds"), options.survivalNeeds, "survival_needs");
		options.requireDifficultyReport = ReadBoolean(Find(a_root, "require_difficulty_report", "requireDifficultyReport"), options.requireDifficultyReport, "require_difficulty_report");
		options.friendlyFire = ReadBoolean(Find(a_root, "friendly_fire", "friendlyFire"), true, "friendly_fire");
		options.factionFriendlyFire = ReadBoolean(Find(a_root, "faction_friendly_fire", "factionFriendlyFire"), false, "faction_friendly_fire");
		{
			const auto* raid = Find(a_root, "workshop_raid", "workshopRaid");
			const auto* modeNode = Pick(Member(raid, "mode"), Find(a_root, "raid_mode", "raidMode"));
			if (modeNode != nullptr) {
				const auto text = Trim(ReadRequiredString(modeNode, "raid_mode"));
				const auto parsed = F4T::WorkshopRaids::ParseRaidMode(text);
				if (!parsed) {
					throw std::invalid_argument("raid_mode must be open, windowed or off");
				}
				options.workshopRaid.mode = *parsed;
			}
			options.workshopRaid.windowStartHour = static_cast<float>(ReadNumber(
				Pick(Member(raid, "window_start_hour"), Find(a_root, "raid_window_start_hour", "raidWindowStartHour")),
				options.workshopRaid.windowStartHour,
				"workshop_raid.window_start_hour"));
			options.workshopRaid.windowDurationHours = static_cast<float>(ReadNumber(
				Pick(Member(raid, "window_duration_hours"), Find(a_root, "raid_window_duration_hours", "raidWindowDurationHours")),
				options.workshopRaid.windowDurationHours,
				"workshop_raid.window_duration_hours"));
		}
		if (const auto* node = Find(a_root, "starting_inventory", "startingInventory")) {
			(void)F4T::DurableState::NormalizeStartingInventory(*node);
			options.startingInventory = *node;
		}
		if (const auto* node = Find(a_root, "inventory_gain_mode", "inventoryGainMode")) {
			const auto text = Trim(ReadRequiredString(node, "inventory_gain_mode"));
			if (!F4T::InventorySnapshot::ParseGainMode(text)) {
				throw std::invalid_argument("inventory_gain_mode must be off, warn or enforce");
			}
			options.inventoryGainMode = text;
		}
		if (const auto* worldMode = Find(a_root, "worldMode", "world_mode")) {
			options.worldMode = ReadRequiredString(worldMode, "worldMode");
		} else if (const auto* questMode = Find(a_root, "questMode", "quest_mode")) {
			options.worldMode = ReadRequiredString(questMode, "questMode");
		}
		options.doorsShared = ReadBoolean(Find(a_root, "doorsShared", "doors_shared"), true, "doorsShared");
		if (const auto* vats = Find(a_root, "vats", "vats_mode")) {
			const auto text = ReadRequiredString(vats, "vats");
			if (!F4T::WorldPolicy::ParseVats(Trim(text))) {
				throw std::invalid_argument("vats must be disabled, real-time or vanilla");
			}
			options.vats = text;
		}
		if (const auto* node = Find(a_root, "worldPolicyPath", "world_policy_path")) {
			options.worldPolicyPath = ReadRequiredString(node, "worldPolicyPath");
		}
		if (const auto* node = Find(a_root, "durable_state_path", "durableStatePath")) {
			options.durableStatePath = ReadRequiredString(node, "durable_state_path");
		}
		options.lanDiscoveryPort = ReadPort(
			Find(a_root, "lan_discovery_port", "lanDiscoveryPort"),
			options.lanDiscoveryPort,
			"lan_discovery_port");
		options.enableLanDiscovery = ReadBoolean(
			Find(a_root, "lan_discovery_enabled", "lanDiscoveryEnabled"),
			true,
			"lan_discovery_enabled");
		options.publicListing = ReadBoolean(Find(a_root, "public_listing", "publicListing"), true, "public_listing");
		if (const auto* node = Find(a_root, "directory_url", "directoryUrl"); node != nullptr && !node->is_null()) {
			options.directoryUrl = ReadRequiredString(node, "directory_url");
		}
		if (a_root.contains("required_client_version") || a_root.contains("requiredClientVersion")) {
			options.requiredClientVersion = ReadOptionalString(Find(a_root, "required_client_version", "requiredClientVersion"), "required_client_version");
		} else {
			options.requiredClientVersion = std::string{ F4T::ClientCompatibility::kClientVersion };
		}
		options.requiredFalloutRuntimeVersion = ReadOptionalString(Find(a_root, "required_fallout_runtime_version", "requiredFalloutRuntimeVersion"), "required_fallout_runtime_version");
		options.requiredContentManifestFingerprint = ReadOptionalString(
			Find(a_root, "required_content_manifest_fingerprint", "requiredContentManifestFingerprint"),
			"required_content_manifest_fingerprint");
		options.requiredCapabilities = ReadStringArray(Find(a_root, "required_capabilities", "requiredCapabilities"), "required_capabilities");
		options.adminProfiles = ReadAdminProfiles(Find(a_root, "admin_profiles", "adminProfiles"));
		if (const auto* node = Find(a_root, "server_password", "serverPassword")) {
			options.serverPassword = ReadRequiredString(node, "server_password");
		}
		options.requiredModpackUrl = ReadOptionalString(
			Find(a_root, "required_modpack_url", "requiredModpackUrl"),
			"required_modpack_url");
		options.modpackName = ReadOptionalString(Find(a_root, "modpack_name", "modpackName"), "modpack_name");
		options.allowModpackDownloads = ReadBoolean(Find(a_root, "allow_modpack_downloads", "allowModpackDownloads"), false, "allow_modpack_downloads");
		if (auto directory = ReadOptionalString(Find(a_root, "modpack_download_dir", "modpackDownloadDir"), "modpack_download_dir")) {
			auto path = std::filesystem::path{ *directory };
			if (path.is_relative() && !a_baseDirectory.empty()) {
				path = a_baseDirectory / path;
			}
			options.modpackDownloadDirectory = path.string();
		}
		options.modpackDownloadable = ReadStringArray(Find(a_root, "modpack_downloadable", "modpackDownloadable"), "modpack_downloadable");
		const auto downloadConcurrency = ReadInteger(
			Find(a_root, "modpack_download_concurrency", "modpackDownloadConcurrency"),
			static_cast<std::int64_t>(options.modpackDownloadConcurrency),
			"modpack_download_concurrency");
		options.modpackDownloadConcurrency = downloadConcurrency >= 1 && downloadConcurrency <= static_cast<std::int64_t>(kMaximumModpackDownloadConcurrency)
			? static_cast<std::size_t>(downloadConcurrency)
			: 0;
		const auto downloadQuota = ReadInteger(
			Find(a_root, "modpack_download_bytes_per_hour_per_address", "modpackDownloadBytesPerHourPerAddress"),
			static_cast<std::int64_t>(options.modpackDownloadBytesPerHourPerAddress),
			"modpack_download_bytes_per_hour_per_address");
		options.modpackDownloadBytesPerHourPerAddress = downloadQuota >= 1 && static_cast<std::uint64_t>(downloadQuota) <= kDefaultModpackDownloadBytesPerHourPerAddress
			? static_cast<std::uint64_t>(downloadQuota)
			: 0;
		const auto resumeHours = ReadInteger(
			Find(a_root, "resume_token_lifetime_hours", "resumeTokenLifetimeHours"),
			options.resumeTokenLifetimeHours,
			"resume_token_lifetime_hours");
		options.resumeTokenLifetimeHours = resumeHours >= 1 &&
			resumeHours <= static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())
			? static_cast<std::uint32_t>(resumeHours)
			: 0;
		if (const auto* found = Find(a_root, "resume_token_lifetime_seconds", "resumeTokenLifetimeSeconds");
			found != nullptr && !found->is_null()) {
			const auto resumeSeconds = ReadInteger(found, 0, "resume_token_lifetime_seconds");
			options.resumeTokenLifetimeSeconds = resumeSeconds >= 1 &&
				resumeSeconds <= static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())
				? static_cast<std::uint32_t>(resumeSeconds)
				: 0;
		}
		return options;
	}

	std::string RegionDirectoryName(const Options& a_options)
	{
		const auto* region = FindRegion(a_options.region);
		return region != nullptr ? std::string{ region->directoryName } : std::string{};
	}

	F4T::DownedState::Config DownedConfig(const Options& a_options) noexcept
	{
		return F4T::DownedState::Config{
			.downedSeconds = a_options.downedSeconds,
			.reviveHealthFraction = a_options.reviveHealthFraction,
			.reviveRangeMeters = a_options.reviveRangeMeters,
			.reviveChannelSeconds = static_cast<double>(a_options.reviveChannelSeconds),
			.respawnGraceSeconds = a_options.respawnGraceSeconds,
			.stimpaksPerRevive = a_options.stimpaksPerRevive
		};
	}

	std::vector<std::string> Validate(const Options& a_options, std::optional<bool> a_modpackSourceConfigured)
	{
		std::vector<std::string> errors;
		if (!IsExplicitIpv4(a_options.bindHost) && !F4T::ConnectionPolicy::Ipv6Prefix64(Trim(a_options.bindHost))) {
			errors.push_back("host must be an explicit IPv4 or IPv6 bind address");
		}
		if (a_options.inboundBudget.maximumPacketsPerWindow < 10 || a_options.inboundBudget.maximumPacketsPerWindow > F4T::PacketRateLimit::MaximumInboundWirePacketsPerSecond) {
			errors.push_back("inbound_packets_per_second must be 10-512");
		}
		if (a_options.inboundBudget.maximumBytesPerWindow < 64ULL * 1024ULL || a_options.inboundBudget.maximumBytesPerWindow > 64ULL * 1024ULL * 1024ULL) {
			errors.push_back("inbound_bytes_per_second must be 65536-67108864");
		}
		if (a_options.inboundBudget.maximumViolationWindows < 1 || a_options.inboundBudget.maximumViolationWindows > 60) {
			errors.push_back("inbound_violation_windows must be 1-60");
		}
		if (a_options.port < 1) {
			errors.push_back("port must be 1-65535");
		}
		if (a_options.adminPort < 1) {
			errors.push_back("admin_port must be 1-65535");
		} else if (a_options.adminPort == a_options.port) {
			errors.push_back("admin_port must differ from the game port");
		}
		if (a_options.serverName.empty()) {
			errors.push_back("server_name cannot be empty");
		} else if (a_options.serverName.size() > 64) {
			errors.push_back("server_name must be <= 64 characters");
		}
		if (a_options.serverDescription.size() > 256) {
			errors.push_back("server_description must be <= 256 characters");
		}
		if (a_options.publicListing) {
			const auto listedName = TrimSpaces(a_options.serverName);
			const auto listedDescription = TrimSpaces(a_options.serverDescription);
			if (listedName.empty()) {
				errors.push_back(
					"server_name cannot be blank while public_listing is on; give the server a printable name, or set \"public_listing\": false (or start with --no-public-listing)");
			} else if (!IsPrintableListedText(listedName, 64)) {
				errors.push_back(
					"server_name must be 1-64 printable UTF-8 bytes while public_listing is on; remove the control characters or tabs/newlines from the name, or set \"public_listing\": false (or start with --no-public-listing)");
			}
			if (!IsPrintableListedText(listedDescription, 256)) {
				errors.push_back(
					"server_description must be at most 256 printable UTF-8 bytes while public_listing is on; remove the control characters or tabs/newlines from the description, or set \"public_listing\": false (or start with --no-public-listing)");
			}
		}
		if (!IsAllowedAppealLink(a_options.appealLink)) {
			errors.push_back("appeal_link must be empty, or an http:// or https:// link of at most 200 visible characters with no '@'");
		}
		if (a_options.maximumPlayers < 1 || a_options.maximumPlayers > F4T::RemotePlayerCapacity::MaximumSessionPlayers) {
			errors.push_back(
				"max_players must be 1-" + std::to_string(F4T::RemotePlayerCapacity::MaximumSessionPlayers));
		}
		if (a_options.maximumMessageBytes < F4T::TransportLimits::MinimumMaximumMessageBytes ||
			a_options.maximumMessageBytes > F4T::TransportLimits::HardMaximumMessageBytes) {
			errors.push_back(
				"max_message_bytes must be " +
				std::to_string(F4T::TransportLimits::MinimumMaximumMessageBytes) + "-" +
				std::to_string(F4T::TransportLimits::HardMaximumMessageBytes));
		}
		if (!F4T::CharacterSlots::IsValidSlotCount(a_options.characterSlots)) {
			errors.push_back("character_slots must be 1-8");
		}
		if (!a_options.requireV2Identity) {
			errors.push_back("require_v2_identity can no longer be disabled; Protocol V2 identity is mandatory");
		}
		const auto range = [&errors](double a_value, double a_low, double a_high, const char* a_message) {
			if (!(a_value >= a_low && a_value <= a_high)) errors.emplace_back(a_message);
		};
		range(static_cast<double>(a_options.factions.maximumFactions), 1.0, static_cast<double>(kFactionCeilingFactions), "factions.max_factions must be 1-4096");
		range(static_cast<double>(a_options.factions.maximumMembers), 1.0, static_cast<double>(kFactionCeilingMembers), "factions.max_members must be 1-64");
		range(a_options.factions.inviteLifetimeSeconds, 1.0, 3600.0, "factions.invite_lifetime_seconds must be 1-3600");
		range(a_options.factions.inviteWindowSeconds, 1.0, 3600.0, "factions.invite_window_seconds must be 1-3600");
		range(static_cast<double>(a_options.factions.invitesPerWindow), 1.0, 1000.0, "factions.invites_per_window must be 1-1000");
		range(static_cast<double>(a_options.factions.pendingInvites), 1.0, 65536.0, "factions.pending_invites must be 1-65536");
		range(static_cast<double>(a_options.factions.pendingInvitesPerTarget), 1.0, 1024.0, "factions.pending_invites_per_target must be 1-1024");
		range(a_options.factions.rosterRequestIntervalSeconds, 0.0, 60.0, "factions.roster_request_interval_seconds must be 0-60");
		range(a_options.factions.listRequestIntervalSeconds, 0.0, 60.0, "factions.list_request_interval_seconds must be 0-60");
		range(a_options.factions.nameRosterIntervalSeconds, 0.0, 60.0, "factions.name_roster_interval_seconds must be 0-60");
		range(a_options.factions.mutationCapacity, 1.0, 1000.0, "factions.mutation_capacity must be 1-1000");
		if (!(a_options.factions.mutationRefillPerSecond > 0.0 && a_options.factions.mutationRefillPerSecond <= 1000.0)) errors.emplace_back("factions.mutation_refill_per_second must be above 0 and at most 1000");
		if (!(a_options.reviveHealthFraction > 0.0 && a_options.reviveHealthFraction <= 1.0)) errors.emplace_back("downed.revive_health_fraction must be above 0 and at most 1");
		if (!(a_options.reviveRangeMeters > 0.0 && a_options.reviveRangeMeters <= 50.0)) errors.emplace_back("downed.revive_range_meters must be above 0 and at most 50");
		range(a_options.respawnGraceSeconds, 0.0, 600.0, "downed.respawn_grace_seconds must be 0-600");
		range(static_cast<double>(a_options.stimpaksPerRevive), 0.0, static_cast<double>(F4T::DownedState::MaximumStimpaksReported), "downed.stimpaks_per_revive must be 0-99");
		if (!F4T::DownedState::IsValidDownedSeconds(a_options.downedSeconds)) {
			errors.push_back("downed_seconds must be 5-600");
		}
		if (!F4T::DownedState::IsValidReviveChannelSeconds(static_cast<double>(a_options.reviveChannelSeconds))) {
			errors.push_back("revive_channel_seconds must be 0-15");
		}
		if (a_options.startingInventory) {
			if (!a_options.enablePersistence) errors.push_back("starting_inventory requires durable persistence");
			try { (void)F4T::DurableState::NormalizeStartingInventory(*a_options.startingInventory); }
			catch (const std::invalid_argument& error) { errors.push_back(error.what()); }
		}
		if (a_options.enablePersistence && Trim(a_options.durableStatePath).empty()) {
			errors.push_back("durable_state_path cannot be empty when persistence is enabled");
		}
		std::unordered_set<std::string> overrideQuests;
		for (const auto& entry : a_options.questOverrides) {
			const auto key = F4T::QuestProtocol::FormatFormKey(entry.quest);
			if (!overrideQuests.insert(key).second) {
				errors.push_back("quest_sync_overrides must not repeat " + key);
			}
		}
		if (!F4T::WorldPolicy::ParseMode(a_options.worldMode)) {
			errors.push_back("worldMode '" + a_options.worldMode + "' must be coop-story or free-roam");
		}
		if (!F4T::WorldPolicy::ParseVats(a_options.vats)) {
			errors.push_back("vats must be disabled, real-time or vanilla");
		}
		if (Trim(a_options.worldPolicyPath).empty()) {
			errors.push_back("worldPolicyPath cannot be empty");
		}
		if (a_options.enableLanDiscovery) {
			if (a_options.lanDiscoveryPort == 0) {
				errors.push_back("lan_discovery_port must be 1-65535");
			} else if (a_options.lanDiscoveryPort == a_options.port) {
				errors.push_back("lan_discovery_port must differ from the game port");
			}
		}
		if (a_options.publicListing && !IsAllowedDirectoryUrl(a_options.directoryUrl)) {
			errors.push_back("directory_url must be an https:// URL (plain http is only allowed for 127.0.0.1 or localhost)");
		}
		if (a_options.resumeTokenLifetimeHours < 1 || a_options.resumeTokenLifetimeHours > 8760) {
			errors.push_back("resume_token_lifetime_hours must be 1-8760");
		}
		if (a_options.resumeTokenLifetimeSeconds &&
			(*a_options.resumeTokenLifetimeSeconds < 1 || *a_options.resumeTokenLifetimeSeconds > 8760u * 60u * 60u)) {
			errors.push_back("resume_token_lifetime_seconds must be 1-31536000");
		}

		auto validateOptional = [&](const std::optional<std::string>& value, std::string_view field) {
			if (value && !IsPrintableBounded(*value, 1, 64)) {
				errors.push_back(std::string{ field } + " must be 1-64 printable characters when set");
			}
		};
		validateOptional(a_options.requiredClientVersion, "required_client_version");
		validateOptional(a_options.requiredFalloutRuntimeVersion, "required_fallout_runtime_version");
		if (a_options.serverPassword) {
			if (a_options.serverPassword->empty()) {
				errors.push_back("server_password cannot be empty; omit it to disable the password");
			} else if (a_options.serverPassword->size() < 4 || a_options.serverPassword->size() > 64 || !IsPrintableAscii(*a_options.serverPassword)) {
				errors.push_back("server_password must be 4-64 printable ASCII characters when set");
			}
		}

		if (a_options.requiredContentManifestFingerprint &&
			(a_options.requiredContentManifestFingerprint->size() != 64 || !IsHex(*a_options.requiredContentManifestFingerprint))) {
			errors.push_back("required_content_manifest_fingerprint must be a 64-character SHA-256 hex string");
		}
		if (a_options.requiredModpackUrl && !F4T::Identity::NormalizeModpackUrl(a_options.requiredModpackUrl)) {
			errors.push_back("required_modpack_url must be an https:// URL of 256 characters or fewer (plain http is only allowed for 127.0.0.1 or localhost)");
		}
		if (a_options.modpackName && !F4T::ModpackManifest::IsValidModpackName(*a_options.modpackName)) {
			errors.push_back("modpack_name must be 1-80 printable characters without < or > when set");
		}
		const bool hasModpack = a_modpackSourceConfigured.value_or(a_options.requiredModpackManifestJson.has_value());
		if (a_options.modpackName && !hasModpack) {
			errors.push_back("modpack_name requires a modpack manifest or MO2 profile");
		}
		if (a_options.allowModpackDownloads) {
			if (!hasModpack) {
				errors.push_back("allow_modpack_downloads requires a modpack manifest or MO2 profile");
			}
			if (!a_options.modpackDownloadDirectory || a_options.modpackDownloadDirectory->empty()) {
				errors.push_back("allow_modpack_downloads requires modpack_download_dir");
			}
			if (a_options.modpackDownloadable.empty()) {
				errors.push_back("allow_modpack_downloads requires modpack_downloadable to list the files you may redistribute, or [\"*\"] for every file in modpack_download_dir");
			}
		}
		if (a_options.modpackDownloadable.size() > kMaximumModpackDownloadableEntries) {
			errors.push_back("modpack_downloadable cannot contain more than 8192 entries");
		}
		for (const auto& name : a_options.modpackDownloadable) {
			if (name == "*") {
				if (a_options.modpackDownloadable.size() != 1) {
					errors.push_back("modpack_downloadable must be [\"*\"] on its own or a list of file names");
				}
				continue;
			}
			if (!F4T::ModpackManifest::IsInstallableName(name)) {
				errors.push_back("modpack_downloadable entry " + name + " is not a plugin, plugin archive or resource view name that can be downloaded");
			}
		}
		if (a_options.modpackDownloadConcurrency == 0) {
			errors.push_back("modpack_download_concurrency must be 1-16");
		}
		if (a_options.modpackDownloadBytesPerHourPerAddress == 0) {
			errors.push_back("modpack_download_bytes_per_hour_per_address must be 1 to 42949672960 (40 GiB)");
		}
		if (a_options.requiredCapabilities.size() > 64) {
			errors.push_back("required_capabilities cannot contain more than 64 entries");
		}
		std::unordered_set<std::string> seen;
		for (const auto& capability : a_options.requiredCapabilities) {
			if (!IsPrintableBounded(capability, 1, 64) || Trim(capability).empty()) {
				errors.push_back("required_capabilities entries must be 1-64 non-whitespace printable characters");
			} else if (!seen.insert(capability).second) {
				errors.push_back("required_capabilities contains a duplicate entry");
			}
		}
		if (a_options.adminProfiles.size() > 64) {
			errors.push_back("admin_profiles cannot contain more than 64 entries");
		}
		std::unordered_set<std::string> seenAdmins;
		for (const auto& admin : a_options.adminProfiles) {
			if (admin.find(':') != std::string::npos) {
				errors.push_back("admin_profiles entries are profile ids; the 'profileId:secret' form is not supported");
			} else if (!F4T::Identity::IsValidProfileId(admin)) {
				errors.push_back("admin_profiles entries must be a profile id");
			} else if (!seenAdmins.insert(admin).second) {
				errors.push_back("admin_profiles contains a duplicate entry");
			}
		}
		return errors;
	}

	std::vector<std::string> ListedTextSaveErrors(const Options& a_options)
	{
		std::vector<std::string> errors;
		if (!a_options.publicListing) {
			return errors;
		}
		CollectListedTextSaveErrors(a_options, errors);
		return errors;
	}

	bool IsAllowedDirectoryUrl(std::string_view a_url)
	{
		if (a_url.size() > 512) {
			return false;
		}
		for (const unsigned char character : a_url) {
			if (character <= 0x20 || character >= 0x7f) {
				return false;
			}
		}
		const auto schemeEnd = a_url.find("://");
		if (schemeEnd == std::string_view::npos) {
			return false;
		}
		const auto scheme = a_url.substr(0, schemeEnd);
		const auto afterScheme = a_url.substr(schemeEnd + 3);
		const auto authority = afterScheme.substr(0, afterScheme.find_first_of("/?#"));
		if (authority.find('@') != std::string_view::npos) {
			return false;
		}
		const auto host = authority.substr(0, authority.find(':'));
		if (scheme == "https") {
			return !host.empty();
		}
		return scheme == "http" && (host == "127.0.0.1" || host == "localhost");
	}

	bool IsAllowedAppealLink(std::string_view a_link)
	{
		if (a_link.empty()) {
			return true;
		}
		if (a_link.size() > 200) {
			return false;
		}
		for (const unsigned char character : a_link) {
			if (character <= 0x20 || character >= 0x7f) {
				return false;
			}
		}
		const auto schemeEnd = a_link.find("://");
		if (schemeEnd == std::string_view::npos) {
			return false;
		}
		const auto scheme = a_link.substr(0, schemeEnd);
		const auto afterScheme = a_link.substr(schemeEnd + 3);
		const auto authority = afterScheme.substr(0, afterScheme.find_first_of("/?#"));
		return (scheme == "http" || scheme == "https") && !authority.empty() && authority.find('@') == std::string_view::npos;
	}

}
