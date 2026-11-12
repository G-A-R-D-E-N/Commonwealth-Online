#include "F4TServerProfileStore.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>

namespace
{
	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-first-join-hold-tests-{}", stamp);
	}
}

void RunServerProfileFirstJoinTests()
{
	using namespace F4T::ServerProfileStore;

	const auto scratch = MakeScratchDirectory();
	const auto serverKey = MakeServerKey("LOCALHOST", 7777);
	const std::string saveName = "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2";

	{
		const auto path = scratch / "save-less" / "server-profiles.json";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		assert(!FirstJoinHoldIsSaveLess(serverKey));
		assert(!ClearSaveLessFirstJoinHold(serverKey));
		assert(RecordFirstJoinHold(serverKey, 0, "", "first join started"));
		assert(HasFirstJoinHold(serverKey));
		assert(FirstJoinHoldIsSaveLess(serverKey));
		assert(!FirstJoinHoldIsSaveLess(MakeServerKey("other", 7777)));
		assert(!ClearSaveLessFirstJoinHold(MakeServerKey("other", 7777)));
		assert(ClearSaveLessFirstJoinHold(serverKey));
		assert(!HasFirstJoinHold(serverKey));
		assert(!FirstJoinHoldIsSaveLess(serverKey));
		SetProfilesPathForTesting(path);
		assert(!HasFirstJoinHold(serverKey));
	}

	{
		const auto path = scratch / "save-hold" / "server-profiles.json";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		assert(RecordFirstJoinHold(serverKey, 7, saveName, "first save did not bind"));
		assert(HasFirstJoinHold(serverKey));
		assert(!FirstJoinHoldIsSaveLess(serverKey));
		assert(!ClearSaveLessFirstJoinHold(serverKey));
		assert(HasFirstJoinHold(serverKey));
		SetProfilesPathForTesting(path);
		assert(HasFirstJoinHold(serverKey));
		assert(!FirstJoinHoldIsSaveLess(serverKey));
	}

	{
		const auto path = scratch / "blocked-clear" / "server-profiles.json";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		assert(RecordFirstJoinHold(serverKey, 11, "", "first join started"));
		const auto tempPath = path.string() + ".tmp";
		std::filesystem::create_directory(tempPath);
		assert(!ClearSaveLessFirstJoinHold(serverKey));
		SetProfilesPathForTesting(path);
		assert(HasFirstJoinHold(serverKey));
		assert(FirstJoinHoldIsSaveLess(serverKey));
		std::filesystem::remove_all(tempPath);
		assert(ClearSaveLessFirstJoinHold(serverKey));
		assert(!HasFirstJoinHold(serverKey));
	}

	{
		const auto path = scratch / "hold-slot" / "server-profiles.json";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		SetConnectedCharacterSlot(2u);
		assert(RecordFirstJoinHold(serverKey, 0, "", "first join started", ConnectedCharacterSlot()));
		assert(FirstJoinHoldSlot(serverKey) == std::optional<std::uint32_t>{ 2u });
		SetProfilesPathForTesting(path);
		assert(FirstJoinHoldSlot(serverKey) == std::optional<std::uint32_t>{ 2u });
		assert(ClearSaveLessFirstJoinHold(serverKey));

		SetConnectedCharacterSlot(std::nullopt);
		assert(RecordFirstJoinHold(serverKey, 0, "", "first join started", ConnectedCharacterSlot()));
		assert(!FirstJoinHoldSlot(serverKey));
		assert(ClearSaveLessFirstJoinHold(serverKey));

		SetConnectedCharacterSlot(3u);
		assert(RecordFirstJoinHold(serverKey, 9, saveName, "first save did not bind", ConnectedCharacterSlot()));
		assert(!FirstJoinHoldSlot(serverKey));
		SetConnectedCharacterSlot(std::nullopt);
	}

	{
		const auto path = scratch / "hold-slot-blocked" / "server-profiles.json";
		std::filesystem::create_directories(path.parent_path());
		SetProfilesPathForTesting(path);
		SetConnectedCharacterSlot(4u);
		const auto tempPath = path.string() + ".tmp";
		std::filesystem::create_directory(tempPath);
		assert(!RecordFirstJoinHold(serverKey, 0, "", "first join started", ConnectedCharacterSlot()));
		SetProfilesPathForTesting(path);
		assert(HasFirstJoinHold(serverKey));
		std::filesystem::remove_all(tempPath);
		assert(!HasFirstJoinHold(serverKey));
		assert(RecordFirstJoinHold(serverKey, 0, "", "first join started", ConnectedCharacterSlot()));
		assert(FirstJoinHoldSlot(serverKey) == std::optional<std::uint32_t>{ 4u });
		SetProfilesPathForTesting(path);
		assert(FirstJoinHoldSlot(serverKey) == std::optional<std::uint32_t>{ 4u });
		assert(ClearSaveLessFirstJoinHold(serverKey));
		SetConnectedCharacterSlot(std::nullopt);
	}

	SetConnectedCharacterSlot(5u);
	assert(ConnectedCharacterSlot() == std::optional<std::uint32_t>{ 5u });
	SetConnectedCharacterSlot(std::nullopt);
	assert(!ConnectedCharacterSlot());

	std::filesystem::remove_all(scratch);
}
