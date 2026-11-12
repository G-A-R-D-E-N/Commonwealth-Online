#include "F4TServerProfileStore.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-profile-recovery-tests-{}", stamp);
	}

	void WriteText(const std::filesystem::path& a_path, const std::string& a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		assert(output.is_open());
		output << a_text;
		assert(output.good());
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		assert(input.is_open());
		return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	std::vector<std::filesystem::path> QuarantinedCopies(const std::filesystem::path& a_directory)
	{
		std::vector<std::filesystem::path> copies;
		for (const auto& entry : std::filesystem::directory_iterator(a_directory)) {
			if (entry.path().filename().string().starts_with("server-profiles.json.corrupt-")) {
				copies.push_back(entry.path());
			}
		}
		return copies;
	}
}

void RunServerProfileRecoveryTests()
{
	using namespace F4T::ServerProfileStore;

	const auto scratch = MakeScratchDirectory();
	const auto serverKey = MakeServerKey("LOCALHOST", 7777);
	std::filesystem::create_directories(scratch);

	{
		const auto path = scratch / "non-object-servers" / "server-profiles.json";
		const std::string backupSave = "Save50_AAAA1111_Nomad_Commonwealth_20260814002020_1_2";
		std::filesystem::create_directories(path.parent_path());
		WriteText(path, R"({"servers":[]})");
		WriteText(path.string() + ".bak", nlohmann::json{ { "servers", { { serverKey, { { "saveFile", backupSave } } } } } }.dump());
		SetProfilesPathForTesting(path);
		assert(LookupSaveFile(serverKey) == backupSave);
	}

	{
		const auto path = scratch / "quarantine-primary" / "server-profiles.json";
		const std::string backupSave = "Save51_AAAA1111_Nomad_Commonwealth_20260814002121_1_2";
		std::filesystem::create_directories(path.parent_path());
		WriteText(path, "invalid");
		WriteText(path.string() + ".bak", nlohmann::json{ { "servers", { { serverKey, { { "saveFile", backupSave } } } } } }.dump());
		SetProfilesPathForTesting(path);
		assert(LookupSaveFile(serverKey) == backupSave);
		assert(TouchProfile(serverKey));

		const auto quarantined = QuarantinedCopies(path.parent_path());
		assert(quarantined.size() == 1 && ReadText(quarantined.front()) == "invalid");
		assert(ReadText(path.string() + ".bak").find(backupSave) != std::string::npos);
		SetProfilesPathForTesting(path);
		assert(LookupSaveFile(serverKey) == backupSave);
		assert(ReadText(path).find(backupSave) != std::string::npos);
	}

#ifdef _WIN32
	{
		const auto path = scratch / "quarantine-refused" / "server-profiles.json";
		const std::string backupSave = "Save52_AAAA1111_Nomad_Commonwealth_20260814002222_1_2";
		std::filesystem::create_directories(path.parent_path());
		WriteText(path, "invalid");
		WriteText(path.string() + ".bak", nlohmann::json{ { "servers", { { serverKey, { { "saveFile", backupSave } } } } } }.dump());
		SetProfilesPathForTesting(path);
		assert(LookupSaveFile(serverKey) == backupSave);
		{
			std::ifstream held(path, std::ios::binary);
			assert(held.is_open());
			assert(!TouchProfile(serverKey));
		}
		assert(ReadText(path) == "invalid");
		assert(QuarantinedCopies(path.parent_path()).empty());
		SetProfilesPathForTesting(path);
		assert(LookupSaveFile(serverKey) == backupSave);
	}
#endif

	{
		const auto path = scratch / "previous-saves" / "server-profiles.json";
		const std::string world(32, 'a');
		const std::string oldSave = "Save60_AAAA1111_Nomad_Commonwealth_20260814002323_1_2";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		SetConnectedWorldId(world);
		SetConnectedCharacterSlot(1);
		assert(BeginFreshCharacter(serverKey, 1, world));
		assert(ConfirmFreshCharacter(serverKey, 1, world));
		assert(BindSaveFile(serverKey, oldSave));
		assert(BeginFreshCharacter(serverKey, 1, world));

		const auto stored = nlohmann::json::parse(ReadText(path));
		const auto& previous = stored.at("servers").at(serverKey).at("previousCharacterSaves");
		assert(previous.is_array() && previous.size() == 1);
		assert(previous[0].at("worldId") == world);
		assert(previous[0].at("slot") == 1);
		assert(previous[0].at("binding").at("saveFile") == oldSave);

		SetConnectedCharacterSlot(std::nullopt);
		SetConnectedWorldId({});
	}

	std::error_code error;
	std::filesystem::remove_all(scratch, error);
}
