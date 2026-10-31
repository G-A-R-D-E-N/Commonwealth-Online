#include "F4TSaveLineageStore.h"
#include "F4TServerProfileStore.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	using F4T::SaveProfileLogic::AttestedSave;

	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-save-lineage-tests-{}", stamp);
	}

	AttestedSave MakeSave(const std::string& a_saveFile, const std::string& a_sha256Hex, std::uint64_t a_attestedAt)
	{
		return AttestedSave{ a_saveFile, a_sha256Hex, a_attestedAt };
	}

	std::string MakeHex(std::uint64_t a_value)
	{
		return std::format("{:064x}", a_value);
	}

	void WriteText(const std::filesystem::path& a_path, const std::string& a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		assert(output.is_open());
		output << a_text;
		assert(output.good());
	}
}

int main()
{
	using namespace F4T::SaveLineageStore;
	using F4T::ServerProfileStore::MakeServerKey;
	using F4T::ServerProfileStore::SetProfilesPathForTesting;

	const auto scratch = MakeScratchDirectory();
	std::filesystem::create_directories(scratch);
	const auto profilesPath = scratch / "profiles" / "server-profiles.json";
	const auto lineagePath = scratch / "profiles" / "save-lineage.json";
	const auto serverA = MakeServerKey("LOCALHOST", 7777);
	const auto serverB = MakeServerKey("LOCALHOST", 7778);
	SetProfilesPathForTesting(profilesPath);

	assert(LookupAttestedSaves(serverA).empty());
	assert(!std::filesystem::exists(lineagePath));

	const auto first = MakeSave("Save1_AAAA1111_Nomad_Commonwealth_20260814000202_1_2", MakeHex(0xaa11), 1000);
	assert(RecordAttestedSave(serverA, first));
	assert(std::filesystem::exists(lineagePath));
	{
		const auto saves = LookupAttestedSaves(serverA);
		assert(saves.size() == 1);
		assert(saves.front().saveFile == first.saveFile);
		assert(saves.front().sha256Hex == first.sha256Hex);
		assert(saves.front().attestedAtUnix == first.attestedAtUnix);
	}

	const auto otherServerSave = MakeSave("Save2_BBBB2222_Nomad_Commonwealth_20260814000303_1_2", MakeHex(0xbb22), 2000);
	assert(RecordAttestedSave(serverB, otherServerSave));
	assert(LookupAttestedSaves(serverA).size() == 1);
	assert(LookupAttestedSaves(serverB).size() == 1);
	assert(LookupAttestedSaves(serverB).front().saveFile == otherServerSave.saveFile);
	assert(LookupAttestedSaves(serverA).front().saveFile == first.saveFile);

	const auto secretServer = MakeServerKey("LOCALHOST", 7781);
	const std::string serverSecret{ "0123456789abcdef0123456789abcdef" };
	assert(!LookupServerSecret(secretServer));
	assert(!LookupServerSecret(""));
	assert(StoreServerSecret(secretServer, serverSecret));
	assert(LookupServerSecret(secretServer) == std::optional<std::string>{ serverSecret });
	assert(StoreServerSecret(secretServer, serverSecret));
	assert(LookupServerSecret(secretServer) == std::optional<std::string>{ serverSecret });
	assert(!StoreServerSecret(secretServer, ""));
	assert(!StoreServerSecret("", serverSecret));
	assert(!StoreServerSecret(secretServer, "0123456789abcdef0123456789abcde"));
	assert(!StoreServerSecret(secretServer, "0123456789abcdef0123456789abcdef0"));
	assert(!StoreServerSecret(secretServer, "0123456789abcdef0123456789abcdeF"));
	assert(!StoreServerSecret(secretServer, std::string(32, 'A')));
	assert(!StoreServerSecret(secretServer, "0123456789abcdef0123456789abcdeg"));
	assert(!StoreServerSecret(secretServer, std::string(64, 'a')));
	assert(!StoreServerSecret(secretServer, "0123456789abcdef 123456789abcdef"));
	assert(LookupServerSecret(secretServer) == std::optional<std::string>{ serverSecret });
	assert(!LookupServerSecret(MakeServerKey("LOCALHOST", 7782)).has_value());

	const auto third = MakeSave("Save3_AAAA1111_Nomad_Commonwealth_20260814000404_1_2", MakeHex(0xcc33), 3000);
	assert(RecordAttestedSave(serverA, third));
	const auto firstUpper = MakeSave("SAVE1_AAAA1111_NOMAD_COMMONWEALTH_20260814000202_1_2", MakeHex(0xdd44), 4000);
	assert(RecordAttestedSave(serverA, firstUpper));
	{
		const auto saves = LookupAttestedSaves(serverA);
		assert(saves.size() == 2);
		assert(saves[0].saveFile == third.saveFile);
		assert(saves[1].saveFile == firstUpper.saveFile);
		assert(saves[1].sha256Hex == MakeHex(0xdd44));
	}

	const auto boundServer = MakeServerKey("LOCALHOST", 7779);
	for (int i = 0; i < 40; ++i) {
		const auto saveFile = std::format("Bound{}_AAAA1111_Nomad_Commonwealth_20260814000505_1_2", i);
		assert(RecordAttestedSave(boundServer, MakeSave(saveFile, MakeHex(static_cast<std::uint64_t>(i)), static_cast<std::uint64_t>(i))));
	}
	{
		const auto saves = LookupAttestedSaves(boundServer);
		assert(saves.size() == 32);
		assert(saves.front().saveFile == "Bound8_AAAA1111_Nomad_Commonwealth_20260814000505_1_2");
		assert(saves.back().saveFile == "Bound39_AAAA1111_Nomad_Commonwealth_20260814000505_1_2");
	}

	const auto recoveryServer = MakeServerKey("LOCALHOST", 7780);
	const auto recoveryA = MakeSave("Save30_AAAA1111_Nomad_Commonwealth_20260814000606_1_2", MakeHex(0xee55), 5000);
	const auto recoveryB = MakeSave("Save31_AAAA1111_Nomad_Commonwealth_20260814000707_1_2", MakeHex(0xff66), 6000);
	assert(RecordAttestedSave(recoveryServer, recoveryA));
	assert(RecordAttestedSave(recoveryServer, recoveryB));
	assert(std::filesystem::exists(scratch / "profiles" / "save-lineage.json.bak"));
	WriteText(lineagePath, "{ corrupt");
	{
		const auto saves = LookupAttestedSaves(recoveryServer);
		assert(saves.size() == 1);
		assert(saves.front().saveFile == recoveryA.saveFile);
	}

	const auto blockedDirectory = scratch / "blocked";
	WriteText(blockedDirectory, "not a directory");
	SetProfilesPathForTesting(blockedDirectory / "server-profiles.json");
	assert(!RecordAttestedSave(serverA, first));
	SetProfilesPathForTesting(profilesPath);

	assert(!RecordAttestedSave(serverA, MakeSave("", MakeHex(1), 1)));
	assert(!RecordAttestedSave(serverA, MakeSave("Save4_AAAA1111_Nomad_Commonwealth_20260814000808_1_2", "", 1)));
	assert(!RecordAttestedSave(serverA, MakeSave("Save5_AAAA1111_Nomad_Commonwealth_20260814000808_1_2", std::string(64, 'A'), 1)));
	assert(!RecordAttestedSave("", first));

	{
		const auto dir = scratch / "oversized";
		std::filesystem::create_directories(dir);
		SetProfilesPathForTesting(dir / "server-profiles.json");
		const std::string server = "oversized-server";
		nlohmann::json primary;
		primary["servers"][server]["attested"].push_back({ { "saveFile", "Primary1_AAAA1111_Nomad_Commonwealth_20260814000101_1_2" }, { "sha256", MakeHex(1) }, { "attestedAt", 1 } });
		primary["padding"] = std::string(1024 * 1024 + 1, 'x');
		nlohmann::json backup;
		backup["servers"][server]["attested"].push_back({ { "saveFile", "Backup1_AAAA1111_Nomad_Commonwealth_20260814000202_1_2" }, { "sha256", MakeHex(2) }, { "attestedAt", 2 } });
		WriteText(dir / "save-lineage.json", primary.dump());
		WriteText(dir / "save-lineage.json.bak", backup.dump());
		const auto saves = LookupAttestedSaves(server);
		assert(saves.size() == 1);
		assert(saves.front().saveFile == "Backup1_AAAA1111_Nomad_Commonwealth_20260814000202_1_2");
	}

	{
		const auto dir = scratch / "hand";
		std::filesystem::create_directories(dir);
		SetProfilesPathForTesting(dir / "server-profiles.json");
		const std::string server = "hand-server";
		nlohmann::json hand;
		for (std::uint64_t i = 0; i < 40; ++i) {
			hand["servers"][server]["attested"].push_back({ { "saveFile", std::format("Hand{}_AAAA1111_Nomad_Commonwealth_20260814000303_1_2", i) }, { "sha256", MakeHex(i) }, { "attestedAt", i } });
		}
		WriteText(dir / "save-lineage.json", hand.dump());
		const auto saves = LookupAttestedSaves(server);
		assert(saves.size() == 32);
		assert(saves.front().saveFile == "Hand8_AAAA1111_Nomad_Commonwealth_20260814000303_1_2");
		assert(saves.back().saveFile == "Hand39_AAAA1111_Nomad_Commonwealth_20260814000303_1_2");
	}

	{
		const auto dir = scratch / "invalid";
		std::filesystem::create_directories(dir);
		SetProfilesPathForTesting(dir / "server-profiles.json");
		const std::string server = "invalid-server";
		nlohmann::json invalid;
		invalid["servers"][server]["attested"].push_back({ { "saveFile", "Good1_AAAA1111_Nomad_Commonwealth_20260814000404_1_2" }, { "sha256", MakeHex(1) }, { "attestedAt", 1 } });
		invalid["servers"][server]["attested"].push_back({ { "saveFile", "Upper1_AAAA1111_Nomad_Commonwealth_20260814000404_1_2" }, { "sha256", std::string(64, 'A') }, { "attestedAt", 2 } });
		invalid["servers"][server]["attested"].push_back({ { "saveFile", "Short1_AAAA1111_Nomad_Commonwealth_20260814000404_1_2" }, { "sha256", "abc" }, { "attestedAt", 3 } });
		invalid["servers"][server]["attested"].push_back({ { "saveFile", "Good2_AAAA1111_Nomad_Commonwealth_20260814000404_1_2" }, { "sha256", MakeHex(4) }, { "attestedAt", 4 } });
		WriteText(dir / "save-lineage.json", invalid.dump());
		const auto saves = LookupAttestedSaves(server);
		assert(saves.size() == 2);
		assert(saves[0].saveFile == "Good1_AAAA1111_Nomad_Commonwealth_20260814000404_1_2");
		assert(saves[1].saveFile == "Good2_AAAA1111_Nomad_Commonwealth_20260814000404_1_2");
	}

	{
		const auto dir = scratch / "servers";
		std::filesystem::create_directories(dir);
		SetProfilesPathForTesting(dir / "server-profiles.json");
		for (int i = 0; i < 65; ++i) {
			const auto server = std::format("cap-server-{:02}", i);
			const auto save = MakeSave(std::format("Cap{}_AAAA1111_Nomad_Commonwealth_20260814000909_1_2", i), MakeHex(static_cast<std::uint64_t>(i)), static_cast<std::uint64_t>(10000 + i));
			assert(RecordAttestedSave(server, save));
		}
		assert(LookupAttestedSaves("cap-server-00").empty());
		assert(LookupAttestedSaves("cap-server-01").size() == 1);
		assert(LookupAttestedSaves("cap-server-64").size() == 1);
	}

	std::filesystem::remove_all(scratch);
	return 0;
}
