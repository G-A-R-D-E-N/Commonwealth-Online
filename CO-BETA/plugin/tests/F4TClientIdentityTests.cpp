#include "F4TClientIdentity.h"
#include "F4TClientCompatibility.h"
#include "F4TContentManifest.h"
#include "F4TServerProfileStore.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <string>

namespace
{
	constexpr std::string_view kValidFingerprint =
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	constexpr std::string_view kServerPassword = "correct-horse";

	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-client-identity-tests-{}", stamp);
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		assert(input.is_open());
		return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	void WriteFile(const std::filesystem::path& a_path, const std::string& a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		assert(output.is_open());
		output << a_text;
		assert(output.good());
	}
}

int main()
{
	using F4T::ContentManifest::IsValidFingerprint;

	assert(IsValidFingerprint(kValidFingerprint));
	assert(!IsValidFingerprint(""));
	assert(!IsValidFingerprint(std::string(63, 'a')));
	assert(!IsValidFingerprint(std::string(65, 'a')));
	assert(!IsValidFingerprint(std::string(64, 'A')));
	assert(!IsValidFingerprint(std::string(64, 'F')));
	assert(!IsValidFingerprint("0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
	assert(!IsValidFingerprint(std::string(64, 'g')));
	assert(!IsValidFingerprint(std::string(64, 'z')));
	assert(!IsValidFingerprint(std::string(64, ' ')));

	const auto scratch = MakeScratchDirectory();
	std::filesystem::create_directories(scratch);

	F4T::ClientIdentity::ResetManifestPathForTesting();
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());
	std::string prepareError;
	assert(!F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError));
	assert(prepareError == "Packaged client identity manifest is missing or invalid.");

	const auto nonExistent = scratch / "does-not-exist.json";
	F4T::ClientIdentity::SetManifestPathForTesting(nonExistent);
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	const auto manifestPath = scratch / "CommonwealthOnlineClient.json";
	F4T::ClientIdentity::SetManifestPathForTesting(manifestPath);

	WriteFile(manifestPath, "{ invalid json }");
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	WriteFile(manifestPath, "[\"not\", \"an\", \"object\"]");
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	WriteFile(manifestPath, "42");
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json missingVersion = {
		{ "contentManifestFingerprint", std::string{ kValidFingerprint } }
	};
	WriteFile(manifestPath, missingVersion.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json nonStringVersion = {
		{ "clientVersion", 1 },
		{ "contentManifestFingerprint", std::string{ kValidFingerprint } }
	};
	WriteFile(manifestPath, nonStringVersion.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json mismatchVersion = {
		{ "clientVersion", "999.0.0" },
		{ "contentManifestFingerprint", std::string{ kValidFingerprint } }
	};
	WriteFile(manifestPath, mismatchVersion.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json missingFingerprint = {
		{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } }
	};
	WriteFile(manifestPath, missingFingerprint.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json nonStringFingerprint = {
		{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
		{ "contentManifestFingerprint", true }
	};
	WriteFile(manifestPath, nonStringFingerprint.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json upperFingerprint = {
		{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
		{ "contentManifestFingerprint", std::string(64, 'A') }
	};
	WriteFile(manifestPath, upperFingerprint.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json shortFingerprint = {
		{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
		{ "contentManifestFingerprint", std::string(63, 'a') }
	};
	WriteFile(manifestPath, shortFingerprint.dump());
	assert(!F4T::ClientIdentity::LoadPackagedMetadata());

	nlohmann::json validManifest = {
		{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
		{ "contentManifestFingerprint", std::string{ kValidFingerprint } }
	};
	WriteFile(manifestPath, validManifest.dump());
	const auto loaded = F4T::ClientIdentity::LoadPackagedMetadata();
	assert(loaded.has_value());
	assert(loaded->clientVersion == F4T::ClientCompatibility::kClientVersion);
	assert(loaded->contentManifestFingerprint == kValidFingerprint);
	assert(loaded->buildTarget == F4T::ClientCompatibility::BuildTarget::OG);
	assert(loaded->falloutRuntimeVersion == F4T::ClientCompatibility::kOgRuntimeVersion);

	const auto profilesPath = scratch / "server-profiles.json";
	F4T::ServerProfileStore::SetProfilesPathForTesting(profilesPath);
	const std::optional<std::string> password{ std::string{ kServerPassword } };
	const auto prepared = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError, password);
	assert(prepared.has_value() && prepareError.empty());
	assert(prepared->serverPassword == password);
	assert(std::filesystem::exists(profilesPath));
	assert(ReadText(profilesPath).find(kServerPassword) == std::string::npos);

	const auto unpassworded = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError);
	assert(unpassworded.has_value() && !unpassworded->serverPassword);

	const auto maximumPassword = F4T::ClientIdentity::PrepareConnectionIdentity(
		"127.0.0.1", 7777, prepareError, std::optional<std::string>{ std::string(128, 'a') });
	assert(maximumPassword.has_value());
	assert(!F4T::ClientIdentity::PrepareConnectionIdentity(
		"127.0.0.1", 7777, prepareError, std::optional<std::string>{ std::string(129, 'a') }));
	assert(prepareError == "Server password must be 128 characters or fewer.");

	F4T::ClientIdentity::ConnectionIdentity testIdentity{
		.serverKey = "127.0.0.1:7777",
		.profileId = "test-profile",
		.resumeToken = std::optional<std::string>{ "test-token" },
		.metadata = loaded,
		.serverPassword = password
	};
	F4T::ClientIdentity::HoldConnectionIdentity(testIdentity);
	const auto taken = F4T::ClientIdentity::TakeConnectionIdentity();
	assert(taken.has_value());
	assert(taken->serverKey == "127.0.0.1:7777");
	assert(taken->profileId == "test-profile");
	assert(taken->resumeToken.has_value() && *taken->resumeToken == "test-token");
	assert(taken->serverPassword == password);
	assert(!F4T::ClientIdentity::TakeConnectionIdentity().has_value());

	F4T::ClientIdentity::HoldConnectionIdentity(testIdentity);
	F4T::ClientIdentity::ClearConnectionIdentity();
	assert(!F4T::ClientIdentity::TakeConnectionIdentity().has_value());

	F4T::ClientIdentity::ResetManifestPathForTesting();
	std::error_code error;
	std::filesystem::remove_all(scratch, error);
	return 0;
}
