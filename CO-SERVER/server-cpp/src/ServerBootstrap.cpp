#include "ServerBootstrap.h"

#include "AdminTokenStore.h"
#include "F4TClientCompatibility.h"
#include "F4TProfanityFilter.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TUtf8Path.h"
#include "ServerArguments.h"
#include "ServerLiveSettings.h"
#include "ServerRuntimeSupport.h"
#include "SteamWebApiVerifier.h"

#include <array>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server
{
	std::optional<int> ServerBootstrap::Prepare(int argc, char** argv, ServerBootstrap& a_out)
	{
		std::optional<std::filesystem::path> configPath;
		for (int index = 1; index < argc; ++index) {
			const std::string_view argument{ argv[index] };
			if (argument == "--config" && index + 1 < argc) {
				configPath = std::filesystem::absolute(argv[++index]);
			}
		}
		a_out.options.requiredClientVersion = std::string{ F4T::ClientCompatibility::kClientVersion };
		try {
			if (configPath) {
				const auto root = Arguments::ReadConfiguration(*configPath);
				if (F4T::ServerConfiguration::HasConfiguredMo2Profile(root, configPath->parent_path())) {
					std::error_code error;
					const auto identity = std::filesystem::weakly_canonical(configPath->parent_path(), error);
					if (error) throw std::runtime_error("Cannot identify the server configuration directory: " + error.message());
					std::string lockError;
					if (!a_out.instanceLock.Acquire(identity / ".commonwealth-server.lock", lockError)) {
						throw std::runtime_error("Cannot lock the server directory; another server may be running or it may not be writable: " + lockError);
					}
				}
				a_out.options = Configuration::Parse(root, configPath->parent_path());
			}
		} catch (const std::exception& exception) {
			std::cerr << "Failed to load server configuration: " << exception.what() << '\n';
			return 2;
		}

		for (int index = 1; index < argc; ++index) {
			const std::string_view argument{ argv[index] };
			if (argument == "--config" && index + 1 < argc) {
				++index;
				continue;
			}
			if (argument == "--bind" && index + 1 < argc) {
				a_out.options.bindHost = argv[++index];
				continue;
			}
			if (argument == "--port" && index + 1 < argc) {
				if (!Arguments::ParsePort(argv[++index], a_out.options.port)) {
					std::cerr << "Invalid --port value\n";
					return 2;
				}
				continue;
			}
			if (argument == "--admin-port" && index + 1 < argc) {
				if (!Arguments::ParsePort(argv[++index], a_out.options.adminPort)) {
					std::cerr << "Invalid --admin-port value\n";
					return 2;
				}
				continue;
			}
			if (argument == "--name" && index + 1 < argc) {
				a_out.options.serverName = argv[++index];
				continue;
			}
			if (argument == "--description" && index + 1 < argc) {
				a_out.options.serverDescription = argv[++index];
				continue;
			}
			if (argument == "--max-players" && index + 1 < argc) {
				if (!Arguments::ParseMaximumPlayers(argv[++index], a_out.options.maximumPlayers)) {
					std::cerr << "Invalid --max-players value; expected 1-"
						<< F4T::RemotePlayerCapacity::MaximumSessionPlayers << '\n';
					return 2;
				}
				continue;
			}
			if (argument == "--enable-persistence") {
				a_out.options.enablePersistence = true;
				continue;
			}
			if (argument == "--durable-state" && index + 1 < argc) {
				a_out.options.enablePersistence = true;
				a_out.options.durableStatePath = argv[++index];
				continue;
			}
			if (argument == "--world-mode" && index + 1 < argc) {
				a_out.options.worldMode = argv[++index];
				continue;
			}
			if (argument == "--no-shared-doors") {
				a_out.options.doorsShared = false;
				continue;
			}
			if (argument == "--lan-discovery-port" && index + 1 < argc) {
				if (!Arguments::ParsePort(argv[++index], a_out.options.lanDiscoveryPort)) {
					std::cerr << "Invalid --lan-discovery-port value; expected 1-65535\n";
					return 2;
				}
				continue;
			}
			if (argument == "--list-publicly") {
				a_out.options.publicListing = true;
				a_out.options.publicListingLocked = false;
				continue;
			}
			if (argument == "--no-public-listing") {
				a_out.options.publicListing = false;
				a_out.options.publicListingLocked = true;
				continue;
			}
			if (argument == "--directory-url" && index + 1 < argc) {
				a_out.options.directoryUrl = argv[++index];
				continue;
			}
			if (argument == "--no-lan-discovery") {
				a_out.options.enableLanDiscovery = false;
				continue;
			}
			if (argument == "--resume-token-lifetime-hours" && index + 1 < argc) {
				if (!Arguments::ParseResumeLifetimeHours(argv[++index], a_out.options.resumeTokenLifetimeHours)) {
					std::cerr << "Invalid --resume-token-lifetime-hours value; expected 1-8760\n";
					return 2;
				}
				continue;
			}
			if (argument == "--resume-token-lifetime-seconds" && index + 1 < argc) {
				if (!Arguments::ParseResumeLifetimeSeconds(argv[++index], a_out.options.resumeTokenLifetimeSeconds)) {
					std::cerr << "Invalid --resume-token-lifetime-seconds value; expected 1-31536000\n";
					return 2;
				}
				continue;
			}
			if (argument == "--help") {
				std::cout
					<< "Usage: CommonwealthOnlineServerCpp [--config FILE] [--bind IPV4] [--port PORT] [--admin-port PORT] [--name NAME] [--description TEXT] "
					   "[--max-players 1-100] [--enable-persistence | --durable-state PATH] "
					   "[--world-mode coop-story|free-roam] [--no-shared-doors] "
					   "[--resume-token-lifetime-hours 1-8760] [--resume-token-lifetime-seconds 1-31536000] "
					   "[--lan-discovery-port PORT] [--no-lan-discovery] [--list-publicly | --no-public-listing] [--directory-url HTTPS_URL] [--setup-steam-key | --remove-steam-key]\n";
				return 0;
			}
			std::cerr << "Unknown argument: " << argument << '\n';
			return 2;
		}

		const auto validationErrors = Configuration::Validate(a_out.options);
		if (!validationErrors.empty()) {
			for (const auto& error : validationErrors) {
				std::cerr << "Configuration error: " << error << '\n';
			}
			return 2;
		}

		if (a_out.options.appealLink.size() > 111) {
			std::cerr << "WARNING: appeal_link is longer than 111 characters, so admission refusals leave it out. Keep it at 111 characters or less.\n";
		}

		if (!a_out.options.requiredModpackFingerprint) {
			std::cerr << "WARNING: required_modpack_path is not set, so this server accepts any client mod setup. "
				<< "Set required_modpack_path or an MO2 instance and profile to reject clients whose plugins or load order differ.\n";
		}

		a_out.baseDirectory = configPath ? configPath->parent_path() : std::filesystem::current_path();
		if (a_out.options.requireSteamIdentity) {
			std::string keyError;
			if (!LoadSteamWebApiKey(a_out.baseDirectory, keyError)) {
				std::cerr << "Configuration error: require_steam_identity needs a Steam Web API key" << (keyError.empty() ? std::string{} : ": " + keyError) << ". Run CommonwealthOnline.Server.exe --setup-steam-key first." << '\n';
				return 2;
			}
		}
		if (std::string raidError; !LoadRaidSettingsFile(a_out.baseDirectory / "raid_settings.json", a_out.options.workshopRaid, raidError)) {
			std::cerr << raidError << '\n';
			return 2;
		}
		const auto adminTokenPath = a_out.baseDirectory / ".admin-token";
		const auto bansPath = a_out.baseDirectory / "bans.json";

		std::optional<std::filesystem::path> durableStatePath;
		if (a_out.options.enablePersistence) {
			durableStatePath = std::filesystem::path{ a_out.options.durableStatePath };
			if (durableStatePath->is_relative()) {
				durableStatePath = a_out.baseDirectory / *durableStatePath;
			}
		}

		a_out.worldPolicyPath = F4T::Utf8Path::Decode(a_out.options.worldPolicyPath);
		if (a_out.worldPolicyPath.is_relative()) {
			a_out.worldPolicyPath = a_out.baseDirectory / a_out.worldPolicyPath;
		}
		if (const auto loaded = F4T::WorldPolicy::LoadData(a_out.worldPolicyPath)) {
			a_out.worldPolicyData = *loaded;
			std::cout << "World policy data loaded: version " << a_out.worldPolicyData.version
				<< ", " << a_out.worldPolicyData.EntryCount() << " entries from " << F4T::Utf8Path::Encode(a_out.worldPolicyPath) << std::endl;
		} else {
			std::cerr << "World policy data missing or invalid at " << F4T::Utf8Path::Encode(a_out.worldPolicyPath) << '\n';
		}
		a_out.vendorItemValuesPath = a_out.baseDirectory / "item-values.json";
		if (!std::filesystem::exists(a_out.vendorItemValuesPath) && argc > 0) {
			a_out.vendorItemValuesPath = std::filesystem::path{ argv[0] }.parent_path() / "item-values.json";
		}
		std::filesystem::path blockedWordsPath = a_out.baseDirectory / "blocked-words.txt";
		if (!std::filesystem::exists(blockedWordsPath) && argc > 0) {
			blockedWordsPath = std::filesystem::path{ argv[0] }.parent_path() / "blocked-words.txt";
		}
		std::ifstream blockedWordsFile{ blockedWordsPath, std::ios::binary };
		const std::string blockedWords{ std::istreambuf_iterator<char>{ blockedWordsFile }, std::istreambuf_iterator<char>{} };
		if (!F4T::ProfanityFilter::LoadWords(blockedWords)) {
			std::cerr << "Blocked word list missing or empty at " << F4T::Utf8Path::Encode(blockedWordsPath) << '\n';
			return 2;
		}
		F4T::ProfanityFilter::SetProfanityEnabled(a_out.options.profanityFilter);
		a_out.worldMode = F4T::WorldPolicy::ParseMode(a_out.options.worldMode)
			.value_or(F4T::WorldPolicy::Mode::CoopStory);
		if (!F4T::WorldPolicy::CanServeMode(a_out.worldPolicyData, a_out.worldMode)) {
			std::cerr << F4T::WorldPolicy::UnservableModeMessage(F4T::Utf8Path::Encode(a_out.worldPolicyPath), a_out.worldMode) << '\n';
			return 2;
		}
		F4T::WorldPolicy::SetServerDefaultVats(
			F4T::WorldPolicy::ParseVats(a_out.options.vats).value_or(F4T::WorldPolicy::Vats::Disabled));

		try {
			if (durableStatePath) {
				a_out.durableStore = std::make_unique<DurableStateFileStore>(
					*durableStatePath,
					[] { return RuntimeSupport::UnixTimeSeconds(); },
					[&tokens = a_out.identityTokens] { return tokens.CreateRandomHex(16); });
				const auto gainMode = F4T::InventorySnapshot::ParseGainMode(a_out.options.inventoryGainMode)
					.value_or(F4T::InventorySnapshot::GainMode::Warn);
				a_out.durableStore->SetInventoryGainPolicy(F4T::InventorySnapshot::DefaultGainPolicy(gainMode));
				auto recoveryPath = *durableStatePath;
				recoveryPath += ".save-recovery";
				a_out.saveRecoveryApprovals = std::make_unique<SaveRecoveryApprovals>(
					std::move(recoveryPath), a_out.durableStore->ServerInstanceId(),
					[] { return RuntimeSupport::UnixTimeSeconds(); });
			}
			a_out.banStore = std::make_unique<BanStore>(bansPath);
		} catch (const std::exception& exception) {
			std::cerr << "Failed to load server state: " << exception.what() << '\n';
			return 2;
		}

		std::signal(SIGINT, RuntimeSupport::HandleSignal);
		std::signal(SIGTERM, RuntimeSupport::HandleSignal);

		std::array<char, 512> error{};
		if (co_gns_server_create(a_out.options.bindHost.c_str(), a_out.options.port, &a_out.server, error.data(), error.size()) == 0 ||
			a_out.server == nullptr) {
			std::cerr << "Failed to start GNS server: " << error.data() << '\n';
			return 1;
		}

		std::string adminToken;
		try {
			adminToken = AdminTokenStore::LoadOrCreate(adminTokenPath, [&] { return a_out.identityTokens.CreateToken(); });
			if (AdminControlServer::WeakAdminToken(adminToken)) {
				adminToken = a_out.identityTokens.CreateToken();
				AdminTokenStore::Replace(adminTokenPath, adminToken);
				std::cerr << "Admin token warning: the stored token was too weak, so it was replaced with a new random one in .admin-token.\n";
			}
		} catch (const std::exception& exception) {
			std::cerr << "Failed to load or create admin token: " << exception.what() << '\n'
				<< "Admin control is disabled; the game server keeps running.\n";
			a_out.admin = std::make_unique<AdminControlServer>(a_out.options.adminPort, std::string{});
			return std::nullopt;
		}

		a_out.admin = std::make_unique<AdminControlServer>(a_out.options.adminPort, adminToken);
		std::string adminError;
		if (!a_out.admin->Start(adminError)) {
			std::cerr << "Failed to start admin control server: " << adminError << '\n';
			co_gns_server_destroy(a_out.server);
			return 1;
		}

		return std::nullopt;
	}
}
