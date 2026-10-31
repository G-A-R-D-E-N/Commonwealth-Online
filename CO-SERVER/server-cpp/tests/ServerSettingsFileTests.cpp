#include "F4TServerSettingsFile.h"

#include "F4TWorldPolicy.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;

	std::filesystem::path Scratch()
	{
		auto directory = std::filesystem::temp_directory_path() / "co-server-settings-file-tests";
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory;
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
	}

	void WriteText(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		output << a_text;
	}

	F4T::ServerSettings::Editable SampleValues()
	{
		return F4T::ServerSettings::Editable{
			.serverName = "Nomad's Game",
			.serverDescription = "Survival co-op",
		.maximumPlayers = 8,
		.worldMode = "free-roam",
		.vats = "real-time",
		.doorsShared = false,
			.friendlyFire = false,
			.factionFriendlyFire = true,
			.downedSeconds = 45,
			.enableChat = false,
			.enableFactions = false,
			.enablePersistence = true,
			.enableObjectState = false,
			.enableLanDiscovery = false,
			.publicListing = true,
			.resumeTokenLifetimeHours = 240
		};
	}

	F4T::ServerSettings::SaveOutcome WriteFixture(const std::filesystem::path& a_path, const F4T::ServerSettings::Editable& a_values)
	{
		return F4T::ServerSettings::CommitSave(a_path, F4T::ServerSettings::PrepareSave(a_path, a_values));
	}
}

int main()
{
	using F4T::ServerConfiguration::Options;
	using F4T::ServerSettings::Editable;
	using F4T::ServerSettings::PasswordUpdate;

	const auto directory = Scratch();
	const auto configPath = directory / "commonwealth-server.json";

	{
		const auto outcome = WriteFixture(configPath, SampleValues());
		assert(outcome.saved);
		assert(outcome.errors.empty());
		assert(std::filesystem::exists(configPath));
		assert(!std::filesystem::exists(directory / "commonwealth-server.json.tmp"));

		const auto document = F4T::ServerSettings::ReadDocument(configPath);
		const auto options = Options{};
		const auto parsed = F4T::ServerConfiguration::Parse(document, directory);
		assert(F4T::ServerConfiguration::Validate(parsed).empty());
		const auto editable = F4T::ServerSettings::ReadEditable(parsed);
		const auto expected = SampleValues();
		assert(editable.serverName == expected.serverName);
		assert(editable.serverDescription == expected.serverDescription);
		assert(editable.maximumPlayers == expected.maximumPlayers);
		assert(editable.worldMode == expected.worldMode);
		assert(editable.vats == expected.vats);
		assert(editable.doorsShared == expected.doorsShared);
		assert(editable.friendlyFire == expected.friendlyFire);
		assert(editable.factionFriendlyFire == expected.factionFriendlyFire);
		assert(editable.downedSeconds == expected.downedSeconds);
		assert(editable.enableChat == expected.enableChat);
		assert(editable.enableFactions == expected.enableFactions);
		assert(editable.enablePersistence == expected.enablePersistence);
		assert(editable.enableObjectState == expected.enableObjectState);
		assert(editable.enableLanDiscovery == expected.enableLanDiscovery);
		assert(editable.publicListing == expected.publicListing);
		assert(editable.resumeTokenLifetimeHours == expected.resumeTokenLifetimeHours);
		assert(!parsed.serverPassword.has_value());
		assert(parsed.enableLanDiscovery == false);
		assert(parsed.port == 7777);
		assert(parsed.adminPort == 7779);
		assert(parsed.worldPolicyPath == "world-policy.json");
		assert(parsed.startingInventory == std::nullopt);
	}

	{
		const auto before = ReadText(configPath);
		const auto unchanged = F4T::ServerSettings::PrepareSave(configPath, SampleValues());
		assert(unchanged.Ready());
		assert(unchanged.worldMode == "free-roam");
		assert(unchanged.previousWorldMode == "free-roam");
		assert(!unchanged.ChangesWorldMode());
		assert(unchanged.worldPolicyPath == directory / "world-policy.json");
		assert(ReadText(configPath) == before);

		auto switched = SampleValues();
		switched.worldMode = "coop-story";
		const auto plan = F4T::ServerSettings::PrepareSave(configPath, switched);
		assert(plan.Ready());
		assert(plan.worldMode == "coop-story");
		assert(plan.previousWorldMode == "free-roam");
		assert(plan.ChangesWorldMode());
		assert(ReadText(configPath) == before);

		auto refused = SampleValues();
		refused.downedSeconds = 1;
		const auto rejected = F4T::ServerSettings::PrepareSave(configPath, refused);
		assert(!rejected.Ready());
		assert(!rejected.ChangesWorldMode());
		const auto refusedCommit = F4T::ServerSettings::CommitSave(configPath, rejected);
		assert(!refusedCommit.saved);
		assert(!refusedCommit.errors.empty());
		assert(ReadText(configPath) == before);

		const auto written = F4T::ServerSettings::CommitSave(configPath, plan);
		assert(written.saved);
		assert(written.errors.empty());
		assert(F4T::ServerSettings::ReadDocument(configPath).at("worldMode") == "coop-story");
		assert(!std::filesystem::exists(directory / "commonwealth-server.json.tmp"));
	}

	{
		const auto freshPath = directory / "fresh-server.json";
		const auto plan = F4T::ServerSettings::PrepareSave(freshPath, SampleValues());
		assert(plan.Ready());
		assert(plan.previousWorldMode.empty());
		assert(plan.ChangesWorldMode());
		assert(!std::filesystem::exists(freshPath));
		assert(F4T::ServerSettings::CommitSave(freshPath, plan).saved);
		assert(F4T::ServerSettings::ReadDocument(freshPath).at("worldMode") == "free-roam");
	}

	{
		const auto spellingPath = directory / "spelling-server.json";
		WriteText(spellingPath, R"({ "host": "127.0.0.1", "server_name": "Spelling", "max_players": 4, "worldMode": "Free-Roam", "world_policy_path": "policy/world.json" })");
		const auto plan = F4T::ServerSettings::PrepareSave(spellingPath, SampleValues());
		assert(plan.Ready());
		assert(plan.worldMode == "free-roam");
		assert(plan.previousWorldMode == "free-roam");
		assert(!plan.ChangesWorldMode());
		assert(plan.worldPolicyPath == directory / "policy" / "world.json");
	}

	{
		const auto legacyPath = directory / "legacy-server.json";
		WriteText(legacyPath, R"({ "host": "127.0.0.1", "server_name": "Legacy", "max_players": 4, "questMode": "coop-story" })");
		const auto plan = F4T::ServerSettings::PrepareSave(legacyPath, SampleValues());
		assert(plan.Ready());
		assert(plan.previousWorldMode == "coop-story");
		assert(plan.worldMode == "free-roam");
		assert(plan.ChangesWorldMode());
		assert(F4T::ServerSettings::ReadDocument(legacyPath).at("questMode") == "coop-story");
	}

	{
		WriteText(configPath, R"({
  "host": "127.0.0.1",
  "port": 7801,
  "admin_port": 7802,
  "serverName": "Old Alias",
  "maxPlayers": 4,
  "enableChat": true,
  "world_mode": "coop-story",
  "quest_mode": "coop-story",
  "admin_profiles": [ "co-admin-0001" ],
  "required_capabilities": [ "interest-v1" ],
  "starting_inventory": [ { "plugin": "Test.esm", "localFormId": "123", "count": 5 } ],
  "custom_note": { "kept": true },
  "custom_list": [ 1, 2, 3 ]
})");
		const auto before = ReadText(configPath);
		const auto outcome = WriteFixture(configPath, SampleValues());
		assert(outcome.saved);
		const auto after = ReadText(configPath);
		assert(after != before);

		const auto document = F4T::ServerSettings::ReadDocument(configPath);
		assert(document.at("host") == "127.0.0.1");
		assert(document.at("port") == 7801);
		assert(document.at("admin_port") == 7802);
		assert(document.at("admin_profiles") == Json::array({ "co-admin-0001" }));
		assert(document.at("required_capabilities") == Json::array({ "interest-v1" }));
		assert(document.at("starting_inventory").size() == 1);
		assert(document.at("custom_note").at("kept") == true);
		assert(document.at("custom_list") == Json::array({ 1, 2, 3 }));
		assert(!document.contains("serverName"));
		assert(!document.contains("maxPlayers"));
		assert(!document.contains("enableChat"));
		assert(!document.contains("world_mode"));
		assert(!document.contains("quest_mode"));
		assert(document.at("server_name") == "Nomad's Game");
		assert(document.at("max_players") == 8);
		assert(document.at("enable_chat") == false);
		assert(document.at("worldMode") == "free-roam");
		assert(document.at("vats") == "real-time");

		const auto parsed = F4T::ServerConfiguration::Parse(document, directory);
		assert(F4T::ServerConfiguration::Validate(parsed).empty());
		assert(parsed.bindHost == "127.0.0.1");
		assert(parsed.port == 7801);
		assert(parsed.adminPort == 7802);
		assert(parsed.adminProfiles.size() == 1);
		assert(parsed.requiredCapabilities.size() == 1);
		assert(parsed.startingInventory.has_value());
	}

	{
		const auto before = ReadText(configPath);
		auto invalid = SampleValues();
		invalid.downedSeconds = 1;
		const auto outcome = WriteFixture(configPath, invalid);
		assert(!outcome.saved);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("downed_seconds") != std::string::npos);
		assert(ReadText(configPath) == before);
		assert(!std::filesystem::exists(directory / "commonwealth-server.json.tmp"));

		auto noName = SampleValues();
		noName.serverName.clear();
		const auto nameOutcome = WriteFixture(configPath, noName);
		assert(!nameOutcome.saved);
		assert(ReadText(configPath) == before);

		auto badVats = SampleValues();
		badVats.vats = "slow-mo";
		const auto vatsOutcome = WriteFixture(configPath, badVats);
		assert(!vatsOutcome.saved);
		assert(!vatsOutcome.errors.empty());
		assert(vatsOutcome.errors.front().find("vats") != std::string::npos);
		assert(ReadText(configPath) == before);

		auto aliasedVats = SampleValues();
		aliasedVats.vats = "Real-Time";
		const auto vatsPath = directory / "vats-alias-server.json";
		const auto vatsPlan = F4T::ServerSettings::PrepareSave(vatsPath, aliasedVats);
		assert(vatsPlan.Ready());
		const auto vatsCommitted = F4T::ServerSettings::CommitSave(vatsPath, vatsPlan);
		assert(vatsCommitted.saved);
		assert(F4T::ServerSettings::ReadEditable(
			F4T::ServerConfiguration::Parse(F4T::ServerSettings::ReadDocument(vatsPath), directory)).vats == "real-time");

		auto tooManyPlayers = SampleValues();
		tooManyPlayers.maximumPlayers = 17;
		const auto playersOutcome = WriteFixture(configPath, tooManyPlayers);
		assert(!playersOutcome.saved);
		assert(ReadText(configPath) == before);

		auto badWorldMode = SampleValues();
		badWorldMode.worldMode = "sandbox";
		const auto worldOutcome = WriteFixture(configPath, badWorldMode);
		assert(!worldOutcome.saved);
		assert(ReadText(configPath) == before);

		auto shortPassword = SampleValues();
		shortPassword.passwordUpdate = PasswordUpdate::Set;
		shortPassword.serverPassword = std::string{ "abc" };
		const auto passwordOutcome = WriteFixture(configPath, shortPassword);
		assert(!passwordOutcome.saved);
		assert(passwordOutcome.errors.front().find("server_password") != std::string::npos);
		assert(ReadText(configPath) == before);
	}

	{
		WriteText(configPath, "{ not json");
		const auto before = ReadText(configPath);
		const auto outcome = WriteFixture(configPath, SampleValues());
		assert(!outcome.saved);
		assert(!outcome.errors.empty());
		assert(ReadText(configPath) == before);
		WriteText(configPath, "[]");
		const auto arrayOutcome = WriteFixture(configPath, SampleValues());
		assert(!arrayOutcome.saved);
		assert(ReadText(configPath) == "[]");
	}

	{
		WriteText(configPath, R"({ "server_password": "kept-pass", "serverPassword": "kept-pass" })");
		auto values = SampleValues();
		values.passwordUpdate = PasswordUpdate::Keep;
		assert(WriteFixture(configPath, values).saved);
		auto document = F4T::ServerSettings::ReadDocument(configPath);
		assert(document.at("server_password") == "kept-pass");
		assert(F4T::ServerConfiguration::Parse(document, directory).serverPassword == std::optional<std::string>{ "kept-pass" });

		values.passwordUpdate = PasswordUpdate::Set;
		values.serverPassword = std::string{ "new-pass" };
		assert(WriteFixture(configPath, values).saved);
		document = F4T::ServerSettings::ReadDocument(configPath);
		assert(document.at("server_password") == "new-pass");
		assert(!document.contains("serverPassword"));
		assert(F4T::ServerConfiguration::Parse(document, directory).serverPassword == std::optional<std::string>{ "new-pass" });

		values.passwordUpdate = PasswordUpdate::Clear;
		assert(WriteFixture(configPath, values).saved);
		document = F4T::ServerSettings::ReadDocument(configPath);
		assert(!document.contains("server_password"));
		assert(!document.contains("serverPassword"));
		assert(!F4T::ServerConfiguration::Parse(document, directory).serverPassword.has_value());
	}

	{
		WriteText(configPath, R"({ "host": "not-an-ip", "port": 7777, "admin_port": 7779 })");
		const auto before = ReadText(configPath);
		const auto outcome = WriteFixture(configPath, SampleValues());
		assert(!outcome.saved);
		bool hostReported = false;
		for (const auto& error : outcome.errors) {
			if (error.find("host must be an explicit IPv4 or IPv6 bind address") != std::string::npos) {
				hostReported = true;
			}
		}
		assert(hostReported);
		assert(ReadText(configPath) == before);
	}

	{
		Json payload = F4T::ServerSettings::ToPayload(SampleValues(), false);
		assert(payload.size() == 17);
		payload["passwordConfigured"] = true;

		F4T::ServerSettings::Editable decoded;
		std::string payloadError;
		assert(F4T::ServerSettings::FromPayload(payload, decoded, payloadError));
		assert(payloadError.empty());

		const auto payloadPath = directory / "payload-server.json";
		const auto plan = F4T::ServerSettings::PrepareSave(payloadPath, decoded);
		assert(plan.Ready());
		assert(F4T::ServerSettings::CommitSave(payloadPath, plan).saved);

		const auto options = F4T::ServerConfiguration::Parse(F4T::ServerSettings::ReadDocument(payloadPath), directory);
		assert(F4T::ServerConfiguration::Validate(options).empty());
		assert(F4T::ServerSettings::ToPayload(F4T::ServerSettings::ReadEditable(options), true) == payload);
	}

	{
		Json payload = F4T::ServerSettings::ToPayload(SampleValues(), true);
		payload["passwordClear"] = true;
		payload["unknownSetting"] = Json::array();
		F4T::ServerSettings::Editable decoded;
		std::string error;
		assert(F4T::ServerSettings::FromPayload(payload, decoded, error));
		assert(error.empty());
		assert(decoded.serverName == SampleValues().serverName);

		auto missing = payload;
		missing.erase("downedSeconds");
		assert(!F4T::ServerSettings::FromPayload(missing, decoded, error));
		assert(error.find("downedSeconds") != std::string::npos);

		auto quoted = payload;
		quoted["maxPlayers"] = "8";
		assert(!F4T::ServerSettings::FromPayload(quoted, decoded, error));
		assert(error.find("maxPlayers") != std::string::npos);

		auto nulled = payload;
		nulled["friendlyFire"] = nullptr;
		assert(!F4T::ServerSettings::FromPayload(nulled, decoded, error));
		assert(error.find("friendlyFire") != std::string::npos);

		auto negative = payload;
		negative["downedSeconds"] = -1;
		assert(!F4T::ServerSettings::FromPayload(negative, decoded, error));
		assert(error.find("out of range") != std::string::npos);

		auto fractional = payload;
		fractional["resumeTokenLifetimeHours"] = 12.5;
		assert(!F4T::ServerSettings::FromPayload(fractional, decoded, error));

		assert(!F4T::ServerSettings::FromPayload(Json::array(), decoded, error));
	}

	{
		const auto variantPath = directory / "variant-server.json";
		WriteText(variantPath, R"({ "host": "127.0.0.1", "port": 7777, "admin_port": 7779, "server_name": "Variant", "max_players": 4, "questMode": "Free-Roam" })");
		const auto options = F4T::ServerConfiguration::Parse(F4T::ServerSettings::ReadDocument(variantPath), directory);
		assert(F4T::ServerConfiguration::Validate(options).empty());
		assert(options.worldMode == "Free-Roam");
		const auto payload = F4T::ServerSettings::ToPayload(F4T::ServerSettings::ReadEditable(options), false);
		assert(payload.at("worldMode") == "free-roam");

		F4T::ServerSettings::Editable sent;
		std::string payloadError;
		assert(F4T::ServerSettings::FromPayload(payload, sent, payloadError));
		const auto plan = F4T::ServerSettings::PrepareSave(variantPath, sent);
		assert(plan.Ready());
		assert(!plan.ChangesWorldMode());
	}

	{
		const auto samplePolicy = directory / "judge-sample.json";
		WriteText(samplePolicy, R"({ "version": 1, "game": "Fallout4.esm", "sample": true, "classes": { "story": [], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");
		const auto generatedPolicy = directory / "judge-generated.json";
		WriteText(generatedPolicy, R"({ "version": 3, "game": "Fallout4.esm", "classes": { "story": ["01CA6E:Fallout4.esm"], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");

		const auto judge = [&](std::string_view a_mode, std::string_view a_policy) {
			const auto document = Json{
				{ "serverName", "Nomad's Game" },
				{ "worldMode", a_mode },
				{ "worldPolicyPath", a_policy }
			};
			return F4T::ServerSettings::JudgeWorldMode(F4T::ServerConfiguration::Parse(document, directory), directory);
		};

		const auto refused = judge("free-roam", "judge-sample.json");
		assert(refused.mode == "free-roam");
		assert(refused.policyPath == samplePolicy);
		assert(!refused.Servable());
		assert(refused.refusal == F4T::WorldPolicy::UnservableModeMessage(
			samplePolicy.string(), F4T::WorldPolicy::Mode::FreeRoam));

		const auto servable = judge("free-roam", "judge-generated.json");
		assert(servable.Servable());
		assert(servable.policyPath == generatedPolicy);
		assert(judge("coop-story", "judge-sample.json").Servable());
		assert(!judge("free-roam", "judge-missing.json").Servable());

		const auto absoluteDocument = Json{
			{ "serverName", "Nomad's Game" },
			{ "worldMode", "free-roam" },
			{ "worldPolicyPath", generatedPolicy.string() }
		};
		assert(F4T::ServerSettings::JudgeWorldMode(
			F4T::ServerConfiguration::Parse(absoluteDocument, directory), directory).Servable());

		const auto planDirectory = directory / "judge-plan";
		std::filesystem::create_directories(planDirectory);
		const auto plan = F4T::ServerSettings::PrepareSave(planDirectory / "commonwealth-server.json", SampleValues());
		assert(plan.Ready());
		assert(plan.worldMode == "free-roam");
		assert(plan.worldPolicyPath == planDirectory / "world-policy.json");
		assert(!plan.worldModeRefusal.empty());
		assert(plan.worldModeRefusal == F4T::WorldPolicy::UnservableModeMessage(
			plan.worldPolicyPath.string(), F4T::WorldPolicy::Mode::FreeRoam));
	}

	{
		const auto blockedDirectory = directory / "unservable";
		std::filesystem::create_directories(blockedDirectory);
		WriteText(blockedDirectory / "sample.json", R"({ "version": 1, "game": "Fallout4.esm", "sample": true, "classes": { "story": [], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");
		const auto blockedConfig = blockedDirectory / "commonwealth-server.json";
		WriteText(blockedConfig, R"({ "serverName": "Nomad's Game", "maxPlayers": 8, "worldMode": "free-roam", "worldPolicyPath": "sample.json", "downedSeconds": 45, "resumeTokenLifetimeHours": 24 })");

		const auto before = ReadText(blockedConfig);
		auto renamed = SampleValues();
		renamed.serverName = "Renamed";
		bool askedLive = false;
		const auto refused = F4T::ServerSettings::SaveSettings(blockedConfig, renamed, [&](const nlohmann::json&, nlohmann::json&, std::string&) {
			askedLive = true;
			return F4T::ServerSettings::LiveApplyResult::Applied;
		});
		assert(!refused.saved);
		assert(!askedLive);
		assert(ReadText(blockedConfig) == before);
		assert(!refused.errors.empty());
		assert(refused.errors.front().find("Nothing was changed") != std::string::npos);
		assert(refused.errors.front().find("free-roam") != std::string::npos);

		renamed.worldMode = "coop-story";
		const auto repaired = F4T::ServerSettings::SaveSettings(blockedConfig, renamed, [](const nlohmann::json&, nlohmann::json&, std::string&) {
			return F4T::ServerSettings::LiveApplyResult::Unavailable;
		});
		assert(repaired.saved);
		const auto repairedDocument = F4T::ServerSettings::ReadDocument(blockedConfig);
		assert(repairedDocument.at("worldMode") == "coop-story");
		assert(repairedDocument.at("server_name") == "Renamed");
	}

	{
		const auto policyPath = directory / "world-policy.json";
		WriteText(policyPath, R"({ "version": 1, "game": "Fallout4.esm", "classes": { "story": ["01CA6E:Fallout4.esm"], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");

		const auto savePath = directory / "sequence-server.json";
		auto values = SampleValues();
		assert(WriteFixture(savePath, values).saved);
		assert(F4T::ServerSettings::ReadDocument(savePath).at("worldMode") == "free-roam");

		{
			const auto noPolicy = directory / "no-policy";
			std::filesystem::create_directories(noPolicy);
			int liveCalls = 0;
			const auto outcome = F4T::ServerSettings::SaveSettings(
				noPolicy / "commonwealth-server.json",
				values,
				[&](const nlohmann::json&, nlohmann::json&, std::string&) {
					++liveCalls;
					return F4T::ServerSettings::LiveApplyResult::Applied;
				});
			assert(!outcome.saved);
			assert(liveCalls == 0);
			assert(!std::filesystem::exists(noPolicy / "commonwealth-server.json"));
			assert(!outcome.errors.empty());
			assert(outcome.errors.front().find("Nothing was changed") != std::string::npos);
		}

		{
			values.worldMode = "coop-story";
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, [](const nlohmann::json&, nlohmann::json&, std::string&) {
				return F4T::ServerSettings::LiveApplyResult::Unavailable;
			});
			assert(outcome.saved);
			assert(!outcome.liveApplied);
			assert(F4T::ServerSettings::ReadDocument(savePath).at("worldMode") == "coop-story");
		}

		{
			values.worldMode = "free-roam";
			std::string askedMode;
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, [&](const nlohmann::json& a_document, nlohmann::json&, std::string&) {
				askedMode = a_document.value("worldMode", std::string{});
				return F4T::ServerSettings::LiveApplyResult::Applied;
			});
			assert(outcome.saved);
			assert(outcome.liveApplied);
			assert(askedMode == "free-roam");
		}

		{
			values.worldMode = "coop-story";
			const auto before = ReadText(savePath);
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, [](const nlohmann::json&, nlohmann::json&, std::string& a_error) {
				a_error = "World mode needs a different data file.";
				return F4T::ServerSettings::LiveApplyResult::Refused;
			});
			assert(!outcome.saved);
			assert(!outcome.errors.empty());
			assert(outcome.errors.front().find("World mode needs a different data file.") != std::string::npos);
			assert(outcome.errors.front().find("still matches the running server") != std::string::npos);
			assert(ReadText(savePath) == before);
		}

		{
			const auto partialDirectory = directory / "partial-refusal";
			std::filesystem::create_directories(partialDirectory);
			WriteText(partialDirectory / "world-policy.json", R"({ "version": 1, "game": "Fallout4.esm", "classes": { "story": ["01CA6E:Fallout4.esm"], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");
			const auto partialConfig = partialDirectory / "commonwealth-server.json";
			auto previous = SampleValues();
			previous.worldMode = "coop-story";
			assert(WriteFixture(partialConfig, previous).saved);
			const auto before = ReadText(partialConfig);
			auto changed = previous;
			changed.serverName = "Renamed while running";
			changed.maximumPlayers = 4;

			std::vector<std::string> askedNames;
			const auto refusedAfterPartial = F4T::ServerSettings::SaveSettings(
				partialConfig,
				changed,
				[&](const nlohmann::json& a_document, nlohmann::json& a_result, std::string& a_error) {
					askedNames.push_back(a_document.value("server_name", std::string{}));
					if (askedNames.size() == 1) {
						a_result = nlohmann::json{ { "applied", nlohmann::json::array({ "serverName", "maxPlayers" }) } };
						a_error = "LAN discovery could not start: port busy.";
						return F4T::ServerSettings::LiveApplyResult::Refused;
					}
					return F4T::ServerSettings::LiveApplyResult::Applied;
				});
			assert(!refusedAfterPartial.saved);
			assert(askedNames.size() == 2);
			assert(askedNames[0] == "Renamed while running");
			assert(askedNames[1] == "Nomad's Game");
			assert(refusedAfterPartial.errors.size() == 1);
			assert(refusedAfterPartial.errors.front().find("LAN discovery could not start: port busy.") != std::string::npos);
			assert(refusedAfterPartial.errors.front().find("Nothing was changed") != std::string::npos);
			assert(ReadText(partialConfig) == before);

			askedNames.clear();
			const auto revertFailed = F4T::ServerSettings::SaveSettings(
				partialConfig,
				changed,
				[&](const nlohmann::json& a_document, nlohmann::json& a_result, std::string& a_error) {
					askedNames.push_back(a_document.value("server_name", std::string{}));
					if (askedNames.size() == 1) {
						a_result = nlohmann::json{ { "applied", nlohmann::json::array({ "serverName", "maxPlayers" }) } };
						a_error = "LAN discovery could not start: port busy.";
						return F4T::ServerSettings::LiveApplyResult::Refused;
					}
					a_error = "The server is busy.";
					return F4T::ServerSettings::LiveApplyResult::Refused;
				});
			assert(!revertFailed.saved);
			assert(askedNames.size() == 2);
			assert(revertFailed.errors.size() >= 2);
			assert(revertFailed.errors.front().find("LAN discovery could not start: port busy.") != std::string::npos);
			bool namedFields = false;
			bool claimsNothing = false;
			for (const auto& error : revertFailed.errors) {
				if (error.find("serverName") != std::string::npos && error.find("maxPlayers") != std::string::npos) {
					namedFields = true;
				}
				if (error.find("Nothing was changed") != std::string::npos) {
					claimsNothing = true;
				}
			}
			assert(namedFields);
			assert(!claimsNothing);
			assert(ReadText(partialConfig) == before);

			const auto freshDirectory = directory / "partial-refusal-fresh";
			std::filesystem::create_directories(freshDirectory);
			WriteText(freshDirectory / "world-policy.json", R"({ "version": 1, "game": "Fallout4.esm", "classes": { "story": ["01CA6E:Fallout4.esm"], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })");
			const auto freshConfig = freshDirectory / "commonwealth-server.json";
			std::vector<std::string> freshNames;
			const auto freshRefusal = F4T::ServerSettings::SaveSettings(
				freshConfig,
				changed,
				[&](const nlohmann::json& a_document, nlohmann::json& a_result, std::string& a_error) {
					freshNames.push_back(a_document.value("server_name", std::string{}));
					a_result = nlohmann::json{ { "applied", nlohmann::json::array({ "serverName" }) } };
					a_error = "LAN discovery could not start: port busy.";
					return F4T::ServerSettings::LiveApplyResult::Refused;
				});
			assert(!freshRefusal.saved);
			assert(freshNames.size() == 1);
			assert(freshRefusal.errors.front().find("LAN discovery could not start: port busy.") != std::string::npos);
			bool freshNamesField = false;
			bool freshClaimsNothing = false;
			for (const auto& error : freshRefusal.errors) {
				if (error.find("serverName") != std::string::npos) {
					freshNamesField = true;
				}
				if (error.find("Nothing was changed") != std::string::npos) {
					freshClaimsNothing = true;
				}
			}
			assert(freshNamesField);
			assert(!freshClaimsNothing);
			assert(!std::filesystem::exists(freshConfig));
		}

		{
			auto blockedTemporary = savePath;
			blockedTemporary += ".tmp";
			std::filesystem::create_directories(blockedTemporary);
			values.worldMode = "coop-story";
			std::vector<std::string> liveModes;
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, [&](const nlohmann::json& a_document, nlohmann::json&, std::string&) {
				liveModes.emplace_back(a_document.value("worldMode", std::string{}));
				return F4T::ServerSettings::LiveApplyResult::Applied;
			});
			std::filesystem::remove_all(blockedTemporary);
			assert(!outcome.saved);
			assert(liveModes.size() == 2);
			assert(liveModes[0] == "coop-story");
			assert(liveModes[1] == "free-roam");
			assert(F4T::ServerSettings::ReadDocument(savePath).at("worldMode") == "free-roam");
		}

		{
			auto blockedTemporary = savePath;
			blockedTemporary += ".tmp";
			std::filesystem::create_directories(blockedTemporary);
			values.worldMode = "coop-story";
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, [](const nlohmann::json& a_document, nlohmann::json&, std::string&) {
				return a_document.value("worldMode", std::string{}) == "coop-story" ? F4T::ServerSettings::LiveApplyResult::Applied :
					F4T::ServerSettings::LiveApplyResult::Refused;
			});
			std::filesystem::remove_all(blockedTemporary);
			assert(!outcome.saved);
			bool noted = false;
			for (const auto& error : outcome.errors) {
				if (error.find("keeps the settings") != std::string::npos) {
					noted = true;
				}
			}
			assert(noted);
		}

		{
			values.worldMode = "coop-story";
			const auto outcome = F4T::ServerSettings::SaveSettings(savePath, values, {});
			assert(outcome.saved);
			assert(!outcome.liveApplied);
			assert(F4T::ServerSettings::ReadDocument(savePath).at("worldMode") == "coop-story");
		}
	}

	std::filesystem::remove_all(directory);
	return 0;
}
