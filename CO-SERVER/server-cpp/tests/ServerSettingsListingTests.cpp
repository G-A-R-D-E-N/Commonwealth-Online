#include "F4TServerSettingsFile.h"

#include "F4TServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
	using Json = nlohmann::json;

	std::filesystem::path Scratch()
	{
		auto directory = std::filesystem::temp_directory_path() / "co-server-settings-listing-tests";
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
			.serverName = "Locked P2P Host",
			.serverDescription = "Survival co-op",
			.maximumPlayers = 8,
			.worldMode = "coop-story",
			.vats = "real-time",
			.downedSeconds = 45,
			.enablePersistence = false,
			.enableLanDiscovery = false,
			.publicListing = false,
			.resumeTokenLifetimeHours = 24
		};
	}

	F4T::ServerSettings::SaveOutcome LockedSave(
		const std::filesystem::path& a_path,
		const F4T::ServerSettings::Editable& a_values,
		int& a_liveCalls)
	{
		F4T::ServerSettings::DocumentAdjuster adjust;
		std::string error;
		assert(F4T::ServerSettings::RawPublicListingRestorer(a_path, adjust, error));
		return F4T::ServerSettings::SaveSettings(
			a_path,
			a_values,
			[&a_liveCalls](const Json&, Json&, std::string&) {
				++a_liveCalls;
				return F4T::ServerSettings::LiveApplyResult::Applied;
			},
			nullptr,
			adjust);
	}
}

int main()
{
	using F4T::ServerSettings::DocumentAdjuster;
	using F4T::ServerSettings::RawPublicListingRestorer;
	using F4T::ServerSettings::ReadDocument;

	const auto directory = Scratch();

	{
		const auto configPath = directory / "restore-canonical.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": false })");
		DocumentAdjuster adjust;
		std::string error;
		assert(RawPublicListingRestorer(configPath, adjust, error));
		assert(error.empty());
		assert(adjust);
		Json document{ { "public_listing", true }, { "publicListing", true } };
		adjust(document);
		assert(document.at("public_listing") == false);
		assert(!document.contains("publicListing"));
	}

	{
		const auto configPath = directory / "restore-alias.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "publicListing": false })");
		DocumentAdjuster adjust;
		std::string error;
		assert(RawPublicListingRestorer(configPath, adjust, error));
		assert(adjust);
		Json document{ { "public_listing", true }, { "publicListing", true } };
		adjust(document);
		assert(document.at("publicListing") == false);
		assert(!document.contains("public_listing"));
	}

	{
		const auto configPath = directory / "restore-absent.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host" })");
		DocumentAdjuster adjust;
		std::string error;
		assert(RawPublicListingRestorer(configPath, adjust, error));
		assert(adjust);
		Json document{ { "public_listing", true }, { "publicListing", true } };
		adjust(document);
		assert(!document.contains("public_listing"));
		assert(!document.contains("publicListing"));
	}

	{
		const auto missingPath = directory / "restore-missing.json";
		DocumentAdjuster adjust;
		std::string error;
		assert(RawPublicListingRestorer(missingPath, adjust, error));
		assert(error.empty());
		assert(adjust);
		Json missingDocument{ { "public_listing", true }, { "publicListing", true }, { "server_name", "Locked P2P Host" } };
		adjust(missingDocument);
		assert(!missingDocument.contains("public_listing"));
		assert(!missingDocument.contains("publicListing"));
		assert(missingDocument.at("server_name") == "Locked P2P Host");
	}

	{
		const auto configPath = directory / "restore-malformed.json";
		WriteText(configPath, "{ not json");
		DocumentAdjuster adjust;
		std::string error;
		assert(!RawPublicListingRestorer(configPath, adjust, error));
		assert(!adjust);
		assert(!error.empty());
		assert(error.find("Nothing was changed, so the file on disk was left as it was.") != std::string::npos);
	}

	{
		const auto unreadablePath = directory / "restore-unreadable.json";
		std::filesystem::create_directories(unreadablePath);
		DocumentAdjuster adjust;
		std::string error;
		assert(!RawPublicListingRestorer(unreadablePath, adjust, error));
		assert(!adjust);
		assert(!error.empty());
	}

	{
		F4T::ServerConfiguration::Options options;
		options.serverName = "Good Name";
		options.directoryUrl = "http://example.com/functions/v1/server-directory";
		assert(F4T::ServerConfiguration::ListedTextSaveErrors(options).empty());

		options.serverName = "   ";
		const auto blank = F4T::ServerConfiguration::ListedTextSaveErrors(options);
		assert(blank.size() == 1);
		assert(blank.front().find("server_name") != std::string::npos);

		options.serverName = "Good Name";
		options.serverDescription = "Bad\nDescription";
		const auto description = F4T::ServerConfiguration::ListedTextSaveErrors(options);
		assert(description.size() == 1);
		assert(description.front().find("server_description") != std::string::npos);

		options.serverDescription.clear();
		options.serverName = "   ";
		options.publicListing = false;
		assert(F4T::ServerConfiguration::ListedTextSaveErrors(options).empty());
	}

	{
		const auto configPath = directory / "listed-blank-name.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": true })");
		const auto before = ReadText(configPath);
		auto values = SampleValues();
		values.serverName = "   ";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(!outcome.saved);
		assert(!outcome.liveApplied);
		assert(liveCalls == 0);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("server_name") != std::string::npos);
		assert(outcome.errors.front().find("Nothing was changed, so the file on disk was left as it was.") != std::string::npos);
		assert(ReadText(configPath) == before);
		assert(!std::filesystem::exists(directory / "listed-blank-name.json.tmp"));
	}

	{
		const auto configPath = directory / "listed-control-name.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": true })");
		const auto before = ReadText(configPath);
		auto values = SampleValues();
		values.serverName = "Bad\tName";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(!outcome.saved);
		assert(liveCalls == 0);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("server_name") != std::string::npos);
		assert(ReadText(configPath) == before);
	}

	{
		const auto configPath = directory / "listed-control-description.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": true })");
		const auto before = ReadText(configPath);
		auto values = SampleValues();
		values.serverDescription = "Bad\tDescription";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(!outcome.saved);
		assert(liveCalls == 0);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("server_description") != std::string::npos);
		assert(ReadText(configPath) == before);
	}

	{
		const auto configPath = directory / "listed-key-absent.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host" })");
		const auto before = ReadText(configPath);
		auto values = SampleValues();
		values.serverName = "   ";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(!outcome.saved);
		assert(liveCalls == 0);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("server_name") != std::string::npos);
		assert(ReadText(configPath) == before);
	}

	{
		const auto configPath = directory / "listed-bad-boolean.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": "maybe" })");
		const auto before = ReadText(configPath);
		auto values = SampleValues();
		values.serverName = "Renamed P2P Host";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(!outcome.saved);
		assert(!outcome.liveApplied);
		assert(liveCalls == 0);
		assert(!outcome.errors.empty());
		assert(outcome.errors.front().find("Nothing was changed, so the file on disk was left as it was.") != std::string::npos);
		assert(ReadText(configPath) == before);
	}

	{
		const auto configPath = directory / "listed-normal-alias.json";
		WriteText(configPath, R"({ "serverName": "Old Alias Name", "publicListing": false, "maxPlayers": 8 })");
		auto values = SampleValues();
		values.serverName = "Renamed P2P Host";
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(outcome.saved);
		assert(outcome.liveApplied);
		assert(liveCalls == 1);
		const auto document = ReadDocument(configPath);
		assert(document.at("server_name") == "Renamed P2P Host");
		assert(document.at("publicListing") == false);
		assert(!document.contains("public_listing"));
		assert(!document.contains("serverName"));
	}

	{
		const auto configPath = directory / "listed-normal-canonical.json";
		WriteText(configPath, R"({ "server_name": "Original Name", "public_listing": true })");
		auto values = SampleValues();
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, values, liveCalls);
		assert(outcome.saved);
		assert(liveCalls == 1);
		const auto document = ReadDocument(configPath);
		assert(document.at("public_listing") == true);
		assert(!document.contains("publicListing"));
		assert(document.at("server_name") == "Locked P2P Host");
	}

	{
		const auto configPath = directory / "listed-directory-url.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": true, "directory_url": "http://example.com/functions/v1/server-directory" })");
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, SampleValues(), liveCalls);
		assert(outcome.saved);
		assert(liveCalls == 1);
		const auto document = ReadDocument(configPath);
		assert(document.at("public_listing") == true);
		assert(document.at("directory_url") == "http://example.com/functions/v1/server-directory");
		bool directoryRefused = false;
		for (const auto& error : F4T::ServerConfiguration::Validate(F4T::ServerConfiguration::Parse(document, directory))) {
			if (error.find("directory_url must be an https:// URL") != std::string::npos) {
				directoryRefused = true;
			}
		}
		assert(directoryRefused);
	}

	{
		const auto configPath = directory / "listed-empty-directory-url.json";
		WriteText(configPath, R"({ "server_name": "Locked P2P Host", "public_listing": true, "directory_url": "" })");
		int liveCalls = 0;
		const auto outcome = LockedSave(configPath, SampleValues(), liveCalls);
		assert(outcome.saved);
		assert(liveCalls == 1);
		assert(ReadDocument(configPath).at("public_listing") == true);
	}

	std::filesystem::remove_all(directory);
	return 0;
}
