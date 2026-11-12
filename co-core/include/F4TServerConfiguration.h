#pragma once

#include "F4TNexusSources.h"

#include "F4TCharacterSlots.h"
#include "F4TDownedState.h"
#include "F4TFactionCapacity.h"
#include "F4TIdentityProtocol.h"
#include "F4TPacketRateLimit.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestStoryOrder.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TTransportLimits.h"
#include "F4TWorkshopRaids.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ServerConfiguration
{
	inline constexpr std::size_t kDefaultModpackDownloadConcurrency = 4;
	inline constexpr std::size_t kMaximumModpackDownloadConcurrency = 16;
	inline constexpr std::uint64_t kDefaultModpackDownloadBytesPerHourPerAddress = 40ull * 1024 * 1024 * 1024;
	inline constexpr std::size_t kMaximumModpackDownloadableEntries = 8192;

	struct FactionLimits
	{
		std::size_t maximumFactions{ 512 };
		std::size_t maximumMembers{ 16 };
		double inviteLifetimeSeconds{ 60.0 };
		double inviteWindowSeconds{ 60.0 };
		std::size_t invitesPerWindow{ 8 };
		std::size_t pendingInvites{ 256 };
		std::size_t pendingInvitesPerTarget{ 16 };
		double rosterRequestIntervalSeconds{ 1.0 };
		double listRequestIntervalSeconds{ 1.0 };
		double nameRosterIntervalSeconds{ 1.0 };
		double mutationCapacity{ 4.0 };
		double mutationRefillPerSecond{ 1.0 };
	};

	struct Options
	{
		std::string bindHost{ "0.0.0.0" };
		std::uint16_t port{ 7777 };
		std::uint16_t adminPort{ 7779 };
		std::uint16_t lanDiscoveryPort{ 7778 };
		bool enableLanDiscovery{ true };
		bool publicListing{ true };
		bool publicListingLocked{};
		std::string directoryUrl{ "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory" };
		std::string serverName{ "Commonwealth Online Server" };
		std::string serverDescription;
		std::string appealLink;
		std::size_t maximumPlayers{ F4T::RemotePlayerCapacity::MaximumSessionPlayers };
		std::size_t maximumMessageBytes{ F4T::TransportLimits::DefaultMaximumMessageBytes };
		F4T::PacketRateLimit::Config inboundBudget{};
		bool requireV2Identity{ true };
		bool requireKnownProfile{ false };
		bool requireSteamIdentity{ false };
		bool enablePersistence{ true };
		bool enableChat{ true };
		bool profanityFilter{};
		bool steamFriendsOnly{ true };
		bool enableFactions{ true };
		bool enableObjectState{ true };
		bool allowDeathBagLootByOthers{ false };
		bool enableQuestSync{ true };
		std::uint32_t downedSeconds{ F4T::DownedState::DefaultDownedSeconds };
		std::uint32_t reviveChannelSeconds{ static_cast<std::uint32_t>(F4T::DownedState::DefaultReviveChannelSeconds) };
		double reviveHealthFraction{ F4T::DownedState::DefaultReviveHealthFraction };
		double reviveRangeMeters{ F4T::DownedState::DefaultReviveRangeMeters };
		double respawnGraceSeconds{ F4T::DownedState::kRespawnGraceSeconds };
		std::uint32_t stimpaksPerRevive{ F4T::DownedState::StimpaksPerRevive };
		FactionLimits factions;
		std::optional<std::string> questStoryOrderPath;
		std::optional<F4T::QuestStory::Order> questStoryOrder;
		bool survivalMode{ true };
		bool survivalNeeds{ true };
		bool requireDifficultyReport{ true };
		std::int32_t requiredDifficulty{ 4 };
		bool friendlyFire{ true };
		bool factionFriendlyFire{};
		F4T::QuestProtocol::Mode questMode{ F4T::QuestProtocol::Mode::CoopStory };
		std::vector<F4T::QuestProtocol::OverrideEntry> questOverrides;
		std::optional<nlohmann::json> startingInventory;
		std::string inventoryGainMode{ "warn" };
		std::uint32_t vendorRestockHours{ 24 };
		std::string worldMode{ "coop-story" };
		bool doorsShared{ true };
		std::string vats{ "disabled" };
		std::string worldPolicyPath{ "world-policy.json" };
		std::string durableStatePath{ "durable-state.json" };
		std::optional<std::string> requiredClientVersion;
		std::optional<std::string> requiredFalloutRuntimeVersion;
		std::optional<std::string> requiredContentManifestFingerprint;
		std::vector<std::string> requiredCapabilities;
		std::optional<std::string> serverPassword;
		std::optional<std::string> requiredModpackManifestJson;
		std::optional<std::string> requiredModpackFingerprint;
		std::optional<std::string> requiredModpackViewlessManifestJson;
		std::optional<std::string> requiredModpackViewlessFingerprint;
		bool requiredModpackViewsOnly{};
		std::optional<std::string> requiredModpackUrl;
		std::vector<F4T::NexusSources::Source> nexusSources;
		std::optional<F4T::NexusSources::Collection> nexusCollection;
		std::optional<std::string> modpackName;
		bool allowModpackDownloads{};
		std::optional<std::string> modpackDownloadDirectory;
		std::vector<std::string> modpackDownloadable;
		std::size_t modpackDownloadConcurrency{ kDefaultModpackDownloadConcurrency };
		std::uint64_t modpackDownloadBytesPerHourPerAddress{ kDefaultModpackDownloadBytesPerHourPerAddress };
		std::vector<std::string> adminProfiles;
		std::uint32_t resumeTokenLifetimeHours{ 720 };
		std::optional<std::uint32_t> resumeTokenLifetimeSeconds;
		std::size_t characterSlots{ F4T::CharacterSlots::DefaultCharacterSlots };
		F4T::WorkshopRaids::RaidConfig workshopRaid{};
		std::string region;
	};

	struct RequiredModpack
	{
		std::string manifestJson;
		std::string fingerprint;
		std::optional<std::string> viewlessManifestJson;
		std::optional<std::string> viewlessFingerprint;
		bool viewsOnly{};
		bool requiresAssetCapability{};
	};

	[[nodiscard]] Options Parse(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory = {});
	[[nodiscard]] std::string RegionDirectoryName(const Options& a_options);
	[[nodiscard]] std::vector<std::string> Validate(const Options& a_options, std::optional<bool> a_modpackSourceConfigured = std::nullopt);
	[[nodiscard]] std::vector<std::string> ListedTextSaveErrors(const Options& a_options);
	[[nodiscard]] F4T::DownedState::Config DownedConfig(const Options& a_options) noexcept;
	[[nodiscard]] bool IsAllowedDirectoryUrl(std::string_view a_url);
	[[nodiscard]] bool IsAllowedAppealLink(std::string_view a_link);
	[[nodiscard]] std::optional<std::filesystem::path> RequiredModpackPath(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory);
	[[nodiscard]] bool HasConfiguredRequiredModpack(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory);
	[[nodiscard]] bool HasConfiguredMo2Profile(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory);
	[[nodiscard]] std::optional<RequiredModpack> LoadRequiredModpack(
		const std::filesystem::path& a_path,
		std::string& a_error);
	[[nodiscard]] std::optional<RequiredModpack> LoadConfiguredRequiredModpack(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory,
		std::string& a_error);
	void SetRequiredModpack(Options& a_options, RequiredModpack a_modpack);
	[[nodiscard]] std::optional<std::string> RequiredModpackError(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory);
}
