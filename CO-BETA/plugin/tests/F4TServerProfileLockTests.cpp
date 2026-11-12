#include "F4TServerProfileStore.h"
#include "F4TServerProfileStoreInternal.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-profile-lock-tests-{}", stamp);
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

	std::string SaveName(int a_index)
	{
		return std::format("Save{}_AAAA1111_Nomad_Commonwealth_20260814002323_1_2", 100 + a_index);
	}

	std::size_t CountTagged(const nlohmann::json& a_archive, bool a_tagged)
	{
		std::size_t count = 0;
		for (const auto& item : a_archive) {
			count += item.contains("worldId") == a_tagged ? 1 : 0;
		}
		return count;
	}
}

void RunServerProfileLockTests()
{
	using namespace F4T::ServerProfileStore;

	const auto scratch = MakeScratchDirectory();
	const auto keyA = MakeServerKey("LOCALHOST", 7777);
	const auto keyB = MakeServerKey("OTHERHOST", 7777);
	const auto keyC = MakeServerKey("THIRDHOST", 7777);
	std::filesystem::create_directories(scratch);

	{
		const auto path = scratch / "stale" / "server-profiles.json";
		SetProfilesPathForTesting(path);
		assert(!EnsureProfileId(keyA).empty());

		auto external = nlohmann::json::parse(ReadText(path));
		external["servers"][keyB] = nlohmann::json{ { "profileId", "co-external" } };
		WriteText(path, external.dump());
		assert(TouchProfile(keyA));
		auto stored = nlohmann::json::parse(ReadText(path));
		assert(stored.at("servers").contains(keyA));
		assert(stored.at("servers").at(keyB).at("profileId") == "co-external");

		external = stored;
		external["servers"][keyB]["profileId"] = "co-external-2";
		WriteText(path, external.dump());
		assert(StoreResumeCredential(keyA, "token-a", 4102444800.0));
		stored = nlohmann::json::parse(ReadText(path));
		assert(stored.at("servers").at(keyA).contains("resumeTokenProtected"));
		assert(stored.at("servers").at(keyB).at("profileId") == "co-external-2");
		assert(LookupProfileId(keyB) == "co-external-2");
	}

	{
		const auto path = scratch / "locked" / "server-profiles.json";
		SetProfilesPathForTesting(path);
		assert(!EnsureProfileId(keyA).empty());
		const auto before = ReadText(path);
		F4T::ServerProfileStore::Internal::SetLockTimeoutForTesting(50);
		{
			F4T::ServerProfileStore::Internal::ProfileFileLock holder(path);
			assert(holder.Held());
			F4T::ServerProfileStore::Internal::ProfileFileLock second(path);
			assert(!second.Held());
			assert(!TouchProfile(keyA));
			assert(!StoreResumeCredential(keyA, "token-b", 4102444800.0));
			assert(EnsureProfileId(keyC).empty());
			assert(ReadText(path) == before);
		}
		F4T::ServerProfileStore::Internal::SetLockTimeoutForTesting(2000);
		assert(TouchProfile(keyA));
		assert(!EnsureProfileId(keyC).empty());
		F4T::ServerProfileStore::Internal::ProfileFileLock reacquired(path);
		assert(reacquired.Held());
	}

	{
		const auto path = scratch / "previous-cap" / "server-profiles.json";
		const std::string world(32, 'a');
		SetProfilesPathForTesting(path);
		SetConnectedWorldId(world);
		SetConnectedCharacterSlot(1);
		for (int index = 0; index < 7; ++index) {
			assert(BeginFreshCharacter(keyA, 1, world));
			assert(ConfirmFreshCharacter(keyA, 1, world));
			assert(BindSaveFile(keyA, SaveName(index)));
		}
		assert(BeginFreshCharacter(keyA, 1, world));

		auto stored = nlohmann::json::parse(ReadText(path));
		const auto& archive = stored.at("servers").at(keyA).at("previousCharacterSaves");
		assert(archive.size() == 5);
		assert(archive.front().at("binding").at("saveFile") == SaveName(2));
		assert(archive.back().at("binding").at("saveFile") == SaveName(6));

		assert(BeginFreshCharacter(keyA, 2, world));
		assert(ConfirmFreshCharacter(keyA, 2, world));
		SetConnectedCharacterSlot(2);
		assert(BindSaveFile(keyA, SaveName(50)));
		assert(BeginFreshCharacter(keyA, 2, world));
		stored = nlohmann::json::parse(ReadText(path));
		const auto& both = stored.at("servers").at(keyA).at("previousCharacterSaves");
		assert(both.size() == 6);
		assert(both.back().at("slot") == 2);
		assert(both.front().at("binding").at("saveFile") == SaveName(2));
		SetConnectedCharacterSlot(std::nullopt);
		SetConnectedWorldId({});
	}

	{
		const auto path = scratch / "previous-old-format" / "server-profiles.json";
		const std::string world(32, 'b');
		std::filesystem::create_directories(path.parent_path());
		nlohmann::json seeded;
		auto& entry = seeded["servers"][keyA];
		entry["characterSaves"][world]["1"] = nlohmann::json{ { "saveFile", SaveName(80) } };
		for (int index = 0; index < 7; ++index) {
			entry["previousCharacterSaves"].push_back(nlohmann::json{ { "saveFile", SaveName(index) } });
		}
		WriteText(path, seeded.dump());
		SetProfilesPathForTesting(path);
		assert(BeginFreshCharacter(keyA, 1, world));

		const auto stored = nlohmann::json::parse(ReadText(path));
		const auto& archive = stored.at("servers").at(keyA).at("previousCharacterSaves");
		assert(archive.size() == 6);
		assert(CountTagged(archive, false) == 5);
		assert(CountTagged(archive, true) == 1);
		assert(archive.front().at("saveFile") == SaveName(2));
		assert(archive.back().at("binding").at("saveFile") == SaveName(80));
	}

	SetProfilesPathForTesting({});
	std::error_code error;
	std::filesystem::remove_all(scratch, error);
}
