#include "F4TMultiplayerProfile.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main()
{
	using namespace F4T::MultiplayerProfile;
	const auto root = std::filesystem::temp_directory_path() /
		("coui-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	const auto path = root / "profile.json";
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	std::string error;
	assert(SaveDisplayName("Vault Dweller", error));
	char buffer[129]{};
	assert(CopyDisplayName(buffer, sizeof(buffer)));
	assert(std::string(buffer) == "Vault Dweller");
	assert(!CopyDisplayName(buffer, 4));
	assert(buffer[0] == '\0');
	assert(!SaveDisplayName(std::string(129, 'x'), error));
	assert(!SaveDisplayName("bad\nname", error));
	assert(!SaveDisplayName(std::string("bad\0name", 8), error));
	assert(!SaveDisplayName(" trailing ", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName() == "Vault Dweller");
	assert(SaveDisplayName(std::string(128, 'x'), error));
	assert(CopyDisplayName(buffer, sizeof(buffer)));
	assert(std::string(buffer).size() == 128);
	assert(!SaveDisplayName(std::string(1, '\xff'), error));
	assert(error == "Display name must be valid UTF-8.");
	assert(!std::filesystem::exists(std::filesystem::path(path.string() + ".tmp")));
	assert(SaveDisplayName("Ren\xc3\xa9" "e", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName() == "Ren\xc3\xa9" "e");
	assert(SaveDisplayName("", error));
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	{
		std::ofstream output(path);
		output << "not json";
	}
	SetStorePathForTesting(path.string());
	assert(GetDisplayName().empty());
	const auto blocked = root / "blocked";
	std::ofstream(blocked).put('x');
	SetStorePathForTesting((blocked / "profile.json").string());
	assert(!SaveDisplayName("Cannot Save", error));
	assert(!error.empty());
	const auto occupied = root / "occupied.json";
	std::filesystem::create_directories(occupied);
	SetStorePathForTesting(occupied.string());
	assert(!SaveDisplayName("Blocked Move", error));
	assert(!std::filesystem::exists(std::filesystem::path(occupied.string() + ".tmp")));
	std::filesystem::remove_all(root);
}
