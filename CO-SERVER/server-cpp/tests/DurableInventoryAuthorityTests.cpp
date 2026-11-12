#include "AdminCommandDispatcher.h"
#include "DurableStateFileStore.h"
#include "SaveRecoveryAdmin.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::AdminCommandDispatcher;
	using CommonwealthOnline::Server::AdminCommandHooks;
	using CommonwealthOnline::Server::DurableStateFileStore;

	std::filesystem::path UniqueDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		auto directory = std::filesystem::temp_directory_path() / ("co-inventory-authority-test-" + std::to_string(stamp));
		std::filesystem::create_directories(directory);
		return directory;
	}

	void WriteText(const std::filesystem::path& a_path, const std::string& a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		CHECK(output);
		output << a_text;
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		CHECK(input);
		return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
	}

	std::unique_ptr<DurableStateFileStore> Open(const std::filesystem::path& a_path)
	{
		return std::make_unique<DurableStateFileStore>(
			a_path, [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
	}

	void SeedWithBackup(const std::filesystem::path& a_path)
	{
		const auto store = Open(a_path);
		F4T::Identity::TokenHash hash{};
		store->SaveResumeCredential("profile", hash, std::nullopt, true, 5000.0);
		store->SaveResumeCredential("profile", hash, std::nullopt, true, 6000.0);
		CHECK(std::filesystem::exists(a_path.string() + ".bak"));
	}

	void TestClearCommandClearsAndPersists()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		SeedWithBackup(path);
		WriteText(path, "corrupt");
		{
			const auto recovered = Open(path);
			CHECK(recovered->InventoryAuthorityBlocked());
			AdminCommandHooks hooks;
			CommonwealthOnline::Server::InstallInventoryAuthorityHooks(hooks, *recovered);
			const AdminCommandDispatcher dispatcher(hooks);

			const auto status = dispatcher.Dispatch(Json{ { "cmd", "inventory-authority-status" } });
			CHECK(status.value("ok", false) && status.at("data").at("inventoryAuthorityBlocked") == true);

			const auto missing = dispatcher.Dispatch(Json{ { "cmd", "inventory-authority-clear" } });
			CHECK(!missing.value("ok", true));
			const auto wrong = dispatcher.Dispatch(Json{ { "cmd", "inventory-authority-clear" }, { "confirm", "yes" } });
			CHECK(!wrong.value("ok", true));
			CHECK(recovered->InventoryAuthorityBlocked());

			const auto cleared = dispatcher.Dispatch(Json{ { "cmd", "inventory-authority-clear" }, { "confirm", "inventory-authority-verified" } });
			CHECK(cleared.value("ok", false));
			CHECK(!recovered->InventoryAuthorityBlocked());
		}
		const auto restarted = Open(path);
		CHECK(!restarted->InventoryAuthorityBlocked());
		CHECK(Json::parse(ReadText(path)).at("inventoryAuthorityBlocked") == false);
		std::filesystem::remove_all(directory);
	}

	void TestRecoveryQuarantinesPrimary()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		SeedWithBackup(path);
		WriteText(path, "corrupt");
		const auto recovered = Open(path);
		CHECK(recovered->InventoryAuthorityBlocked() && recovered->RecoveryNotice());
		const auto quarantined = std::filesystem::path{ path.string() + ".quarantine.1000" };
		CHECK(std::filesystem::exists(quarantined));
		CHECK(ReadText(quarantined) == "corrupt");
		CHECK(Json::parse(ReadText(path)).at("inventoryAuthorityBlocked") == true);
		std::filesystem::remove_all(directory);
	}

	void TestUnreadablePrimaryRefusesStartup()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		SeedWithBackup(path);
		const auto backupBefore = ReadText(path.string() + ".bak");
		std::filesystem::remove(path);
		std::filesystem::create_directories(path);
		bool threw = false;
		try {
			(void)Open(path);
		} catch (const std::runtime_error&) {
			threw = true;
		}
		CHECK(threw);
		CHECK(std::filesystem::is_directory(path));
		CHECK(ReadText(path.string() + ".bak") == backupBefore);
		CHECK(!std::filesystem::exists(path.string() + ".quarantine.1000"));
		std::filesystem::remove_all(directory);
	}

	bool OpenThrows(const std::filesystem::path& a_path)
	{
		try {
			(void)Open(a_path);
		} catch (const std::runtime_error&) {
			return true;
		}
		return false;
	}

	void RemoveStateFiles(const std::filesystem::path& a_path)
	{
		std::filesystem::remove(a_path);
		std::filesystem::remove(a_path.string() + ".bak");
	}

	void TestMissingStateAfterFirstRunRefusesFreshCreate()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		const auto marker = std::filesystem::path{ path.string() + ".initialized" };
		CHECK(!std::filesystem::exists(marker));
		(void)Open(path);
		CHECK(std::filesystem::exists(path) && std::filesystem::exists(marker));

		RemoveStateFiles(path);
		CHECK(OpenThrows(path));
		CHECK(!std::filesystem::exists(path));

		std::filesystem::remove(marker);
		(void)Open(path);
		CHECK(std::filesystem::exists(path) && std::filesystem::exists(marker));
		std::filesystem::remove_all(directory);
	}

	void TestExistingStateWithoutMarkerLoadsAndGetsOne()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		const auto marker = std::filesystem::path{ path.string() + ".initialized" };
		(void)Open(path);
		std::filesystem::remove(marker);
		(void)Open(path);
		CHECK(std::filesystem::exists(marker));
		std::filesystem::remove_all(directory);
	}

	void TestSeparateMarkerProtectsAMissingVolume()
	{
		const auto directory = UniqueDirectory();
		const auto state = directory / "volume" / "durable-state.json";
		const auto external = directory / "config" / "durable-state.json.initialized";
		const auto create = [&] {
			return std::make_unique<DurableStateFileStore>(
				state, [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; },
				DurableStateFileStore::PostCommitHook{}, external);
		};
		(void)create();
		CHECK(std::filesystem::exists(external));
		std::filesystem::remove_all(directory / "volume");
		bool refused = false;
		try {
			(void)create();
		} catch (const std::runtime_error&) {
			refused = true;
		}
		CHECK(refused);
		CHECK(!std::filesystem::exists(state));
		std::filesystem::remove_all(directory);
	}

	void BlockBackup(const std::filesystem::path& a_path)
	{
		const auto backup = std::filesystem::path{ a_path.string() + ".bak" };
		std::filesystem::remove_all(backup);
		std::filesystem::create_directories(backup / "occupied");
	}

	void TestShutdownFlushRetriesAFailedFinalCommit()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		const auto instance = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, [] { return 1000.0; }, instance);
		CHECK(store.UpdatePlayerState("flush-profile", Json{ { "type", "playerState" }, { "characterName", "First" } }));
		store.SetDeferCommits(true);
		BlockBackup(path);
		CHECK(store.UpdatePlayerState("flush-profile", Json{ { "type", "playerState" }, { "characterName", "Second" } }));
		store.WaitForCommits();
		CHECK(store.LastWriteFailed());
		CHECK(Json::parse(ReadText(path)).at("profiles").at("flush-profile").at("playerState").at("characterName") == "First");

		int sleeps = 0;
		CHECK(store.FlushForShutdown(3, [&](std::chrono::milliseconds) {
			++sleeps;
			std::filesystem::remove_all(path.string() + ".bak");
		}));
		CHECK(sleeps == 1);
		CHECK(!store.LastWriteFailed());
		CHECK(Json::parse(ReadText(path)).at("profiles").at("flush-profile").at("playerState").at("characterName") == "Second");
		std::filesystem::remove_all(directory);
	}

	void TestShutdownFlushGivesUpLoudlyAndKeepsTheLastGoodFile()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		const auto instance = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, [] { return 1000.0; }, instance);
		CHECK(store.UpdatePlayerState("flush-profile", Json{ { "type", "playerState" }, { "characterName", "First" } }));
		store.SetDeferCommits(true);
		BlockBackup(path);
		CHECK(store.UpdatePlayerState("flush-profile", Json{ { "type", "playerState" }, { "characterName", "Second" } }));
		store.WaitForCommits();
		const auto lastGood = ReadText(path);

		int sleeps = 0;
		CHECK(!store.FlushForShutdown(3, [&](std::chrono::milliseconds) { ++sleeps; }));
		CHECK(sleeps == 2);
		CHECK(ReadText(path) == lastGood);
		std::filesystem::remove_all(path.string() + ".bak");
		CHECK(store.FlushForShutdown(1, [](std::chrono::milliseconds) {}));
		CHECK(Json::parse(ReadText(path)).at("profiles").at("flush-profile").at("playerState").at("characterName") == "Second");
		std::filesystem::remove_all(directory);
	}

	void TestSlotEraseDropsSnapshotRemovals()
	{
		const auto directory = UniqueDirectory();
		const auto path = directory / "durable-state.json";
		const std::string account = "removal-account";
		const auto key = F4T::CharacterSlots::SlotProfileKey(account, 0);
		const auto other = F4T::CharacterSlots::SlotProfileKey(account, 1);
		CHECK(!key.empty() && !other.empty() && key != other);
		(void)Open(path);
		auto root = Json::parse(ReadText(path));
		root["snapshotRemovals"] = Json{ { key, Json{ { "item:1", 3 } } }, { other, Json{ { "item:1", 4 } } } };
		WriteText(path, root.dump(2));
		{
			const auto store = Open(path);
			CHECK(store->SnapshotRemovals(key, "item:1") == 3);
			CHECK(store->EraseCharacterSlotState(account, 0, false, true));
			store->WaitForCommits();
			CHECK(store->SnapshotRemovals(key, "item:1") == 0);
			CHECK(store->SnapshotRemovals(other, "item:1") == 4);
		}
		const auto saved = Json::parse(ReadText(path)).at("snapshotRemovals");
		CHECK(!saved.contains(key) && saved.at(other).at("item:1") == 4);
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestClearCommandClearsAndPersists();
	TestRecoveryQuarantinesPrimary();
	TestUnreadablePrimaryRefusesStartup();
	TestMissingStateAfterFirstRunRefusesFreshCreate();
	TestExistingStateWithoutMarkerLoadsAndGetsOne();
	TestSeparateMarkerProtectsAMissingVolume();
	TestShutdownFlushRetriesAFailedFinalCommit();
	TestShutdownFlushGivesUpLoudlyAndKeepsTheLastGoodFile();
	TestSlotEraseDropsSnapshotRemovals();
	return 0;
}
