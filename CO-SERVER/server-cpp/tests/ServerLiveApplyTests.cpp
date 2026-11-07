#include "ServerLiveSettings.h"

#include "DirectoryRegistrar.h"
#include "DurableStateFileStore.h"
#include "F4TAdmission.h"
#include "F4TServerConfiguration.h"
#include "F4TServerSettingsFile.h"
#include "F4TWorldPolicy.h"
#include "IdentityGate.h"
#include "SteamIdentityLink.h"
#include "LanDiscoveryService.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"
#include "ServerRuntimeSupport.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{
	using namespace CommonwealthOnline::Server;
	using Json = nlohmann::json;

	std::filesystem::path TempDirectory(const char* a_label)
	{
		static std::atomic_size_t counter{ 0 };
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto path = std::filesystem::temp_directory_path() /
			("co-live-apply-" + std::string{ a_label } + "-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		std::filesystem::remove_all(path);
		std::filesystem::create_directories(path);
		return path;
	}

	double SilentClock()
	{
		return 1000.0;
	}

	std::string InstanceId()
	{
		return "0123456789abcdef0123456789abcdef";
	}

	Json StoredFactionDocument()
	{
		return Json{
			{ "nextFactionNumber", 2 },
			{ "factions", Json{ { "f-1", Json{
				{ "name", "Stored Team" },
				{ "tag", "STO" },
				{ "leaderProfileId", "profile-leader" },
				{ "members", Json::array({ "profile-leader" }) },
				{ "memberNames", Json{ { "profile-leader", "Leader" } } },
				{ "officers", Json::array() },
				{ "description", "" },
				{ "color", "" },
				{ "createdAt", 100.0 }
			} } } }
		};
	}

	Json Document(Json a_overrides)
	{
		Json document = Json{
			{ "server_name", "Live Test Server" },
			{ "server_description", "Live test description" },
			{ "max_players", 8 },
			{ "world_mode", "coop-story" },
			{ "doors_shared", true },
			{ "friendly_fire", true },
			{ "faction_friendly_fire", false },
			{ "downed_seconds", 60 },
			{ "enable_chat", true },
			{ "enable_factions", false },
			{ "enable_persistence", false },
			{ "enable_object_state", true },
			{ "lan_discovery_enabled", false },
			{ "public_listing", false },
			{ "resume_token_lifetime_hours", 720 }
		};
		for (const auto& [key, value] : a_overrides.items()) {
			document[key] = value;
		}
		return document;
	}

	Json CurrentDocument(const CommonwealthOnline::Server::Configuration::Options& a_options)
	{
		auto values = F4T::ServerSettings::ReadEditable(a_options);
		values.passwordUpdate = values.serverPassword ? F4T::ServerSettings::PasswordUpdate::Set :
			F4T::ServerSettings::PasswordUpdate::Clear;
		return F4T::ServerSettings::ApplyEditable(Json::object(), values);
	}

	struct Harness
	{
		explicit Harness(const std::filesystem::path& a_directory, bool a_persistence)
			: directory{ a_directory },
			  options{ CommonwealthOnline::Server::Configuration::Parse(Document(Json::object()), a_directory) },
			  store{ a_directory / "durable-state.json", SilentClock, InstanceId },
			  gate{ store },
			  host{ "Live Test Server", "Live test description" },
			  admissions{ 8 },
			  discovery{ "Live Test Server", "Live test description", 7777, 8, activePlayers, false, 0 },
			  baseDirectory{ a_directory },
			  worldPolicyPath{ a_directory / "world-policy.json" }
		{
			options.enablePersistence = a_persistence;
			options.enableFactions = false;
			gate.SetSavingEnabled(a_persistence);
			context.options = &options;
			context.protocol = &host;
			context.persistence = &gate;
			context.activePlayers = &activePlayers;
			context.sessionAdmissions = &admissions;
			context.admission = &admission;
			context.discovery = &discovery;
			context.directory = &directoryRegistrar;
			context.worldPolicyData = &worldPolicyData;
			context.worldMode = &worldMode;
			context.baseDirectory = &baseDirectory;
			context.worldPolicyPath = &worldPolicyPath;
			context.localPort = 7777;
			context.sendFrames = [](std::vector<OutboundFrame>) {};
			context.broadcastPacket = [](const Json&) {};
		}

		AdminActionResult Apply(Json a_overrides, bool a_persistenceAvailable = true)
		{
			Json document = CurrentDocument(options);
			for (const auto& [key, value] : a_overrides.items()) {
				document[key] = value;
			}
			context.persistence = a_persistenceAvailable ? &gate : nullptr;
			return ApplyServerLiveSettings(context, document);
		}

		static bool HasApplied(const AdminActionResult& a_result, const char* a_field)
		{
			if (!a_result.data.is_object()) return false;
			const auto found = a_result.data.find("applied");
			if (found == a_result.data.end() || !found->is_array()) return false;
			for (const auto& field : *found) {
				if (field.is_string() && field.get<std::string>() == a_field) return true;
			}
			return false;
		}

		static bool HasFieldError(const AdminActionResult& a_result, const char* a_field)
		{
			if (!a_result.data.is_object()) return false;
			const auto found = a_result.data.find("errors");
			if (found == a_result.data.end() || !found->is_array()) return false;
			for (const auto& error : *found) {
				if (error.is_object() && error.value("field", std::string{}) == a_field) return true;
			}
			return false;
		}

		std::filesystem::path directory;
		CommonwealthOnline::Server::Configuration::Options options;
		DurableStateFileStore store;
		PersistenceGate gate;
		ProtocolHost host;
		std::atomic_size_t activePlayers{ 0 };
		F4T::Admission::AdmissionRegistry admissions;
		RuntimeSupport::AdmissionContext admission{};
		LanDiscoveryService discovery;
		std::unique_ptr<DirectoryRegistrar> directoryRegistrar;
		F4T::WorldPolicy::Data worldPolicyData{};
		F4T::WorldPolicy::Mode worldMode{ F4T::WorldPolicy::Mode::CoopStory };
		std::filesystem::path baseDirectory;
		std::filesystem::path worldPolicyPath;
		ServerLiveSettingsContext context{};
	};

	bool StoredFactionSurvives(Harness& a_harness)
	{
		const auto stored = a_harness.store.GetFactions();
		const auto factions = stored.find("factions");
		return factions != stored.end() && factions->is_object() && factions->contains("f-1");
	}

	void TestRaidSettingsApplyAndLoad()
	{
		const auto directory = TempDirectory("raid-settings");
		Harness harness{ directory, false };
		const auto file = directory / "raid_settings.json";
		const auto before = harness.host.WorkshopRaidConfig();

		const auto badHour = ApplyAdminRaidSettings(harness.context, Json{ { "windowStartHour", 24 } });
		assert(!badHour.ok);
		assert(badHour.message.find("windowStartHour") != std::string::npos);
		assert(!ApplyAdminRaidSettings(harness.context, Json{ { "windowDurationHours", 0 } }).ok);
		assert(!ApplyAdminRaidSettings(harness.context, Json{ { "windowDurationHours", 2.5 } }).ok);
		assert(!ApplyAdminRaidSettings(harness.context, Json{ { "mode", "sometimes" } }).ok);
		assert(!ApplyAdminRaidSettings(harness.context, Json{ { "mode", true } }).ok);
		assert(harness.host.WorkshopRaidConfig().mode == before.mode);
		assert(harness.host.WorkshopRaidConfig().windowDurationHours == before.windowDurationHours);
		assert(!std::filesystem::exists(file));

		const auto applied = ApplyAdminRaidSettings(harness.context, Json{ { "mode", "off" }, { "windowDurationHours", 6 } });
		assert(applied.ok);
		assert(applied.data.at("mode") == "off");
		assert(harness.host.WorkshopRaidConfig().mode == F4T::WorkshopRaids::RaidMode::Off);
		assert(harness.host.WorkshopRaidConfig().windowDurationHours == 6.0F);
		assert(harness.host.WorkshopRaidConfig().windowStartHour == before.windowStartHour);
		assert(!std::filesystem::exists(file.string() + ".tmp"));
		const auto stored = F4T::ServerSettings::ReadDocument(file);
		assert(stored.at("mode") == "off");
		assert(stored.at("windowDurationHours") == 6);

		const auto fileBefore = F4T::ServerSettings::ReadDocument(file);
		auto outOfRange = harness.host.WorkshopRaidConfig();
		outOfRange.windowDurationHours = 0.5F;
		harness.host.SetWorkshopRaidConfig(outOfRange);
		assert(!ApplyAdminRaidSettings(harness.context, Json{ { "mode", "open" } }).ok);
		assert(F4T::ServerSettings::ReadDocument(file) == fileBefore);
		assert(harness.host.WorkshopRaidConfig().mode == F4T::WorkshopRaids::RaidMode::Off);
		outOfRange.windowDurationHours = 6.0F;
		harness.host.SetWorkshopRaidConfig(outOfRange);

		F4T::WorkshopRaids::RaidConfig loaded;
		std::string error;
		assert(LoadRaidSettingsFile(file, loaded, error));
		assert(loaded.mode == F4T::WorkshopRaids::RaidMode::Off);
		assert(loaded.windowDurationHours == 6.0F);
		assert(LoadRaidSettingsFile(directory / "missing.json", loaded, error));
		assert(loaded.mode == F4T::WorkshopRaids::RaidMode::Off);

		std::ofstream(directory / "bad-hour.json") << R"({ "windowStartHour": 9.5 })";
		assert(!LoadRaidSettingsFile(directory / "bad-hour.json", loaded, error));
		assert(error.find("windowStartHour") != std::string::npos);
		std::ofstream(directory / "broken.json") << "{";
		assert(!LoadRaidSettingsFile(directory / "broken.json", loaded, error));
		assert(error.find("could not be read") != std::string::npos);
	}

	void TestPersistenceBeforeFactions()
	{
		const auto directory = TempDirectory("persistence-then-factions");
		Harness harness{ directory, false };
		harness.store.SaveFactions(StoredFactionDocument());
		harness.gate.SetSavingEnabled(false);
		assert(StoredFactionSurvives(harness));

		const auto factionsFirst = harness.Apply(Json{ { "enable_factions", true } });
		assert(factionsFirst.ok);
		assert(Harness::HasApplied(factionsFirst, "enableFactions"));
		assert(StoredFactionSurvives(harness));

		const auto persistenceThen = harness.Apply(Json{ { "enable_persistence", true } });
		assert(persistenceThen.ok);
		assert(Harness::HasApplied(persistenceThen, "enablePersistence"));
		assert(StoredFactionSurvives(harness));
		const auto capabilities = harness.host.ServerInfoPacket().at("capabilities").dump();
		assert(capabilities.find("factions-v1") != std::string::npos);
		std::filesystem::remove_all(directory);
	}

	void TestPersistenceFlushBeforeOff()
	{
		const auto directory = TempDirectory("flush-before-off");
		Harness harness{ directory, true };
		harness.store.SaveFactions(StoredFactionDocument());
		assert(StoredFactionSurvives(harness));
		assert(harness.Apply(Json{ { "enable_factions", true } }).ok);

		const auto off = harness.Apply(Json{ { "enable_persistence", false } });
		assert(off.ok);
		assert(Harness::HasApplied(off, "enablePersistence"));
		assert(!harness.gate.SavingEnabled());
		assert(StoredFactionSurvives(harness));
		std::filesystem::remove_all(directory);
	}

	void TestValidationRefusals()
	{
		const auto directory = TempDirectory("refusals");
		Harness harness{ directory, true };
		const auto original = harness.options.serverName;

		{
			const auto refused = ApplyServerLiveSettings(harness.context, Document(Json{ { "max_players", 0 } }));
			assert(!refused.ok);
			assert(!Harness::HasApplied(refused, "serverName"));
			assert(harness.options.serverName == original);
		}
		{
			const auto refused = ApplyServerLiveSettings(harness.context, Document(Json{ { "world_mode", "nonsense" } }));
			assert(!refused.ok);
			assert(refused.message.find("nonsense") != std::string::npos);
		}
		{
			const auto refused = ApplyServerLiveSettings(harness.context, Document(Json{ { "world_mode", "free-roam" } }));
			assert(!refused.ok);
			assert(refused.message.find("free-roam") != std::string::npos);
			assert(harness.worldMode == F4T::WorldPolicy::Mode::CoopStory);
		}
		{
			const auto refused = ApplyServerLiveSettings(harness.context, Document(Json{ { "public_listing", true }, { "directory_url", "" } }));
			assert(!refused.ok);
			assert(refused.message.find("directory") != std::string::npos);
		}
		assert(harness.options.serverName == original);
		std::filesystem::remove_all(directory);
	}

	void TestLiveFieldApply()
	{
		const auto directory = TempDirectory("fields");
		Harness harness{ directory, true };
		const auto applied = harness.Apply(Json{
			{ "server_name", "Renamed Live" },
			{ "server_description", "Renamed description" },
			{ "max_players", 6 },
			{ "friendly_fire", false },
			{ "faction_friendly_fire", true },
			{ "survival_mode", false },
			{ "survival_needs", false },
			{ "downed_seconds", 90 },
			{ "enable_chat", false },
			{ "enable_object_state", false },
			{ "resume_token_lifetime_hours", 12 }
		});
		assert(applied.ok);
		for (const auto* field : { "serverName", "serverDescription", "maxPlayers", "friendlyFire", "factionFriendlyFire",
				 "survivalMode", "survivalNeeds", "downedSeconds", "enableChat", "enableObjectState", "resumeTokenLifetimeHours" }) {
			assert(Harness::HasApplied(applied, field));
		}
		const auto info = harness.host.ServerInfoPacket();
		assert(info.at("serverName") == "Renamed Live");
		assert(info.at("maxPlayers") == 6);
		assert(info.at("friendlyFire") == false);
		assert(info.at("factionFriendlyFire") == true);
		assert(info.at("survivalMode") == false);
		assert(info.at("survivalNeeds") == false);
		assert(info.at("capabilities").dump().find("chat-v1") == std::string::npos);
		std::filesystem::remove_all(directory);
	}

	void TestPartialApplyWithoutRefusal()
	{
		const auto directory = TempDirectory("partial");
		Harness harness{ directory, false };
		const auto partial = harness.Apply(Json{ { "server_name", "Partial Apply" }, { "enable_persistence", true } }, false);
		assert(partial.ok);
		assert(Harness::HasApplied(partial, "serverName"));
		assert(Harness::HasFieldError(partial, "enablePersistence"));
		assert(harness.options.serverName == "Partial Apply");
		std::filesystem::remove_all(directory);
	}

	void TestDirectoryListingUpdatesInPlace()
	{
		const auto directory = TempDirectory("directory");
		Harness harness{ directory, true };
		auto registrar = std::make_unique<DirectoryRegistrar>(
			"https://directory.example",
			DirectoryListing{ .name = "Live Test Server", .description = "Live test description", .port = 7777, .maximumPlayers = 8 },
			harness.activePlayers,
			[](std::string_view, const std::string&, const std::string&, const std::string&) {
				return DirectoryHttpResponse{ .status = 503, .transportError = "offline in tests" };
			});
		const auto* before = registrar.get();
		harness.directoryRegistrar = std::move(registrar);

		const auto applied = harness.Apply(Json{ { "server_name", "Relisted Name" } });
		assert(applied.ok);
		assert(Harness::HasApplied(applied, "serverName"));
		assert(harness.directoryRegistrar.get() == before);
		assert(harness.directoryRegistrar->ListingSnapshot().name == "Relisted Name");

		const auto regional = harness.Apply(Json{ { "region", "asia" } });
		assert(regional.ok);
		assert(Harness::HasApplied(regional, "region"));
		assert(harness.options.region == "asia");
		assert(harness.directoryRegistrar->ListingSnapshot().region == "Asia");
		std::filesystem::remove_all(directory);
	}

	void TestDirectoryToggle()
	{
		const auto directory = TempDirectory("directory-toggle");
		Harness harness{ directory, true };
		harness.directoryRegistrar = std::make_unique<DirectoryRegistrar>(
			"https://preconfigured.example",
			DirectoryListing{ .name = "Live Test Server", .port = 7777, .maximumPlayers = 8 },
			harness.activePlayers,
			[](std::string_view, const std::string&, const std::string&, const std::string&) {
				return DirectoryHttpResponse{ .status = 503, .transportError = "offline in tests" };
			});
		harness.options.publicListing = true;

		const auto disabled = harness.Apply(Json{ { "public_listing", false } });
		assert(disabled.ok);
		assert(Harness::HasApplied(disabled, "publicListing"));
		assert(!harness.directoryRegistrar);
		assert(!harness.options.publicListing);

		const auto enabled = harness.Apply(Json{ { "public_listing", true }, { "directory_url", "https://directory.example" } });
		assert(enabled.ok);
		assert(Harness::HasApplied(enabled, "publicListing"));
		assert(harness.directoryRegistrar);
		assert(harness.directoryRegistrar->ListingSnapshot().maximumPlayers == harness.options.maximumPlayers);
		assert(harness.directoryRegistrar->ListingSnapshot().name == harness.options.serverName);
		std::filesystem::remove_all(directory);
	}

	void TestLockedP2PHostNeverLists()
	{
		const auto directory = TempDirectory("p2p-lock");
		Harness harness{ directory, true };
		harness.options.publicListing = false;
		harness.options.publicListingLocked = true;

		const auto locked = harness.Apply(Json{
			{ "public_listing", true },
			{ "directory_url", "https://directory.example" },
			{ "server_name", "Locked P2P Host" }
		});
		assert(locked.ok);
		assert(!harness.directoryRegistrar);
		assert(!harness.options.publicListing);
		assert(!Harness::HasApplied(locked, "publicListing"));
		assert(!Harness::HasFieldError(locked, "publicListing"));
		assert(Harness::HasApplied(locked, "serverName"));
		assert(harness.options.serverName == "Locked P2P Host");
		std::filesystem::remove_all(directory);
	}

	void TestLanDiscoveryToggle()
	{
		const auto directory = TempDirectory("lan");
		Harness harness{ directory, true };
		harness.options.enableLanDiscovery = true;
		const auto disabled = harness.Apply(Json{ { "lan_discovery_enabled", false } });
		assert(disabled.ok);
		assert(Harness::HasApplied(disabled, "lanDiscoveryEnabled"));
		assert(!harness.discovery.Running());

		const auto enabled = harness.Apply(Json{ { "lan_discovery_enabled", true } });
		if (Harness::HasFieldError(enabled, "lanDiscoveryEnabled")) {
			assert(!harness.discovery.Running());
		} else {
			assert(Harness::HasApplied(enabled, "lanDiscoveryEnabled"));
			assert(harness.discovery.Running());
			harness.discovery.Stop();
		}
		std::filesystem::remove_all(directory);
	}

	class StubTokens final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override { return "live-apply-token"; }
		F4T::Identity::TokenHash HashToken(std::string_view) override { return F4T::Identity::TokenHash{}; }
	};

	void TestWorldModeStaysConsistentAfterLiveApply()
	{
		const auto directory = TempDirectory("world-mode");
		Harness harness{ directory, false };
		harness.worldPolicyData.classes[static_cast<std::size_t>(F4T::WorldPolicy::ClassId::Story)].push_back({});
		const auto applied = harness.Apply(Json{ { "worldMode", "free-roam" } });
		assert(applied.ok);
		assert(harness.worldMode == F4T::WorldPolicy::Mode::FreeRoam);
		assert(harness.options.worldMode == "free-roam");
		assert(CurrentDocument(harness.options).at("worldMode") == "free-roam");
		std::filesystem::remove_all(directory);
	}

	void TestSteamAdmissionPolicyFollowsLivePassword()
	{
		const auto directory = TempDirectory("steam-password");
		Harness harness{ directory, false };
		StubTokens tokens;
		F4T::Identity::Requirements requirements;
		IdentityGate identity(requirements, tokens, nullptr, {}, nullptr, {}, SteamIdentityHooks{});
		SteamIdentityLink link;
		harness.context.identityGate = &identity;
		harness.context.steamLink = &link;
		assert(!link.Policy().serverPasswordSet);

		assert(harness.Apply(Json{ { "server_password", "hunter2" } }).ok);
		assert(link.Policy().serverPasswordSet);
		assert(harness.options.serverPassword.has_value());

		assert(harness.Apply(Json{ { "server_password", "hunter3" } }).ok);
		assert(link.Policy().serverPasswordSet);

		const auto cleared = ApplyServerLiveSettings(harness.context, [&] {
			Json document = CurrentDocument(harness.options);
			document.erase("server_password");
			document.erase("serverPassword");
			return document;
		}());
		assert(cleared.ok);
		assert(!link.Policy().serverPasswordSet);
		std::filesystem::remove_all(directory);
	}

	void TestDownedSecondsFlatAndNestedBothApply()
	{
		const auto directory = TempDirectory("downed");
		for (const bool nested : { false, true }) {
			Json base = Document(Json::object());
			if (nested) base["downed"] = Json{ { "seconds", 60 }, { "revive_channel_seconds", 3 } };
			auto options = CommonwealthOnline::Server::Configuration::Parse(base, directory);
			assert(options.downedSeconds == 60);
			auto values = F4T::ServerSettings::ReadEditable(options);
			values.downedSeconds = 120;
			const auto written = F4T::ServerSettings::ApplyEditable(base, values);
			const auto reloaded = CommonwealthOnline::Server::Configuration::Parse(written, directory);
			assert(reloaded.downedSeconds == 120);
			if (nested) assert(written.at("downed").at("seconds") == 120);
		}
		std::filesystem::remove_all(directory);
	}

	void TestFactionsDisableThenEnable()
	{
		const auto directory = TempDirectory("factions");
		Harness harness{ directory, true };
		const auto enable = harness.Apply(Json{ { "enable_factions", true } });
		assert(enable.ok);
		assert(Harness::HasApplied(enable, "enableFactions"));
		assert(harness.host.ServerInfoPacket().at("capabilities").dump().find("factions-v1") != std::string::npos);

		const auto disable = harness.Apply(Json{ { "enable_factions", false } });
		assert(disable.ok);
		assert(harness.host.ServerInfoPacket().at("capabilities").dump().find("factions-v1") == std::string::npos);

		const auto reEnable = harness.Apply(Json{ { "enable_factions", true } });
		assert(reEnable.ok);
		assert(harness.host.ServerInfoPacket().at("capabilities").dump().find("factions-v1") != std::string::npos);
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestRaidSettingsApplyAndLoad();
	TestPersistenceBeforeFactions();
	TestPersistenceFlushBeforeOff();
	TestValidationRefusals();
	TestLiveFieldApply();
	TestPartialApplyWithoutRefusal();
	TestDirectoryListingUpdatesInPlace();
	TestDirectoryToggle();
	TestLockedP2PHostNeverLists();
	TestLanDiscoveryToggle();
	TestFactionsDisableThenEnable();
	TestWorldModeStaysConsistentAfterLiveApply();
	TestDownedSecondsFlatAndNestedBothApply();
	TestSteamAdmissionPolicyFollowsLivePassword();
	std::cout << "Server live apply tests passed" << std::endl;
	return 0;
}
