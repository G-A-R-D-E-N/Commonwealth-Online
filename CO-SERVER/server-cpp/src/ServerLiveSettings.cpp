#include "ServerLiveSettings.h"

#include "DirectoryRegistrar.h"
#include "F4TConnectionPolicy.h"
#include "F4TContentManifest.h"
#include "ServerConfiguration.h"
#include "F4TDownedState.h"
#include "F4TProfanityFilter.h"
#include "F4TServerSettingsFile.h"
#include "F4TUtf8Path.h"
#include "IdentityGate.h"
#include "LanDiscoveryService.h"
#include "PersistenceGate.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	DirectoryListing DirectoryListingFor(const CommonwealthOnline::Server::Configuration::Options& a_options, std::uint16_t a_localPort)
	{
		return CommonwealthOnline::Server::DirectoryListing{
			.name = a_options.serverName,
			.description = a_options.serverDescription,
			.port = a_localPort,
			.maximumPlayers = a_options.maximumPlayers,
			.clientVersion = a_options.requiredClientVersion,
			.falloutRuntime = a_options.requiredFalloutRuntimeVersion,
			.contentManifestFingerprint = a_options.requiredContentManifestFingerprint,
			.capabilities = a_options.requiredCapabilities,
			.passworded = a_options.serverPassword.has_value(),
			.modpackFingerprint = a_options.requiredModpackFingerprint,
			.listensOnIpv6 = F4T::ConnectionPolicy::Ipv6Prefix64(a_options.bindHost).has_value(), .region = F4T::ServerConfiguration::RegionDirectoryName(a_options) };
	}

	void UpdateAdvertisements(ServerLiveSettingsContext& a_context, const CommonwealthOnline::Server::Configuration::Options& a_options)
	{
		a_context.discovery->UpdateAdvertisement(
			a_options.serverName,
			a_options.serverDescription,
			a_options.maximumPlayers,
			a_options.serverPassword.has_value());
		if (a_context.directory) {
			if (*a_context.directory) { (*a_context.directory)->UpdateListing(DirectoryListingFor(a_options, a_context.localPort)); }
		}
	}

	AdminActionResult ApplyServerLiveSettings(ServerLiveSettingsContext& a_context, const nlohmann::json& a_document)
	{
		auto& options = *a_context.options;
		using CommonwealthOnline::Server::Configuration::Options;
		Options requested;
		try {
			requested = CommonwealthOnline::Server::Configuration::Parse(a_document, (*a_context.baseDirectory), false);
		} catch (const std::exception& exception) {
			return AdminActionResult{ false, "The running server could not read the settings: " + std::string{ exception.what() }, nullptr };
		}
		// constraint: a P2P-locked server must never start a directory registrar from a save.
		if (options.publicListingLocked) {
			requested.publicListing = false;
		}
		std::vector<std::string> validationErrors;
		try {
			validationErrors = CommonwealthOnline::Server::Configuration::Validate(requested, F4T::ServerConfiguration::HasConfiguredRequiredModpack(a_document, *a_context.baseDirectory));
		} catch (const std::exception& exception) {
			return AdminActionResult{ false, "The running server could not read the settings: " + std::string{ exception.what() }, nullptr };
		}
		if (!validationErrors.empty()) {
			return AdminActionResult{ false, validationErrors.front(), nlohmann::json{ { "errors", validationErrors } } };
		}
		const auto requestedMode = F4T::WorldPolicy::ParseMode(requested.worldMode);
		if (!requestedMode) {
			return AdminActionResult{ false, "Unknown world mode '" + requested.worldMode + "'.", nullptr };
		}
		if (!F4T::WorldPolicy::CanServeMode((*a_context.worldPolicyData), *requestedMode)) {
			return AdminActionResult{
				false,
				F4T::WorldPolicy::UnservableModeMessage(F4T::Utf8Path::Encode((*a_context.worldPolicyPath)), *requestedMode),
				nullptr
			};
		}
		if (requested.publicListing && requested.directoryUrl.empty()) {
			return AdminActionResult{ false, "A directory URL is needed for the public listing.", nullptr };
		}
		if ((requested.serverPassword.has_value() || requested.serverPassword != options.serverPassword) && !a_context.identityGate) {
			return AdminActionResult{ false, "A server password needs Steam identity verification enabled.", nullptr };
		}
		nlohmann::json applied = nlohmann::json::array();
		nlohmann::json fieldErrors = nlohmann::json::array();
		bool resyncFactions = false;
		const auto mark = [&applied](const char* a_field) { applied.push_back(a_field); };
		const auto fieldError = [&fieldErrors](const char* a_field, std::string a_message) {
			fieldErrors.push_back(nlohmann::json{ { "field", a_field }, { "message", std::move(a_message) } });
		};

		if (requested.serverName != options.serverName) {
			options.serverName = requested.serverName;
			mark("serverName");
		}
		if (requested.serverDescription != options.serverDescription) {
			options.serverDescription = requested.serverDescription;
			mark("serverDescription");
		}
		if (requested.serverName != (*a_context.protocol).ServerInfoPacket().value("serverName", std::string{}) ||
			requested.serverDescription != (*a_context.protocol).ServerInfoPacket().value("serverDescription", std::string{})) {
			(*a_context.protocol).SetIdentity(options.serverName, options.serverDescription);
		}
		if (requested.region != options.region) {
			options.region = requested.region;
			mark("region");
		}
		if (requested.maximumPlayers != options.maximumPlayers) {
			options.maximumPlayers = requested.maximumPlayers;
			(*a_context.admission).maximumPlayers = requested.maximumPlayers;
			(*a_context.sessionAdmissions).SetMaximumPlayers(requested.maximumPlayers);
			(*a_context.protocol).SetAdvertisedMaximumPlayers(requested.maximumPlayers);
			mark("maxPlayers");
		}
		if (requested.serverPassword != options.serverPassword) {
			options.serverPassword = requested.serverPassword;
			if (a_context.identityGate) {
				a_context.identityGate->SetServerPasswordSha256(requested.serverPassword ?
					std::optional{ F4T::ContentManifest::Sha256Hex(*requested.serverPassword) } : std::nullopt);
			}
			if (a_context.steamLink) ApplySteamHostPolicy(*a_context.steamLink, options.steamFriendsOnly, options.serverPassword.has_value());
			mark("password");
		}
		if (requested.resumeTokenLifetimeHours != options.resumeTokenLifetimeHours) {
			options.resumeTokenLifetimeHours = requested.resumeTokenLifetimeHours;
			if (a_context.identityGate) {
				a_context.identityGate->SetResumeLifetimeSeconds(static_cast<double>(requested.resumeTokenLifetimeHours) * 60.0 * 60.0);
			}
			mark("resumeTokenLifetimeHours");
		}
		const auto requestedVats = F4T::WorldPolicy::ParseVats(requested.vats)
			.value_or(F4T::WorldPolicy::Vats::Disabled);
		const auto& currentPolicy = (*a_context.protocol).CurrentWorldPolicy();
		const bool vatsChanged =
			std::string{ F4T::WorldPolicy::VatsName(requestedVats) } != std::string{ F4T::WorldPolicy::VatsName(currentPolicy.vats) };
		if (std::string{ F4T::WorldPolicy::ModeName(*requestedMode) } !=
			std::string{ F4T::WorldPolicy::ModeName(currentPolicy.mode) } ||
			requested.doorsShared != options.doorsShared ||
			vatsChanged) {
			(*a_context.worldMode) = *requestedMode;
			options.worldMode = std::string{ F4T::WorldPolicy::ModeName(*requestedMode) };
			options.doorsShared = requested.doorsShared;
			options.vats = requested.vats;
			F4T::WorldPolicy::SetServerDefaultVats(requestedVats);
			auto frames = (*a_context.protocol).SetWorldPolicy(DeriveServerWorldPolicy(
				(*a_context.worldPolicyData), (*a_context.worldMode), options.doorsShared, requestedVats, options.questOverrides));
			a_context.sendFrames(std::move(frames));
			mark("worldMode");
			mark("doorsShared");
			if (vatsChanged) {
				mark("vats");
			}
		}
		if (requested.friendlyFire != options.friendlyFire || requested.factionFriendlyFire != options.factionFriendlyFire) {
			options.friendlyFire = requested.friendlyFire;
			options.factionFriendlyFire = requested.factionFriendlyFire;
			(*a_context.protocol).SetFriendlyFire(options.friendlyFire, options.factionFriendlyFire);
			mark("friendlyFire");
			mark("factionFriendlyFire");
		}
		if (requested.survivalMode != options.survivalMode) {
			options.survivalMode = requested.survivalMode;
			(*a_context.protocol).SetSurvivalMode(options.survivalMode);
			mark("survivalMode");
		}
		if (requested.survivalNeeds != options.survivalNeeds) {
			options.survivalNeeds = requested.survivalNeeds;
			(*a_context.protocol).SetSurvivalNeeds(options.survivalNeeds);
			mark("survivalNeeds");
		}
		if (requested.requireDifficultyReport != options.requireDifficultyReport) {
			options.requireDifficultyReport = requested.requireDifficultyReport;
			(*a_context.protocol).SetRequireDifficultyReport(options.requireDifficultyReport);
			mark("requireDifficultyReport");
		}
		if (requested.requiredDifficulty != options.requiredDifficulty) {
			options.requiredDifficulty = requested.requiredDifficulty;
			(*a_context.protocol).SetRequiredDifficulty(options.requiredDifficulty);
			mark("requiredDifficulty");
		}
		if (requested.downedSeconds != options.downedSeconds) {
			options.downedSeconds = requested.downedSeconds;
			(*a_context.protocol).SetDownedConfig(F4T::ServerConfiguration::DownedConfig(options));
			mark("downedSeconds");
		}
		if (requested.allowDeathBagLootByOthers != options.allowDeathBagLootByOthers) {
			options.allowDeathBagLootByOthers = requested.allowDeathBagLootByOthers;
			(*a_context.protocol).SetDeathBagLootByOthers(options.allowDeathBagLootByOthers);
			mark("allowDeathBagLootByOthers");
		}
		if (requested.enableChat != options.enableChat) {
			options.enableChat = requested.enableChat;
			(*a_context.protocol).SetChatEnabled(options.enableChat);
			mark("enableChat");
		}
		if (requested.profanityFilter != options.profanityFilter) {
			options.profanityFilter = requested.profanityFilter;
			F4T::ProfanityFilter::SetProfanityEnabled(options.profanityFilter);
			mark("profanityFilter");
		}
		if (requested.enablePersistence != options.enablePersistence) {
			if (a_context.persistence != nullptr) {
				bool applied = true;
				if (requested.enablePersistence) {
					a_context.persistence->SetSavingEnabled(true);
					(void)(*a_context.protocol).FlushFactionState();
					(void)(*a_context.protocol).FlushSocialState();
				} else {
					const bool factionsSaved = (*a_context.protocol).FlushFactionState();
					const bool socialSaved = (*a_context.protocol).FlushSocialState();
					applied = factionsSaved && socialSaved && a_context.persistence->SetSavingEnabled(false);
				}
				if (applied) {
					options.enablePersistence = requested.enablePersistence;
					mark("enablePersistence");
				} else {
					fieldError("enablePersistence", "Could not save pending player data, so saving stays on. Try again in a moment.");
				}
			} else {
				fieldError("enablePersistence", "Durable persistence is not available on this server, so the setting is saved for the next Host.");
			}
		}
		if (requested.enableFactions != options.enableFactions) {
			if ((*a_context.protocol).SetFactionsEnabled(requested.enableFactions, a_context.persistence)) {
				options.enableFactions = requested.enableFactions;
				if (options.enableFactions) {
					resyncFactions = true;
				}
				mark("enableFactions");
			} else {
				fieldError("enableFactions", "Player factions could not be switched.");
			}
		}
		if (requested.enableObjectState != options.enableObjectState) {
			options.enableObjectState = requested.enableObjectState;
			(*a_context.protocol).SetObjectStateEnabled(options.enableObjectState);
			mark("enableObjectState");
		}
		if (requested.enableLanDiscovery != options.enableLanDiscovery) {
			if (requested.enableLanDiscovery) {
				std::string startError;
				if ((*a_context.discovery).Start(startError)) {
					options.enableLanDiscovery = true;
					mark("lanDiscoveryEnabled");
				} else {
					fieldError("lanDiscoveryEnabled", "LAN discovery could not start: " + startError);
				}
			} else {
				(*a_context.discovery).Stop();
				options.enableLanDiscovery = false;
				mark("lanDiscoveryEnabled");
			}
		}
		if (requested.publicListing != options.publicListing) {
			if (requested.publicListing) {
				try {
					(*a_context.directory) = std::make_unique<CommonwealthOnline::Server::DirectoryRegistrar>(
						requested.directoryUrl,
						DirectoryListingFor(requested, a_context.localPort),
						(*a_context.activePlayers),
						CommonwealthOnline::Server::CreatePlatformDirectorySender());
					(*a_context.directory)->Start();
					options.publicListing = true;
					mark("publicListing");
				} catch (const std::exception& exception) {
					(*a_context.directory).reset();
					fieldError("publicListing", "The public listing could not start: " + std::string{ exception.what() });
				}
			} else {
				if (*a_context.directory) {
					(*a_context.directory)->Stop();
					(*a_context.directory).reset();
				}
				options.publicListing = false;
				mark("publicListing");
			}
		}
		UpdateAdvertisements(a_context, options);
		if (!applied.empty() || !fieldErrors.empty()) {
			a_context.broadcastPacket((*a_context.protocol).ServerInfoPacket());
		}
		if (resyncFactions) {
			const auto unixTimeSeconds = std::chrono::duration<double>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			a_context.sendFrames((*a_context.protocol).FactionResyncFrames(unixTimeSeconds));
		}
		if (!fieldErrors.empty()) {
			const auto failureMessage = fieldErrors.front().at("message").get<std::string>();
			return AdminActionResult{
				true,
				"Server settings applied live, except: " + failureMessage,
				nlohmann::json{ { "applied", std::move(applied) }, { "errors", std::move(fieldErrors) } }
			};
		}
		return AdminActionResult{
			true,
			"Server settings applied live.",
			nlohmann::json{ { "applied", std::move(applied) }, { "errors", nlohmann::json::array() } }
		};
	}

	F4T::WorldPolicy::Policy DeriveServerWorldPolicy(
		const F4T::WorldPolicy::Data& a_data,
		F4T::WorldPolicy::Mode a_mode,
		bool a_doorsShared,
		F4T::WorldPolicy::Vats a_vats,
		const std::vector<F4T::QuestProtocol::OverrideEntry>& a_overrides)
	{
		auto policy = F4T::WorldPolicy::DerivePolicyV2(a_data, a_mode, a_doorsShared, a_vats);
		for (const auto& entry : a_overrides) {
			if (entry.policy == F4T::QuestProtocol::Override::Blocked) {
				const F4T::WorldPolicy::FormKey key{ entry.quest.plugin, entry.quest.localId };
				const auto text = F4T::WorldPolicy::FormatFormKey(key);
				if (!F4T::WorldPolicy::ParseFormKey(text)) {
					std::cerr << "World policy: skipped blocked quest " << text << ", its plugin name cannot be sent in a world policy\n";
					continue;
				}
				policy.blockedQuests.push_back(key);
			}
		}
		return policy;
	}

	AdminActionResult ApplyAdminMapPublic(ProtocolHost& a_protocol, std::optional<bool> a_enabled)
	{
		if (a_enabled) a_protocol.SetMapPublicPlayers(*a_enabled);
		const bool visible = a_protocol.MapPublicPlayers();
		return AdminActionResult{
			true,
			std::string{ "Public map players are " } + (visible ? "shown." : "hidden."),
			nlohmann::json{ { "enabled", visible } }
		};
	}

	AdminActionResult ApplyAdminWorldMode(ServerLiveSettingsContext& a_context, std::string_view a_mode)
	{
		const auto mode = F4T::WorldPolicy::ParseMode(a_mode);
		if (!mode) {
			return AdminActionResult{
				false,
				"Unknown world mode '" + std::string{ a_mode } + "'. Use coop-story or free-roam.",
				nullptr
			};
		}
		if (!F4T::WorldPolicy::CanServeMode(*a_context.worldPolicyData, *mode)) {
			return AdminActionResult{
				false,
				F4T::WorldPolicy::UnservableModeMessage(F4T::Utf8Path::Encode(*a_context.worldPolicyPath), *mode),
				nullptr
			};
		}
		*a_context.worldMode = *mode;
		a_context.options->worldMode = std::string{ F4T::WorldPolicy::ModeName(*mode) };
		const auto vats = F4T::WorldPolicy::ParseVats(a_context.options->vats)
			.value_or(F4T::WorldPolicy::Vats::Disabled);
		auto frames = a_context.protocol->SetWorldPolicy(
			DeriveServerWorldPolicy(*a_context.worldPolicyData, *a_context.worldMode, a_context.options->doorsShared, vats, a_context.options->questOverrides));
		a_context.sendFrames(std::move(frames));
		const auto policy = a_context.protocol->CurrentWorldPolicy();
		return AdminActionResult{
			true,
			"World mode set to " + std::string{ F4T::WorldPolicy::ModeName(policy.mode) } + ".",
			nlohmann::json{
				{ "mode", std::string{ F4T::WorldPolicy::ModeName(policy.mode) } },
				{ "doors", std::string{ F4T::WorldPolicy::DoorsName(policy.doors) } },
				{ "vats", std::string{ F4T::WorldPolicy::VatsName(policy.vats) } },
				{ "suppress_quests", policy.suppressQuests },
				{ "disabled_entries", policy.disable.size() + policy.noInteract.size() }
			}
		};
	}

	namespace
	{
		std::optional<int> WholeNumberInRange(const nlohmann::json& a_node, int a_minimum, int a_maximum)
		{
			if (!a_node.is_number()) {
				return std::nullopt;
			}
			const auto value = a_node.get<double>();
			if (!std::isfinite(value) || value != std::trunc(value) || value < a_minimum || value > a_maximum) {
				return std::nullopt;
			}
			return static_cast<int>(value);
		}

		bool ApplyRaidSettingsDocument(F4T::WorkshopRaids::RaidConfig& a_config, const nlohmann::json& a_document, std::string& a_error)
		{
			auto next = a_config;
			if (const auto found = a_document.find("mode"); found != a_document.end()) {
				const auto mode = found->is_string() ? F4T::WorkshopRaids::ParseRaidMode(found->get_ref<const std::string&>()) : std::nullopt;
				if (!mode) {
					a_error = "Raid mode must be open, windowed or off.";
					return false;
				}
				next.mode = *mode;
			}
			if (const auto found = a_document.find("windowStartHour"); found != a_document.end()) {
				const auto hour = WholeNumberInRange(*found, 0, 23);
				if (!hour) {
					a_error = "Raid windowStartHour must be a whole number from 0 to 23.";
					return false;
				}
				next.windowStartHour = static_cast<float>(*hour);
			}
			if (const auto found = a_document.find("windowDurationHours"); found != a_document.end()) {
				const auto hours = WholeNumberInRange(*found, 1, 24);
				if (!hours) {
					a_error = "Raid windowDurationHours must be a whole number from 1 to 24.";
					return false;
				}
				next.windowDurationHours = static_cast<float>(*hours);
			}
			a_config = next;
			return true;
		}
	}

	nlohmann::json RaidSettingsJson(const F4T::WorkshopRaids::RaidConfig& a_config)
	{
		return nlohmann::json{
			{ "mode", std::string{ F4T::WorkshopRaids::RaidModeName(a_config.mode) } },
			{ "windowStartHour", static_cast<int>(a_config.windowStartHour) },
			{ "windowDurationHours", static_cast<int>(a_config.windowDurationHours) }
		};
	}

	AdminActionResult ApplyAdminRaidSettings(ServerLiveSettingsContext& a_context, const nlohmann::json& a_patch)
	{
		auto next = a_context.protocol->WorkshopRaidConfig();
		std::string error;
		if (!ApplyRaidSettingsDocument(next, a_patch, error)) {
			return AdminActionResult{ false, error, nullptr };
		}
		auto roundTrip = next;
		if (!ApplyRaidSettingsDocument(roundTrip, RaidSettingsJson(next), error)) {
			return AdminActionResult{ false, "The configured raid window is out of range; set windowStartHour and windowDurationHours explicitly. " + error, nullptr };
		}
		try {
			F4T::ServerSettings::WriteDocument(*a_context.baseDirectory / "raid_settings.json", RaidSettingsJson(next));
		} catch (const std::exception& exception) {
			return AdminActionResult{ false, "Raid settings were not saved: " + std::string{ exception.what() }, nullptr };
		}
		a_context.protocol->SetWorkshopRaidConfig(next);
		return AdminActionResult{ true, "Raid settings updated.", RaidSettingsJson(next) };
	}

	bool LoadRaidSettingsFile(const std::filesystem::path& a_path, F4T::WorkshopRaids::RaidConfig& a_config, std::string& a_error)
	{
		std::error_code error;
		if (!std::filesystem::exists(a_path, error) && !error) {
			return true;
		}
		try {
			return ApplyRaidSettingsDocument(a_config, F4T::ServerSettings::ReadDocument(a_path), a_error);
		} catch (const std::exception& exception) {
			a_error = "Raid settings file " + F4T::Utf8Path::Encode(a_path) + " could not be read: " + exception.what();
			return false;
		}
	}
}
