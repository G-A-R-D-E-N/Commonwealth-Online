#include "F4TServerProfileStore.h"
#include "F4TResumeTokenProtection.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace
{
	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-profile-store-tests-{}", stamp);
	}

	void WriteText(const std::filesystem::path& a_path, const std::string& a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		assert(output.is_open());
		output << a_text;
		assert(output.good());
	}

#ifdef _WIN32
	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		assert(input.is_open());
		return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}
#endif

	void WriteCredential(const std::filesystem::path& a_path, const std::string& a_serverKey, const std::string& a_expiry)
	{
		WriteText(
			a_path,
			std::string{"{\"servers\":{\""} + a_serverKey +
				"\":{\"resumeToken\":\"token\",\"resumeExpiresAt\":" + a_expiry + "}}}");
	}
}

int main()
{
	using namespace F4T::ServerProfileStore;

	const auto scratch = MakeScratchDirectory();
	const auto profilesDirectory = scratch / "profiles";
	const auto profilesPath = profilesDirectory / "server-profiles.json";
	const auto serverKey = MakeServerKey("LOCALHOST", 7777);
	std::filesystem::create_directories(scratch);

	assert(ProfileEndpoint("127.0.0.1", 50123, "steam-76561198000000001") == std::make_pair(std::string("steam:76561198000000001"), std::uint16_t(7777)));
	assert(ProfileEndpoint("203.0.113.5", 7777, "steam-76561198000000001") == std::make_pair(std::string("203.0.113.5"), std::uint16_t(7777)));
	assert(ProfileEndpoint("127.0.0.1", 50123, "steam-host") == std::make_pair(std::string("127.0.0.1"), std::uint16_t(50123)));

	{
		const std::string guestJoinId = "steam-76561198000000002";
		const std::string lookalikeHost = "steam-76561198000000002";
		const std::string lookalikeKey = MakeServerKey(lookalikeHost, 7777);
		const std::string guestKey = ProfileServerKey("127.0.0.1", 50123, guestJoinId);
		assert(lookalikeKey == "steam-76561198000000002:7777");
		assert(guestKey == "steam:76561198000000002:7777");
		assert(ProfileServerKey(lookalikeHost, 7777, guestJoinId) == lookalikeKey);

		const auto lookalikePath = scratch / "lookalike" / "server-profiles.json";
		const std::string lookalikeSave = "Save30_AAAA1111_Nomad_Commonwealth_20260814001010_1_2";
		const std::string guestSave = "Save31_AAAA1111_Nomad_Commonwealth_20260814001111_1_2";
		SetProfilesPathForTesting(lookalikePath);
		assert(BindSaveFile(lookalikeKey, lookalikeSave));
		assert(!LookupSaveFile(guestKey));
		assert(BindSaveFile(guestKey, guestSave));
		assert(LookupSaveFile(lookalikeKey) == lookalikeSave);
		assert(LookupSaveFile(guestKey) == guestSave);
		SetProfilesPathForTesting(profilesPath);
	}

	const auto blockedParent = scratch / "blocked-parent";
	WriteText(blockedParent, "blocked");
	SetProfilesPathForTesting(blockedParent / "server-profiles.json");
	assert(EnsureProfileId(serverKey).empty());
	assert(!LookupProfileId(serverKey));

	SetProfilesPathForTesting(profilesPath);
	std::vector<std::string> profileIds(8);
	std::vector<std::thread> workers;
	for (auto& profileId : profileIds) {
		workers.emplace_back([&profileId, &serverKey]() {
			profileId = EnsureProfileId(serverKey);
		});
	}
	for (auto& worker : workers) {
		worker.join();
	}
	assert(!profileIds.front().empty());
	for (const auto& profileId : profileIds) {
		assert(profileId == profileIds.front());
	}
	assert(LookupProfileId(serverKey) == profileIds.front());

	const auto gameplaySave = "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2";
	assert(BindSaveFile(serverKey, gameplaySave));
	assert(LookupSaveFile(serverKey) == gameplaySave);

	const std::string worldA(32, 'a');
	const std::string worldB(32, 'B');
	const auto worldASave = "Save11_BBBB2222_Nomad_Commonwealth_20260814000303_1_2";
	SetConnectedWorldId(worldA);
	assert(LookupSaveFile(serverKey) == gameplaySave);
	assert(BindSaveFile(serverKey, worldASave));
	assert(LookupSaveFile(serverKey) == worldASave);
	SetConnectedWorldId(worldB);
	assert(!LookupSaveFile(serverKey));
	assert(!LookupCharacterId(serverKey));
	SetConnectedWorldId(worldA);
	assert(LookupSaveFile(serverKey) == worldASave);
	SetConnectedWorldId("not-a-server-instance-id");
	assert(LookupSaveFile(serverKey) == worldASave);
	SetConnectedWorldId({});
	assert(BindSaveFile(serverKey, gameplaySave));
	SetConnectedWorldId(worldA);
	assert(LookupSaveFile(serverKey) == worldASave);
	SetConnectedWorldId({});

	{
		const auto guestKey = ProfileServerKey("127.0.0.1", 50123, "steam-76561198000000003");
		const auto guestWorldSave = "Save12_CCCC3333_Nomad_Commonwealth_20260814000404_1_2";
		SetConnectedWorldId(worldA);
		assert(!LookupSaveFile(guestKey));
		assert(BindSaveFile(guestKey, guestWorldSave));
		assert(LookupSaveFile(guestKey) == guestWorldSave);
		assert(LookupSaveFile(serverKey) == worldASave);
		SetConnectedWorldId(worldB);
		assert(!LookupSaveFile(guestKey));
		SetConnectedWorldId({});
	}

	const auto bindFailureDirectory = scratch / "bind-failure";
	WriteText(bindFailureDirectory, "blocked");
	SetProfilesPathForTesting(bindFailureDirectory / "server-profiles.json");
	assert(!BindSaveFile(serverKey, "Save10_AAAA1111_Nomad_Commonwealth_20260814000202_1_2"));
	SetProfilesPathForTesting(profilesPath);
	assert(LookupSaveFile(serverKey) == gameplaySave);

	{
		const auto rotationSave = "Save11_AAAA1111_Nomad_Commonwealth_20260814000303_1_2";
		assert(BindSaveFile(serverKey, rotationSave));
		assert(LookupSaveFile(serverKey) == rotationSave);
		const auto backupPath = profilesPath.string() + ".bak";
		assert(std::filesystem::exists(backupPath));

		std::filesystem::remove(backupPath);
		std::filesystem::create_directory(backupPath);
		WriteText(std::filesystem::path(backupPath) / "occupied.txt", "not a profile store backup");

		const auto blockedSave = "Save12_AAAA1111_Nomad_Commonwealth_20260814000404_1_2";
		assert(!BindSaveFile(serverKey, blockedSave));
		assert(LookupSaveFile(serverKey) == rotationSave);
		assert(std::filesystem::is_directory(backupPath));
		assert(!std::filesystem::exists(profilesPath.string() + ".tmp"));

		SetProfilesPathForTesting(profilesPath);
		assert(LookupSaveFile(serverKey) == rotationSave);

		std::filesystem::remove_all(backupPath);
	}

	{
		const auto preFaultSave = "Save13_AAAA1111_Nomad_Commonwealth_20260814000505_1_2";
		assert(BindSaveFile(serverKey, preFaultSave));
		assert(LookupSaveFile(serverKey) == preFaultSave);

		const auto backupPath = profilesPath.string() + ".bak";
		std::filesystem::remove(backupPath);
		assert(!std::filesystem::exists(backupPath));

		SetPostRotationFaultForTesting([profilesPath]() {
			std::filesystem::create_directory(profilesPath);
			WriteText(profilesPath / "occupied.txt", "not a profile store");
		});

		const auto rejectedSave = "Save14_AAAA1111_Nomad_Commonwealth_20260814000606_1_2";
		assert(!BindSaveFile(serverKey, rejectedSave));

		assert(std::filesystem::exists(backupPath));
		assert(!std::filesystem::is_directory(backupPath));
		{
			nlohmann::json backupContents;
			std::ifstream backupInput(backupPath);
			assert(backupInput.is_open());
			backupInput >> backupContents;
			const auto backupSaveFile = backupContents["servers"][serverKey]["saveFile"].get<std::string>();
			assert(backupSaveFile == preFaultSave);
		}
		assert(!std::filesystem::exists(profilesPath.string() + ".tmp"));

		assert(LookupSaveFile(serverKey) == preFaultSave);

		std::filesystem::remove_all(profilesPath);
		SetProfilesPathForTesting(profilesPath);
		const auto recovered = LookupSaveFile(serverKey);
		assert(recovered);
		assert(*recovered == preFaultSave);

		assert(BindSaveFile(serverKey, preFaultSave));
		std::filesystem::remove(backupPath);
	}

#ifdef _WIN32
	const auto movedProfilesDirectory = scratch / "profiles-moved";
	if (!F4T::ResumeTokenProtection::Protect("probe")) {
		std::fprintf(stderr, "DPAPI CryptProtectData failed, GetLastError=0x%08X\n", F4T::ResumeTokenProtection::LastError());
	}
	assert(StoreResumeCredential(serverKey, "old-token", 100.0));
	SetProfilesPathForTesting(profilesPath);
	const auto loadedOld = LookupResumeCredential(serverKey);
	assert(loadedOld);
	assert(loadedOld->token == "old-token");
	std::filesystem::rename(profilesDirectory, movedProfilesDirectory);
	WriteText(profilesDirectory, "blocked");
	assert(!StoreResumeCredential(serverKey, "failed-token", 200.0));
	const auto inMemory = LookupResumeCredential(serverKey);
	assert(inMemory);
	assert(inMemory->token == "old-token");
	assert(inMemory->expiresAt == 100.0);
	std::filesystem::remove(profilesDirectory);
	std::filesystem::rename(movedProfilesDirectory, profilesDirectory);
	SetProfilesPathForTesting(profilesPath);
	const auto onDisk = LookupResumeCredential(serverKey);
	assert(onDisk);
	assert(onDisk->token == "old-token");
	assert(onDisk->expiresAt == 100.0);

	assert(StoreResumeCredential(serverKey, "rotated-token", 123.5));
	SetProfilesPathForTesting(profilesPath);
	const auto rotated = LookupResumeCredential(serverKey);
	assert(rotated);
	assert(rotated->token == "rotated-token");
	assert(rotated->expiresAt == 123.5);
#else
	assert(!StoreResumeCredential(serverKey, "unprotected-token", 700.0));
	assert(!LookupResumeCredential(serverKey));
#endif
	std::filesystem::remove(profilesPath.string() + ".bak");

	for (const auto& expiry : { "0", "-1", "NaN", "Infinity" }) {
		WriteCredential(profilesPath, serverKey, expiry);
		SetProfilesPathForTesting(profilesPath);
		assert(!LookupResumeCredential(serverKey));
	}

	nlohmann::json missingExpiry = {
		{ "servers", { { serverKey, { { "resumeToken", "token" } } } } }
	};
	WriteText(profilesPath, missingExpiry.dump());
	SetProfilesPathForTesting(profilesPath);
	assert(!LookupResumeCredential(serverKey));

	nlohmann::json emptyToken = {
		{ "servers", { { serverKey, { { "resumeToken", "" }, { "resumeExpiresAt", 123 } } } } }
	};
	WriteText(profilesPath, emptyToken.dump());
	SetProfilesPathForTesting(profilesPath);
	assert(!LookupResumeCredential(serverKey));

	nlohmann::json booleanExpiry = {
		{ "servers", { { serverKey, { { "resumeToken", "token" }, { "resumeExpiresAt", true } } } } }
	};
	WriteText(profilesPath, booleanExpiry.dump());
	SetProfilesPathForTesting(profilesPath);
	assert(!LookupResumeCredential(serverKey));

	nlohmann::json stringExpiry = {
		{ "servers", { { serverKey, { { "resumeToken", "token" }, { "resumeExpiresAt", "123" } } } } }
	};
	WriteText(profilesPath, stringExpiry.dump());
	SetProfilesPathForTesting(profilesPath);
	assert(!LookupResumeCredential(serverKey));

#ifdef _WIN32
	const auto otherServerKey = MakeServerKey("OTHERHOST", 8888);
	assert(StoreResumeCredential(serverKey, "server-a-token", 500.0));
	assert(StoreResumeCredential(otherServerKey, "server-b-token", 600.0));
	SetProfilesPathForTesting(profilesPath);
	assert(LookupResumeCredential(serverKey));
	assert(LookupResumeCredential(serverKey)->token == "server-a-token");
	assert(LookupResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey)->token == "server-b-token");

	assert(ClearResumeCredential(serverKey));
	assert(!LookupResumeCredential(serverKey));
	assert(LookupResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey)->token == "server-b-token");

	SetProfilesPathForTesting(profilesPath);
	assert(!LookupResumeCredential(serverKey));
	assert(LookupResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey)->token == "server-b-token");

	assert(!ClearResumeCredential(serverKey));
	assert(!ClearResumeCredential(""));
	assert(!ClearResumeCredential("non-existent-server"));

	const auto blockDir = scratch / "clear-block";
	std::filesystem::rename(profilesDirectory, blockDir);
	WriteText(profilesDirectory, "blocked");
	assert(!ClearResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey)->token == "server-b-token");
	std::filesystem::remove(profilesDirectory);
	std::filesystem::rename(blockDir, profilesDirectory);
	SetProfilesPathForTesting(profilesPath);
	assert(LookupResumeCredential(otherServerKey));
	assert(LookupResumeCredential(otherServerKey)->token == "server-b-token");
	assert(ClearResumeCredential(otherServerKey));
	assert(!LookupResumeCredential(otherServerKey));
#endif

#ifdef _WIN32
	const auto protectedPath = scratch / "protected" / "server-profiles.json";
	const std::string roundTripToken = "fake-resume-token-round-trip";
	SetProfilesPathForTesting(protectedPath);
	assert(StoreResumeCredential(serverKey, roundTripToken, 300.0));
	const auto protectedText = ReadText(protectedPath);
	assert(protectedText.find(roundTripToken) == std::string::npos);
	assert(protectedText.find("\"resumeTokenProtected\"") != std::string::npos);
	SetProfilesPathForTesting(protectedPath);
	const auto roundTrip = LookupResumeCredential(serverKey);
	assert(roundTrip);
	assert(roundTrip->token == roundTripToken);
	assert(roundTrip->expiresAt == 300.0);

	const auto legacyPath = scratch / "legacy" / "server-profiles.json";
	std::filesystem::create_directories(legacyPath.parent_path());
	const std::string legacyToken = "fake-legacy-plaintext-token";
	nlohmann::json legacy = {
		{ "servers", { { serverKey, { { "resumeToken", legacyToken }, { "resumeExpiresAt", 400.0 }, { "saveFile", "Save20_AAAA1111_Nomad_Commonwealth_20260814000707_1_2" } } } } }
	};
	WriteText(legacyPath, legacy.dump());
	SetProfilesPathForTesting(legacyPath);
	const auto migrated = LookupResumeCredential(serverKey);
	assert(migrated);
	assert(migrated->token == legacyToken);
	assert(migrated->expiresAt == 400.0);
	const auto migratedText = ReadText(legacyPath);
	assert(migratedText.find(legacyToken) == std::string::npos);
	assert(migratedText.find("\"resumeToken\"") == std::string::npos);
	assert(migratedText.find("\"resumeTokenProtected\"") != std::string::npos);
	const auto legacyBackupText = ReadText(legacyPath.string() + ".bak");
	assert(legacyBackupText.find(legacyToken) == std::string::npos);
	assert(legacyBackupText.find("\"resumeToken\"") == std::string::npos);
	SetProfilesPathForTesting(legacyPath);
	assert(LookupResumeCredential(serverKey));
	assert(LookupResumeCredential(serverKey)->token == legacyToken);
	assert(LookupSaveFile(serverKey) == "Save20_AAAA1111_Nomad_Commonwealth_20260814000707_1_2");

	const auto backupOnlyPath = scratch / "backup-only" / "server-profiles.json";
	std::filesystem::create_directories(backupOnlyPath.parent_path());
	const std::string backupToken = "fake-legacy-backup-only-token";
	nlohmann::json backupOnly = {
		{ "servers", { { serverKey, { { "resumeToken", backupToken }, { "resumeExpiresAt", 450.0 }, { "saveFile", "Save22_AAAA1111_Nomad_Commonwealth_20260814000909_1_2" } } } } }
	};
	WriteText(backupOnlyPath.string() + ".bak", backupOnly.dump());
	SetProfilesPathForTesting(backupOnlyPath);
	const auto recoveredLegacy = LookupResumeCredential(serverKey);
	assert(recoveredLegacy);
	assert(recoveredLegacy->token == backupToken);
	assert(ReadText(backupOnlyPath).find(backupToken) == std::string::npos);
	assert(ReadText(backupOnlyPath.string() + ".bak").find(backupToken) == std::string::npos);
	SetProfilesPathForTesting(backupOnlyPath);
	assert(LookupResumeCredential(serverKey));
	assert(LookupResumeCredential(serverKey)->token == backupToken);
	assert(LookupSaveFile(serverKey) == "Save22_AAAA1111_Nomad_Commonwealth_20260814000909_1_2");

	const auto bothPath = scratch / "both" / "server-profiles.json";
	std::filesystem::create_directories(bothPath.parent_path());
	const std::string bothToken = "fake-legacy-wins-token";
	nlohmann::json both = {
		{ "servers", { { serverKey, { { "resumeToken", bothToken }, { "resumeTokenProtected", "ZmFrZS1zdGFsZQ==" }, { "resumeExpiresAt", 600.0 } } } } }
	};
	WriteText(bothPath, both.dump());
	SetProfilesPathForTesting(bothPath);
	const auto bothLoaded = LookupResumeCredential(serverKey);
	assert(bothLoaded);
	assert(bothLoaded->token == bothToken);
	assert(bothLoaded->expiresAt == 600.0);
	const auto bothText = ReadText(bothPath);
	assert(bothText.find("ZmFrZS1zdGFsZQ==") == std::string::npos);
	assert(bothText.find(bothToken) == std::string::npos);
#endif

	const auto corruptPath = scratch / "corrupt" / "server-profiles.json";
	std::filesystem::create_directories(corruptPath.parent_path());
	for (const auto& blob : { "not-base64!!", "AAAA" }) {
		nlohmann::json corrupt = {
			{ "servers", { { serverKey, { { "resumeTokenProtected", blob }, { "resumeExpiresAt", 500.0 }, { "saveFile", "Save21_AAAA1111_Nomad_Commonwealth_20260814000808_1_2" } } } } }
		};
		WriteText(corruptPath, corrupt.dump());
		SetProfilesPathForTesting(corruptPath);
		assert(!LookupResumeCredential(serverKey));
		assert(LookupSaveFile(serverKey) == "Save21_AAAA1111_Nomad_Commonwealth_20260814000808_1_2");
	}

	std::error_code error;
	std::filesystem::remove_all(scratch, error);
	return 0;
}
