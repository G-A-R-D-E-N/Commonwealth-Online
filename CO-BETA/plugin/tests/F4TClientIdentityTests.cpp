#include "F4TClientIdentity.h"
#include "F4TClientCompatibility.h"
#include "F4TContentManifest.h"
#include "F4TServerProfileStore.h"
#include "F4TSteamHostProof.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <string>

namespace F4T::TradeWindow { bool Attached() { return false; } }

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
	assert(std::ranges::find(loaded->capabilities, std::string{ "survival-needs-v1" }) != loaded->capabilities.end());

	const auto profilesPath = scratch / "server-profiles.json";
	F4T::ServerProfileStore::SetProfilesPathForTesting(profilesPath);
	const std::optional<std::string> password{ std::string{ kServerPassword } };
	const auto prepared = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError, password);
	assert(prepared.has_value() && prepareError.empty());
	assert(prepared->serverPassword == password);
	assert(std::filesystem::exists(profilesPath));
	assert(ReadText(profilesPath).find(kServerPassword) == std::string::npos);
	F4T::GnsProtocolAdapter::ReadyIdentity ready{ .profileId = prepared->profileId, .resumeToken = "test-token", .resumeExpiresAt = 1000.0 };
	assert(F4T::ClientIdentity::CommitReadyIdentity(prepared->serverKey, ready, prepareError));
	assert(F4T::ClientIdentity::ConnectedServerKey() == prepared->serverKey);
	assert(F4T::ClientIdentity::ConnectedProfileId() == prepared->profileId);
	F4T::ClientIdentity::ClearConnectedProfileId();
	assert(F4T::ClientIdentity::ConnectedServerKey() == prepared->serverKey);
	F4T::ClientIdentity::ClearConnectionIdentity();
	assert(F4T::ClientIdentity::ConnectedServerKey().empty());
	assert(F4T::ClientIdentity::ConnectedProfileId().empty());
	assert(!F4T::ClientIdentity::IsConnectedServerAdmin());
	ready.multiplayerSaveAdmin = true;
	assert(F4T::ClientIdentity::CommitReadyIdentity(prepared->serverKey, ready, prepareError));
	assert(F4T::ClientIdentity::IsConnectedServerAdmin());
	ready.resumeToken.clear();
	assert(!F4T::ClientIdentity::CommitReadyIdentity(prepared->serverKey, ready, prepareError));
	assert(F4T::ClientIdentity::ConnectedServerKey().empty());
	assert(F4T::ClientIdentity::ConnectedProfileId().empty());
	assert(!F4T::ClientIdentity::IsConnectedServerAdmin());

	const auto beforeDrop = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError, password);
	assert(beforeDrop.has_value() && beforeDrop->resumeToken.has_value() && *beforeDrop->resumeToken == "test-token");
	assert(F4T::ServerProfileStore::ClearResumeCredential(prepared->serverKey));
	const auto afterDrop = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError, password);
	assert(afterDrop.has_value() && !afterDrop->resumeToken);
	assert(afterDrop->profileId == beforeDrop->profileId);
	assert(afterDrop->serverKey == beforeDrop->serverKey);
	assert(afterDrop->serverPassword == password);

	const auto unpassworded = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError);
	assert(unpassworded.has_value() && !unpassworded->serverPassword);

	F4T::Steam::HostProof().Set(7777, std::optional<std::string>{ std::string(64, 'c') });
	const auto hosted = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7777, prepareError);
	assert(hosted && hosted->hostProof == std::optional<std::string>{ std::string(64, 'c') });
	const auto otherPort = F4T::ClientIdentity::PrepareConnectionIdentity("127.0.0.1", 7778, prepareError);
	assert(otherPort && !otherPort->hostProof);
	const auto remoteHost = F4T::ClientIdentity::PrepareConnectionIdentity("192.168.1.10", 7777, prepareError);
	assert(remoteHost && !remoteHost->hostProof);
	F4T::Steam::HostProof().Clear();

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
