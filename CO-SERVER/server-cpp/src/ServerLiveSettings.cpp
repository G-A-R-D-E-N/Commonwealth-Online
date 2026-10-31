#include "ServerLiveSettings.h"

#include "DirectoryRegistrar.h"
#include "F4TConnectionPolicy.h"
#include "F4TContentManifest.h"
#include "ServerConfiguration.h"
#include "F4TDownedState.h"
#include "F4TUtf8Path.h"
#include "IdentityGate.h"
#include "LanDiscoveryService.h"
#include "PersistenceGate.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <utility>

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
			.listensOnIpv6 = F4T::ConnectionPolicy::Ipv6Prefix64(a_options.bindHost).has_value() };
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
			requested = CommonwealthOnline::Server::Configuration::Parse(a_document, (*a_context.baseDirectory));
		} catch (const std::exception& exception) {
			return AdminActionResult{ false, "The running server could not read the settings: " + std::string{ exception.what() }, nullptr };
		}
		auto validationErrors = CommonwealthOnline::Server::Configuration::Validate(requested);
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
			options.doorsShared = requested.doorsShared;
			options.vats = requested.vats;
			F4T::WorldPolicy::SetServerDefaultVats(requestedVats);
			auto frames = (*a_context.protocol).SetWorldPolicy(F4T::WorldPolicy::DerivePolicy(
				(*a_context.worldPolicyData), (*a_context.worldMode), options.doorsShared, requestedVats));
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
		if (requested.downedSeconds != options.downedSeconds) {
			options.downedSeconds = requested.downedSeconds;
			(*a_context.protocol).SetDownedConfig(F4T::ServerConfiguration::DownedConfig(options));
			mark("downedSeconds");
		}
		if (requested.enableChat != options.enableChat) {
			options.enableChat = requested.enableChat;
			(*a_context.protocol).SetChatEnabled(options.enableChat);
			mark("enableChat");
		}
		if (requested.enablePersistence != options.enablePersistence) {
			if (a_context.persistence != nullptr) {
				if (requested.enablePersistence) {
					a_context.persistence->SetSavingEnabled(true);
					(void)(*a_context.protocol).FlushFactionState();
				} else {
					(void)(*a_context.protocol).FlushFactionState();
					a_context.persistence->SetSavingEnabled(false);
				}
				options.enablePersistence = requested.enablePersistence;
				mark("enablePersistence");
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
		const auto vats = F4T::WorldPolicy::ParseVats(a_context.options->vats)
			.value_or(F4T::WorldPolicy::Vats::Disabled);
		auto frames = a_context.protocol->SetWorldPolicy(
			F4T::WorldPolicy::DerivePolicy(*a_context.worldPolicyData, *a_context.worldMode, a_context.options->doorsShared, vats));
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
				{ "disabled_entries", policy.disable.size() }
			}
		};
	}
}
