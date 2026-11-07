#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "DurableStateFileStore.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <sys/resource.h>
#endif

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;

	std::filesystem::path UniqueDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / ("co-durable-state-test-" + std::to_string(stamp));
	}

	void WriteText(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		CHECK(output);
		output << a_text;
		output.flush();
		CHECK(output.good());
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		CHECK(input);
		return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
	}
	void TestStartingInventory()
	{
		const auto directory = UniqueDirectory();
		std::filesystem::create_directories(directory);
		const auto path = directory / "inventory.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		auto clock = [] { return 1000.0; };
		auto id = [] { return "0123456789abcdef0123456789abcdef"; };
		bool failPostCommit = false;
		try {
			DurableStateFileStore store(path, clock, id, [&] { if (failPostCommit) throw std::runtime_error("post-commit fault"); });
			bool threw = false;
			try { (void)store.InitializeInventory("unadmitted", catalog); }
			catch (const std::invalid_argument&) { threw = true; }
			CHECK(threw && !store.GetInventory("unadmitted"));
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile", hash, std::nullopt, true, 5000.0);
			const auto diskBefore = ReadText(path);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			threw = false;
			try { (void)store.InitializeInventory("profile", catalog); }
			catch (const std::exception&) { threw = true; }
			CHECK(threw && !store.GetInventory("profile"));
			CHECK(ReadText(path) == diskBefore);
			std::filesystem::remove_all(backup);
			failPostCommit = true;
			const auto granted = store.InitializeInventory("profile", catalog);
			CHECK(store.TakePersistenceWarning());
			CHECK(granted.at("initialized") == true && granted.at("revision") == 0);
			CHECK(granted.at("items").at("test.esm:00000123").at("count") == 5);
			failPostCommit = false;
			const auto diskGranted = ReadText(path);
			CHECK(store.InitializeInventory("profile", Json::array()) == granted);
			CHECK(ReadText(path) == diskGranted);
			CHECK(store.DeleteResumeCredential("profile"));
			store.SaveResumeCredential("profile", hash, std::nullopt, false, 6000.0);
			CHECK(store.InitializeInventory("profile", Json::array()) == granted);
			store.SaveResumeCredential("empty", hash, std::nullopt, true, 6000.0);
			const auto empty = store.InitializeInventory("empty", Json::array());
			CHECK(empty.at("items").empty());
			CHECK(store.InitializeInventory("empty", catalog) == empty);
			DurableStateFileStore restarted(path, clock, id);
			CHECK(restarted.InitializeInventory("profile", Json::array()) == granted);
			CHECK(restarted.InitializeInventory("empty", catalog) == empty);
			WriteText(path, "corrupt");
			DurableStateFileStore recovered(path, clock, id);
			CHECK(recovered.RecoveryNotice());
			threw = false;
			try { (void)recovered.InitializeInventory("empty", catalog); }
			catch (const std::runtime_error&) { threw = true; }
			CHECK(threw && !recovered.GetInventory("empty"));
			CHECK(recovered.InitializeInventory("profile", catalog) == granted);
			DurableStateFileStore recoveredRestart(path, clock, id);
			threw = false;
			try { (void)recoveredRestart.InitializeInventory("empty", catalog); }
			catch (const std::runtime_error&) { threw = true; }
			CHECK(threw && recoveredRestart.InitializeInventory("profile", catalog) == granted);
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}

	void TestInventoryTransactions()
	{
		using namespace F4T::Drops;
		const auto directory = UniqueDirectory();
		std::filesystem::create_directories(directory);
		const auto path = directory / "transactions.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		const auto catalog = Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "123" }, { "count", 5u } } });
		const InventoryPosition position{ "CAFE", "F00D", 1, 2, 3 };
		const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", "drop" },
			{ "expectedRevision", 0u }, { "itemKey", "test.esm:00000123" }, { "count", 5u } };
		const Json pickup{ { "type", "inventoryPickupRequest" }, { "operationId", "pickup" },
			{ "expectedRevision", 0u }, { "entityId", kFirstDroppedEntityId } };
		auto clock = [] { return 1000.0; };
		auto id = [] { return "0123456789abcdef0123456789abcdef"; };
		bool failPostCommit = false;
		try {
			DurableStateFileStore store(path, clock, id, [&] { if (failPostCommit) throw std::runtime_error("post-commit fault"); });
			F4T::Identity::TokenHash hash{};
			for (const std::string profile : { "a", "b", "c" }) {
				store.SaveResumeCredential(profile, hash, std::nullopt, true, 5000.0);
				(void)store.InitializeInventory(profile, profile == "a" ? catalog : Json::array());
			}
			(void)ReconcileStartingGrantFixture(store, "a");
			const auto before = ReadText(path);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			bool threw = false;
			try { (void)store.DropInventory("a", drop, position); }
			catch (const std::exception&) { threw = true; }
			CHECK(threw && ReadText(path) == before && store.GetInventory("a")->at("revision") == 0);
			CHECK(store.GetDropSnapshot()->empty());
			std::filesystem::remove_all(backup);
			failPostCommit = true;
			const auto committed = store.DropInventory("a", drop, position);
			CHECK(committed.outcome == Outcome::Committed && store.TakePersistenceWarning());
			CHECK(store.GetInventory("a")->at("items").empty());
			const auto projection = store.GetDropSnapshot();
			CHECK(projection && projection->size() == 1);
			CHECK(DecodeStableDroppedObject(projection->begin()->second)->item == committed.packet.at("drop").at("item"));
			failPostCommit = false;
			const auto droppedDisk = ReadText(path);
			CHECK(store.DropInventory("a", drop, position).packet == committed.packet);
			CHECK(ReadText(path) == droppedDisk);
			DurableStateFileStore restarted(path, clock, id);
			CHECK(restarted.DropInventory("a", drop, position).packet == committed.packet);
			CHECK(restarted.GetDropSnapshot() == projection);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			threw = false;
			try { (void)restarted.PickupInventory("b", pickup, position); }
			catch (const std::exception&) { threw = true; }
			CHECK(threw && ReadText(path) == droppedDisk && restarted.GetInventory("b")->at("items").empty());
			CHECK(restarted.GetDropSnapshot() == projection);
			std::filesystem::remove_all(backup);
			TransactionResult first, second;
			std::thread one([&] { first = restarted.PickupInventory("b", pickup, position); });
			std::thread two([&] { second = restarted.PickupInventory("c", pickup, position); });
			one.join(); two.join();
			CHECK((first.outcome == Outcome::Committed) != (second.outcome == Outcome::Committed));
			const auto winner = first.outcome == Outcome::Committed ? "b" : "c";
			const auto loser = first.outcome == Outcome::Committed ? "c" : "b";
			const auto result = first.outcome == Outcome::Committed ? first : second;
			CHECK(restarted.GetInventory(winner)->at("items").at("test.esm:00000123").at("count") == 5);
			CHECK(restarted.GetInventory(loser)->at("items").empty());
			CHECK(restarted.GetDropSnapshot()->empty());
			const auto pickedDisk = ReadText(path);
			DurableStateFileStore finalRestart(path, clock, id);
			CHECK(finalRestart.PickupInventory(winner, pickup, position).packet == result.packet);
			CHECK(finalRestart.DropInventory("a", drop, position).packet == committed.packet);
			CHECK(ReadText(path) == pickedDisk);
			{
					const auto tradeDirectory = UniqueDirectory();
					std::filesystem::create_directories(tradeDirectory);
					const auto tradePath = tradeDirectory / "trade.json";
					const auto tradeBackup = std::filesystem::path{ tradePath.string() + ".bak" };
					DurableStateFileStore tradeStore(tradePath, clock, id);
					F4T::Identity::TokenHash tradeHash{};
					for (const std::string profile : { "a", "b" }) {
						tradeStore.SaveResumeCredential(profile, tradeHash, std::nullopt, true, 5000.0);
						(void)tradeStore.InitializeInventory(profile, profile == "a" ? catalog : Json::array());
					}
					(void)ReconcileStartingGrantFixture(tradeStore, "a");
					const F4T::Trade::CommitRequest trade{ "trade-1000-1",
						F4T::Trade::CommitParty{ "a", 0u, { F4T::Trade::Line{ "test.esm:00000123", 2u } } },
						F4T::Trade::CommitParty{ "b", 0u, {} } };
					const auto tradeBefore = ReadText(tradePath);
					std::filesystem::remove(tradeBackup);
					std::filesystem::create_directories(tradeBackup / "occupied");
					threw = false;
					try { (void)tradeStore.TransferTrade(trade); }
					catch (const std::exception&) { threw = true; }
					CHECK(threw);
					CHECK(ReadText(tradePath) == tradeBefore);
					CHECK(tradeStore.GetInventory("a")->at("revision") == 0 && tradeStore.GetInventory("a")->at("items").at("test.esm:00000123").at("count") == 5);
					CHECK(tradeStore.GetInventory("b")->at("items").empty());
					std::filesystem::remove_all(tradeBackup);
					const auto committed = tradeStore.TransferTrade(trade);
					CHECK(committed.outcome == Outcome::Committed && committed.firstRevision == 1u && committed.secondRevision == 1u);
					CHECK(tradeStore.GetInventory("a")->at("items").at("test.esm:00000123").at("count") == 3);
					CHECK(tradeStore.GetInventory("b")->at("items").at("test.esm:00000123").at("count") == 2);
					std::error_code ignored;
					std::filesystem::remove_all(tradeDirectory, ignored);
				}
				auto oversized = Json::parse(droppedDisk);
			oversized["drops"]["live"] = Json::object();
			for (const std::string owner : { "a", "b" }) {
				oversized["profiles"][owner]["inventory"]["revision"] = 200u;
				oversized["profiles"][owner]["inventoryOperations"] = Json::array();
			}
			for (std::uint32_t index = 0; index < 400; ++index) {
				const auto owner = index < 200 ? "a" : "b";
				const auto revision = index % 200;
				auto object = committed.packet.at("drop");
				object["ownerProfileId"] = owner;
				object["item"]["plugin"] = std::string(128, 'a');
				object["item"]["count"] = 1u;
				object["itemKey"] = std::string(128, 'a') + ":00000123";
				oversized["drops"]["live"][std::to_string(kFirstDroppedEntityId + index)] = object;
				if (revision < 136) continue;
				auto request = drop;
				request["operationId"] = "old-" + std::to_string(index);
				request["expectedRevision"] = revision;
				request["itemKey"] = object.at("itemKey");
				request["count"] = 1u;
				auto historicalResult = committed.packet;
				historicalResult["operationId"] = request.at("operationId");
				historicalResult["revision"] = revision + 1;
				historicalResult["entityId"] = kFirstDroppedEntityId + index;
				historicalResult["drop"] = object;
				oversized["profiles"][owner]["inventoryOperations"].push_back(Json{ { "request", request }, { "result", historicalResult } });
			}
			oversized["drops"]["nextEntityId"] = kFirstDroppedEntityId + 400;
			F4T::DurableState::ValidateRoot(oversized);
			const auto oldPath = directory / "old-projection.json";
			WriteText(oldPath, oversized.dump());
			DurableStateFileStore oldStore(oldPath, clock, id);
			CHECK(!oldStore.GetDropSnapshot() && Json::parse(ReadText(oldPath)) == oversized);
			auto shrink = pickup;
			shrink["operationId"] = "shrink";
			shrink["expectedRevision"] = 200u;
			CHECK(oldStore.PickupInventory("a", shrink, position).outcome == Outcome::Committed);
			CHECK(oldStore.GetDropSnapshot()->size() == 399);
			WriteText(path, "corrupt");
			DurableStateFileStore recovered(path, clock, id);
			CHECK(recovered.RecoveryNotice());
			CHECK(!recovered.GetDropSnapshot());
			CHECK(recovered.DropInventory("a", drop, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			CHECK(recovered.PickupInventory(winner, pickup, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			DurableStateFileStore recoveredRestart(path, clock, id);
			CHECK(recoveredRestart.DropInventory("a", drop, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			CHECK(!recoveredRestart.GetDropSnapshot());
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}

	void TestLiveDropConsumption()
	{
		using namespace F4T::Drops;
		const auto directory = UniqueDirectory();
		std::filesystem::create_directories(directory);
		const auto path = directory / "live-drops.json";
		const auto catalog = Json::array({ Json{ { "plugin", "commonwealthonline.esp" }, { "localFormId", "997F" }, { "count", 1u } } });
		const InventoryPosition position{ "CAFE", "F00D", 1, 2, 3 };
		const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", "kit-drop" },
			{ "expectedRevision", 0u }, { "itemKey", "commonwealthonline.esp:0000997F" }, { "count", 1u } };
		auto clock = [] { return 1000.0; };
		auto id = [] { return "0123456789abcdef0123456789abcdef"; };
		try {
			{
				DurableStateFileStore store(path, clock, id);
				F4T::Identity::TokenHash hash{};
				store.SaveResumeCredential("a", hash, std::nullopt, true, 5000.0);
				(void)store.InitializeInventory("a", catalog);
				(void)ReconcileStartingGrantFixture(store, "a");
				CHECK(store.DropInventory("a", drop, position).outcome == Outcome::Committed);
			}
			{
				DurableStateFileStore rejectedCamp(path, clock, id);
				CHECK(rejectedCamp.GetLiveDrop(kFirstDroppedEntityId).at("ownerProfileId") == "a");
				CHECK(rejectedCamp.GetLiveDrop(kFirstDroppedEntityId).at("itemKey") == "commonwealthonline.esp:0000997F");
			}
			bool failPostCommit = false;
			DurableStateFileStore store(path, clock, id, [&] { if (failPostCommit) throw std::runtime_error("post-commit fault"); });
			store.SetDeferCommits(true);
			const auto live = store.GetLiveDrop(kFirstDroppedEntityId);
			const auto oldWorkshop = store.GetWorkshop();
			const auto oldSettlements = store.GetSettlementAccess();
			const auto newWorkshop = Json{ { "camps", Json::array({ Json{ { "id", 1 } } }) } };
			const auto newSettlements = Json::array({ Json{ { "buildZoneId", 1 } } });
			const auto backup = std::filesystem::path{ path.string() + ".bak" };
			const auto diskBefore = ReadText(path);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			bool threw = false;
			try { (void)store.CommitCamp(kFirstDroppedEntityId, live, newWorkshop, newSettlements, "a", kFirstDroppedEntityId, {}); }
			catch (const std::exception&) { threw = true; }
			CHECK(threw && store.GetLiveDrop(kFirstDroppedEntityId) == live);
			CHECK(store.GetWorkshop() == oldWorkshop && store.GetSettlementAccess() == oldSettlements);
			CHECK(ReadText(path) == diskBefore);
			std::filesystem::remove_all(backup);
			CHECK(store.CommitCamp(kFirstDroppedEntityId, Json::object(), newWorkshop, newSettlements, "a", kFirstDroppedEntityId, {}).outcome != F4T::CampGrants::Outcome::Committed);
			failPostCommit = true;
			CHECK(store.CommitCamp(kFirstDroppedEntityId, live, newWorkshop, newSettlements, "a", kFirstDroppedEntityId, {}).outcome == F4T::CampGrants::Outcome::Committed);
			CHECK(store.TakePersistenceWarning());
			CHECK(store.GetWorkshop() == newWorkshop && store.GetSettlementAccess() == newSettlements);
			DurableStateFileStore committed(path, clock, id);
			CHECK(committed.GetLiveDrop(kFirstDroppedEntityId).is_null());
			CHECK(committed.GetWorkshop() == newWorkshop && committed.GetSettlementAccess() == newSettlements);
			CHECK(!store.ConsumeLiveDrop(kFirstDroppedEntityId));
			CHECK(store.GetLiveDrop(kFirstDroppedEntityId).is_null());
			DurableStateFileStore restarted(path, clock, id);
			CHECK(restarted.GetLiveDrop(kFirstDroppedEntityId).is_null());
			CHECK(!restarted.ConsumeLiveDrop(kFirstDroppedEntityId));
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}

#ifdef __linux__
	void CheckLinuxWriteFailures()
	{
		for (const unsigned count : { 1u, 2u, 4u, 8u, 16u }) {
			const auto directory = UniqueDirectory();
			std::filesystem::create_directories(directory);
			const auto path = directory / "durable-state.json";
			const auto backup = std::filesystem::path{ path.string() + ".bak" };
			const std::string serverId = "0123456789abcdef0123456789abcdef";
			DurableStateFileStore store(path, [] { return 1000.; }, [&] { return serverId; });
			std::unordered_map<std::string, Json> oldStates, newStates;
			F4T::Identity::TokenHash current{}, previous{};
			current.fill(1); previous.fill(2);
			for (unsigned index = 0; index < count; ++index) {
				const auto profile = "write-profile-" + std::to_string(index);
				store.SaveResumeCredential(profile, current, previous, true, 5000);
				oldStates.emplace(profile, Json{ { "type", "playerState" }, { "characterName", "old-" + std::to_string(index) } });
				newStates.emplace(profile, Json{ { "type", "playerState" }, { "characterName", "new-" + std::to_string(index) } });
			}
			CHECK(store.UpdatePlayerStates(oldStates));
			store.UpdateServerWorldState(Json{ { "timeHHmm", "1200" } });
			const auto primaryBefore = ReadText(path), backupBefore = ReadText(backup);
			CHECK(primaryBefore.size() > 128);
			struct rlimit original{};
			CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0 && original.rlim_cur >= 128);
			struct sigaction ignored{}, originalSignal{};
			ignored.sa_handler = SIG_IGN;
			CHECK(sigemptyset(&ignored.sa_mask) == 0);
			CHECK(sigaction(SIGXFSZ, &ignored, &originalSignal) == 0);
			auto restore = [&](struct rlimit*) {
				const int restoredLimit = setrlimit(RLIMIT_FSIZE, &original);
				const int restoredSignal = sigaction(SIGXFSZ, &originalSignal, nullptr);
				if (restoredLimit != 0 || restoredSignal != 0) std::abort();
			};
			std::unique_ptr<struct rlimit, decltype(restore)> restoration(&original, restore);
			auto limited = original;
			limited.rlim_cur = 128;
			CHECK(setrlimit(RLIMIT_FSIZE, &limited) == 0);
			CHECK(store.UpdatePlayerStates(newStates));
			store.WaitForCommits();
			const auto failure = store.TakePersistenceWarning().value_or("");
			restoration.reset();
			struct rlimit restored{};
			CHECK(getrlimit(RLIMIT_FSIZE, &restored) == 0 && restored.rlim_cur == original.rlim_cur && restored.rlim_max == original.rlim_max);
			struct sigaction restoredSignal{};
			CHECK(sigaction(SIGXFSZ, nullptr, &restoredSignal) == 0 && restoredSignal.sa_handler == originalSignal.sa_handler);
			CHECK(failure.starts_with("write failed for '" + path.string() + ".tmp-") && failure.ends_with("' (code " + std::to_string(EFBIG) + ")"));
			CHECK(ReadText(path) == primaryBefore && ReadText(backup) == backupBefore && !store.TakePersistenceWarning());
			for (const auto& entry : std::filesystem::directory_iterator(directory))
				CHECK(entry.path() == path || entry.path() == backup);
			auto verify = [&](DurableStateFileStore& checked, const auto& states) {
				CHECK(checked.ServerInstanceId() == serverId && !checked.RecoveryNotice());
				CHECK(checked.GetServerWorldState() == Json({ { "timeHHmm", "1200" } }));
				for (const auto& [profile, expected] : states) {
					CHECK(checked.GetPlayerState(profile) == expected);
					const auto credential = checked.GetResumeCredential(profile);
					CHECK(credential && credential->tokenHash == current && credential->previousTokenHash == previous && credential->confirmed && credential->expiresAt == 5000);
				}
			};
			verify(store, newStates);
			DurableStateFileStore reopened(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
			verify(reopened, oldStates);
			CHECK(store.UpdatePlayerStates(newStates));
			store.WaitForCommits();
			verify(store, newStates);
			DurableStateFileStore retried(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
			verify(retried, newStates);
			CHECK(ReadText(backup) == primaryBefore && !store.TakePersistenceWarning());
			for (const auto& entry : std::filesystem::directory_iterator(directory))
				CHECK(entry.path() == path || entry.path() == backup);
			std::cout << Json{ { "profiles", count }, { "failure", "RLIMIT_FSIZE" }, { "error_code", EFBIG },
				{ "primary_before", Json::parse(primaryBefore) }, { "backup_before", Json::parse(backupBefore) },
				{ "primary_after_retry", Json::parse(ReadText(path)) }, { "pass", true } }.dump() << '\n';
			std::filesystem::remove_all(directory);
		}
	}
#endif

#ifdef _WIN32
	void CheckWindowsReplacementFailures()
	{
		for (const unsigned count : { 1u, 2u, 4u, 8u, 16u }) {
			for (const bool blockPrimary : { false, true }) {
				const auto directory = UniqueDirectory();
				std::filesystem::create_directories(directory);
				const auto path = directory / "durable-state.json";
				const auto backup = std::filesystem::path{ path.string() + ".bak" };
				const std::string serverId = "0123456789abcdef0123456789abcdef";
				DurableStateFileStore store(path, [] { return 1000.; }, [&] { return serverId; });
				std::unordered_map<std::string, Json> oldStates, newStates;
				F4T::Identity::TokenHash current{}, previous{};
				current.fill(1); previous.fill(2);
				for (unsigned index = 0; index < count; ++index) {
					const auto profile = "sharing-profile-" + std::to_string(index);
					store.SaveResumeCredential(profile, current, previous, true, 5000);
					oldStates.emplace(profile, Json{ { "type", "playerState" }, { "characterName", "old-" + std::to_string(index) } });
					newStates.emplace(profile, Json{ { "type", "playerState" }, { "characterName", "new-" + std::to_string(index) } });
				}
				CHECK(store.UpdatePlayerStates(oldStates));
				store.UpdateServerWorldState(Json{ { "timeHHmm", "1200" } });
				const auto primaryBefore = ReadText(path), backupBefore = ReadText(backup);
				const auto blocked = blockPrimary ? path : backup;
				std::unique_ptr<void, decltype(&CloseHandle)> handle(
					CreateFileW(blocked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &CloseHandle);
				CHECK(handle.get() != INVALID_HANDLE_VALUE);
				CHECK(store.UpdatePlayerStates(newStates));
				store.WaitForCommits();
				const auto failure = store.TakePersistenceWarning().value_or("");
				const auto errorPrefix = "MoveFileExW failed for '" + blocked.string() + "' (code ";
				const bool accessDenied = failure == errorPrefix + std::to_string(ERROR_ACCESS_DENIED) + ")";
				CHECK(accessDenied || failure == errorPrefix + std::to_string(ERROR_SHARING_VIOLATION) + ")");
				CHECK(ReadText(path) == primaryBefore);
				CHECK(ReadText(backup) == (blockPrimary ? primaryBefore : backupBefore));
				CHECK(!store.TakePersistenceWarning());
				for (const auto& entry : std::filesystem::directory_iterator(directory))
					CHECK(entry.path() == path || entry.path() == backup);
				auto verify = [&](DurableStateFileStore& checked, const auto& states) {
					CHECK(checked.ServerInstanceId() == serverId && !checked.RecoveryNotice());
					CHECK(checked.GetServerWorldState() == Json({ { "timeHHmm", "1200" } }));
					for (const auto& [profile, expected] : states) {
						CHECK(checked.GetPlayerState(profile) == expected);
						const auto credential = checked.GetResumeCredential(profile);
						CHECK(credential && credential->tokenHash == current && credential->previousTokenHash == previous && credential->confirmed && credential->expiresAt == 5000);
					}
				};
				verify(store, newStates);
				DurableStateFileStore reopened(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				verify(reopened, oldStates);
				const auto backupAfterFailure = ReadText(backup);
				handle.reset();
				CHECK(store.UpdatePlayerStates(newStates));
				store.WaitForCommits();
				verify(store, newStates);
				DurableStateFileStore retried(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				verify(retried, newStates);
				CHECK(ReadText(backup) == primaryBefore && !store.TakePersistenceWarning());
				for (const auto& entry : std::filesystem::directory_iterator(directory))
					CHECK(entry.path() == path || entry.path() == backup);
				std::cout << Json{ { "profiles", count }, { "blocked", blockPrimary ? "primary" : "backup" },
					{ "error_code", accessDenied ? ERROR_ACCESS_DENIED : ERROR_SHARING_VIOLATION }, { "primary_before", Json::parse(primaryBefore) },
					{ "backup_before", Json::parse(backupBefore) }, { "backup_after_failure", Json::parse(backupAfterFailure) },
					{ "primary_after_retry", Json::parse(ReadText(path)) }, { "pass", true } }.dump() << '\n';
				std::filesystem::remove_all(directory);
			}
		}
	}
#endif

	void TestSteamIdentitiesRoundTrip()
	{
		const auto directory = UniqueDirectory();
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const std::string serverId = "0123456789abcdef0123456789abcdef";
		const Json section{ { "records", Json::array({ Json{ { "profileId", "profile-one" }, { "steamId", "76561198000000042" },
			{ "source", "steam-web-api" }, { "firstSeen", 1.0 }, { "lastSeen", 2.0 }, { "nicknames", Json::array({ "Nomad" }) } } }) },
			{ "flags", Json::array() } };
		{
			std::atomic<double> clock{ 1000.0 };
			DurableStateFileStore store(path, [&] {
				const double next = clock.load() + 1.0;
				clock.store(next);
				return next;
			}, [&] { return serverId; });
			assert(store.GetSteamIdentities().is_null());
			store.SaveSteamIdentities(section);
			store.WaitForCommits();
			const auto written = ReadText(path);
			store.SaveSteamIdentities(section);
			store.WaitForCommits();
			CHECK(ReadText(path) == written);
			CHECK(store.GetSteamIdentities() == section);
		}
		{
			DurableStateFileStore reloaded(path, [] { return 2000.; }, [&] { return serverId; });
			CHECK(reloaded.GetSteamIdentities() == section);
		}
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
	}
}

void TestDeathIntentPersistence()
{
	using F4T::DeathIntent::Outcome;
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	const auto path = directory / "intent.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	const auto id = [] { return "0123456789abcdef0123456789abcdef"; };
	const std::string deathId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const Json record{ { "version", 1u }, { "profileId", "profile-a#0" }, { "characterId", "quest-11111111111111111111111111111111" }, { "inventoryRevision", 0u },
		{ "cellKey", "test.esm:00000001" }, { "worldspaceKey", nullptr }, { "position", Json::array({ 1.0, 2.0, 3.0 }) },
		{ "state", "dead" }, { "deadlineUnix", nullptr }, { "locationSource", "client-reported" } };
	{
		DurableStateFileStore store(path, [] { return 1000.0; }, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "First" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		const auto inventory = store.InitializeInventory("profile-a#0", Json::array());
		const auto before = ReadText(path);
		const auto legacy = F4T::DurableState::LoadAndMigrate(Json::parse(before), 1000.0);
		CHECK(!legacy.root.contains("deathIntents"));
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		bool failed = false;
		try { (void)store.RecordDeathIntent(deathId, record); } catch (const std::exception&) { failed = true; }
		CHECK(failed && !store.GetDeathIntent(deathId) && ReadText(path) == before);
		std::filesystem::remove_all(backup);
		store.SetDeferCommits(true);
		CHECK(store.RecordDeathIntent(deathId, record) == Outcome::Created);
		CHECK(store.RecordDeathIntent(deathId, record) == Outcome::Replayed);
		auto changed = record;
		changed["position"][0] = 4.0;
		CHECK(store.RecordDeathIntent(deathId, changed) == Outcome::Conflict);
		auto stale = record;
		stale["inventoryRevision"] = 1u;
		CHECK(store.RecordDeathIntent("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", stale) == Outcome::Rejected);
		auto unsupported = record;
		unsupported["instanceData"] = Json::object();
		CHECK(store.RecordDeathIntent("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", unsupported) == Outcome::Rejected);
		Outcome first = Outcome::Rejected;
		Outcome second = Outcome::Rejected;
		const std::string concurrentId = "cccccccccccccccccccccccccccccccc";
		std::thread one([&] { first = store.RecordDeathIntent(concurrentId, record); });
		std::thread two([&] { second = store.RecordDeathIntent(concurrentId, changed); });
		one.join();
		two.join();
		CHECK((first == Outcome::Created && second == Outcome::Conflict) ||
			(first == Outcome::Conflict && second == Outcome::Created));
		CHECK(store.GetInventory("profile-a#0") == inventory);
		DurableStateFileStore reopened(path, [] { return 1000.0; }, id);
		CHECK(reopened.GetDeathIntent(deathId) == record);
		CHECK(reopened.RecordDeathIntent(deathId, record) == Outcome::Replayed);
		CHECK(reopened.GetInventory("profile-a#0") == inventory);
		auto invalid = Json::parse(ReadText(path));
		invalid["deathIntents"][deathId]["unexpected"] = true;
		bool refused = false;
		try { F4T::DurableState::ValidateRoot(invalid); } catch (const std::invalid_argument&) { refused = true; }
		CHECK(refused);
		CHECK(store.RecordDeathIntent("dddddddddddddddddddddddddddddddd", record) == Outcome::Created);
		WriteText(path, "corrupt");
		DurableStateFileStore recovered(path, [] { return 1000.0; }, id);
		CHECK(recovered.RecoveryNotice());
		CHECK(recovered.InventoryAuthorityBlocked());
		CHECK(recovered.GetDeathIntent(deathId) == record);
		CHECK(recovered.RecordDeathIntent("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", record) == Outcome::Rejected);
	}
	std::filesystem::remove_all(directory);
}

void TestServerAllocatedDeathIntent()
{
	using F4T::DeathIntent::Outcome;
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	const auto path = directory / "allocated-intent.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	const auto id = [] { return "0123456789abcdef0123456789abcdef"; };
	const Json record{ { "version", 1u }, { "profileId", "profile-a#0" }, { "characterId", "quest-11111111111111111111111111111111" }, { "inventoryRevision", 0u },
		{ "cellKey", "test.esm:00000001" }, { "worldspaceKey", nullptr }, { "position", Json::array({ 1.0, 2.0, 3.0 }) },
		{ "state", "dead" }, { "deadlineUnix", nullptr }, { "locationSource", "client-reported" } };
	{
		DurableStateFileStore store(path, [] { return 1000.0; }, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "First" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		const auto inventory = store.InitializeInventory("profile-a#0", Json::array());
		const auto before = ReadText(path);
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		bool failed = false;
		try { (void)store.AllocateDeathIntent(record); } catch (const std::exception&) { failed = true; }
		CHECK(failed && ReadText(path) == before);
		std::filesystem::remove_all(backup);
		const auto created = store.AllocateDeathIntent(record);
		CHECK(created.outcome == Outcome::Created && created.generation == 1 && created.deathId.size() == 32);
		CHECK(store.GetDeathIntent(created.deathId) == record);
		auto transactionRoot = Json::parse(ReadText(path));
		const std::string retainedKey = "fallout4.esm:00021B3B";
		const std::string movedKey = "test.esm:00000002";
		transactionRoot["profiles"]["profile-a#0"]["inventory"]["items"] = F4T::DurableState::NormalizeStartingInventory(Json::array({
			Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "2" }, { "count", 5u } } }));
		CHECK(F4T::DeathBag::CreateEmpty(transactionRoot, created.deathId, 1000.0));
		const auto health = F4T::ItemHealthSnapshot::Encode(0.75f);
		CHECK(health);
		auto healthRoot = transactionRoot;
		const Json healthStacks = Json::array({ Json{ { "itemKey", movedKey }, { "count", 5u }, { "metadata", *health } } });
		CHECK(F4T::DeathBag::Deposit(healthRoot, created.deathId, healthStacks));
		CHECK(F4T::DeathBag::Place(healthRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111"));
		CHECK(healthRoot["deathBags"][created.deathId]["stacks"][0]["metadata"] == *health);
		const auto beforeHealthWithdrawal = healthRoot;
		CHECK(!F4T::DeathBag::Withdraw(healthRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", false, movedKey, 1, 1, "health", 1500.0));
		CHECK(healthRoot == beforeHealthWithdrawal);
		Json metadata;
		CHECK(!F4T::DeathBag::Withdraw(healthRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", false, movedKey, 1, 1, "health", 1500.0, &metadata));
		CHECK(healthRoot == beforeHealthWithdrawal);
		CHECK(F4T::DeathBag::Withdraw(healthRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", false, movedKey, 5, 1, "health", 1500.0, &metadata));
		CHECK(metadata == *health && !healthRoot.at("deathBags").contains(created.deathId) && healthRoot.at("deathGenerations").at("quest-11111111111111111111111111111111").at("bagSpent") == true);
		const auto grantKey = "bag:" + created.deathId + ":health";
		CHECK(healthRoot["profiles"]["profile-a#0"]["deathBagInstances"][grantKey]["metadata"] == *health);
		CHECK(healthRoot["profiles"]["profile-a#0"]["deathBagInstances"][grantKey]["state"] == "pending");
		const auto pendingGrants = F4T::DeathBag::ReconcileGrants(healthRoot, "profile-a#0", "quest-11111111111111111111111111111111");
		CHECK(pendingGrants && pendingGrants->size() == 1 && pendingGrants->at(0).at("state") == "pending");
		CHECK(!F4T::DeathBag::ReconcileGrants(healthRoot, "profile-a#0", "quest-22222222222222222222222222222222"));
		auto restarted = Json::parse(healthRoot.dump());
		const auto recoveredGrants = F4T::DeathBag::ReconcileGrants(restarted, "profile-a#0", "quest-11111111111111111111111111111111");
		CHECK(recoveredGrants && recoveredGrants->at(0).at("metadata") == *health && recoveredGrants->at(0).at("state") == "pending");
		auto blocked = healthRoot;
		blocked["inventoryAuthorityBlocked"] = true;
		CHECK(!F4T::DeathBag::MarkGrantAttempted(blocked, "profile-a#0", "quest-11111111111111111111111111111111", created.deathId, "health", 1500.0));
		CHECK(blocked["profiles"]["profile-a#0"]["deathBagInstances"][grantKey]["state"] == "pending");
		CHECK(!F4T::DeathBag::MarkGrantAttempted(healthRoot, "profile-a#0", "quest-22222222222222222222222222222222", created.deathId, "health", 1500.0));
		CHECK(F4T::DeathBag::MarkGrantAttempted(healthRoot, "profile-a#0", "quest-11111111111111111111111111111111", created.deathId, "health", 1500.0));
		CHECK(F4T::DeathBag::MarkGrantAttempted(healthRoot, "profile-a#0", "quest-11111111111111111111111111111111", created.deathId, "health", 1500.0));
		CHECK(!healthRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(grantKey) && healthRoot.at("deathBagGrantHistory").at("profile-a#0").at(grantKey).at("state") == "attempted");
		auto insufficient = healthRoot;
		insufficient["profiles"]["profile-a#0"]["inventory"]["items"].erase(movedKey);
		F4T::DurableState::ValidateRoot(insufficient);
		F4T::DurableState::ValidateRoot(healthRoot);
		auto unsupportedRoot = transactionRoot;
		auto unsupportedStacks = healthStacks;
		unsupportedStacks[0]["metadata"]["unknown"] = true;
		CHECK(!F4T::DeathBag::Deposit(unsupportedRoot, created.deathId, unsupportedStacks));
		CHECK(unsupportedRoot == transactionRoot);
		const Json stacks = Json::array({ Json{ { "itemKey", movedKey }, { "count", 5u }, { "metadata", nullptr } } });
		CHECK(F4T::DeathBag::Deposit(transactionRoot, created.deathId, stacks));
		CHECK(F4T::DeathBag::Place(transactionRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111"));
		CHECK(!F4T::DeathBag::Deposit(transactionRoot, created.deathId, stacks));
		CHECK(transactionRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(retainedKey));
		CHECK(!transactionRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(movedKey));
		CHECK(transactionRoot["deathBags"][created.deathId]["items"][movedKey]["count"] == 5u);
		const auto visible = F4T::DeathBag::Discovery(transactionRoot, "profile-a#0", "quest-11111111111111111111111111111111", false);
		CHECK(visible && visible->size() == 1 && visible->at(0).at("deathId") == created.deathId && visible->at(0).at("canTake") == true);
		CHECK(!visible->at(0).contains("items") && !visible->at(0).contains("ownerProfileId"));
		CHECK(!F4T::DeathBag::Discovery(transactionRoot, "profile-a#0", "quest-22222222222222222222222222222222", true));
		CHECK(!F4T::DeathBag::Withdraw(transactionRoot, created.deathId, "profile-a#0", "quest-22222222222222222222222222222222", false, movedKey, 2, 1, "one", 1500.0));
		CHECK(F4T::DeathBag::Withdraw(transactionRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", false, movedKey, 2, 1, "one", 1500.0));
		CHECK(F4T::DeathBag::Withdraw(transactionRoot, created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", false, movedKey, 2, 1, "one", 1500.0));
		CHECK(transactionRoot["deathBags"][created.deathId]["items"][movedKey]["count"] == 3u);
		CHECK(transactionRoot["profiles"]["profile-a#0"]["inventory"]["items"][movedKey]["count"] == 2u);
		const std::string other = "quest-22222222222222222222222222222222";
		CHECK(F4T::CharacterSlots::SetSlotRecord(transactionRoot, "profile-a", 1, Json{ { "name", "Other" }, { "questProfile", other } }));
		CHECK(F4T::DurableState::InitializeInventory(transactionRoot, "profile-a#1", Json::array(), 1000.0));
		const auto otherView = F4T::DeathBag::Discovery(transactionRoot, "profile-a#1", other, false);
		CHECK(otherView && otherView->empty());
		const auto otherLootView = F4T::DeathBag::Discovery(transactionRoot, "profile-a#1", other, true);
		CHECK(otherLootView && otherLootView->size() == 1 && otherLootView->at(0).at("deathId") == created.deathId && otherLootView->at(0).at("canTake") == true);
		CHECK(!F4T::DeathBag::Withdraw(transactionRoot, created.deathId, "profile-a#1", other, false, movedKey, 1, 0, "other", 1500.0));
		CHECK(F4T::DeathBag::Withdraw(transactionRoot, created.deathId, "profile-a#1", other, true, movedKey, 1, 0, "other", 1500.0));
		CHECK(transactionRoot["deathBags"][created.deathId]["items"][movedKey]["count"] == 2u);
		CHECK(transactionRoot["profiles"]["profile-a#1"]["inventory"]["items"][movedKey]["count"] == 1u);
		F4T::DurableState::ValidateRoot(transactionRoot);
		CHECK(store.CreateEmptyDeathBag(created.deathId));
		CHECK(!store.CreateEmptyDeathBag(created.deathId));
		const auto bag = store.GetDeathBag(created.deathId);
		CHECK(bag && bag->at("items").empty() && bag->at("ownerProfileId") == "profile-a#0");
		CHECK(!store.CreateAndDepositDeathBag(created.deathId, "profile-a#0", "quest-11111111111111111111111111111111", stacks));
		CHECK(store.GetDeathBag(created.deathId) == bag);
		CHECK(F4T::DeathBag::CanTake(*bag, "quest-11111111111111111111111111111111", false));
		CHECK(!F4T::DeathBag::CanTake(*bag, "quest-22222222222222222222222222222222", false));
		CHECK(F4T::DeathBag::CanTake(*bag, "quest-22222222222222222222222222222222", true));
		CHECK(!F4T::DeathBag::CanTake(*bag, "", true));
		auto staleRoot = Json::parse(ReadText(path));
		staleRoot["profiles"]["profile-a#0"]["inventory"]["revision"] = 1u;
		const auto beforeStale = staleRoot;
		CHECK(F4T::DeathIntent::Record(staleRoot, created.deathId, record) == Outcome::Rejected);
		CHECK(F4T::DeathIntent::Allocate(staleRoot, record).outcome == Outcome::Rejected);
		CHECK(staleRoot == beforeStale);
		const auto replay = store.AllocateDeathIntent(record);
		CHECK(replay.outcome == Outcome::Replayed && replay.deathId == created.deathId && replay.generation == created.generation);
		auto tampered = Json::parse(ReadText(path));
		tampered["deathGenerations"]["quest-11111111111111111111111111111111"]["number"] = 2u;
		bool invalid = false;
		try { F4T::DurableState::ValidateRoot(tampered); } catch (const std::invalid_argument&) { invalid = true; }
		CHECK(invalid);
		auto changed = record;
		changed["position"][0] = 4.0;
		CHECK(store.AllocateDeathIntent(changed).outcome == Outcome::Conflict);
		CHECK(store.GetInventory("profile-a#0") == inventory);
		DurableStateFileStore reopened(path, [] { return 1000.0; }, id);
		CHECK(reopened.GetDeathBag(created.deathId) == bag);
		const auto restored = reopened.AllocateDeathIntent(record);
		CHECK(restored.outcome == Outcome::Replayed && restored.deathId == created.deathId);
		CHECK(reopened.GetInventory("profile-a#0") == inventory);
		auto replacement = record;
		replacement["characterId"] = "quest-22222222222222222222222222222222";
		CHECK(reopened.AllocateDeathIntent(replacement).outcome == Outcome::Rejected);
		CHECK(reopened.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Replacement" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
		CHECK(reopened.AllocateDeathIntent(record).outcome == Outcome::Rejected);
		CHECK(reopened.RecordDeathIntent(created.deathId, record) == Outcome::Rejected);
		const auto replacementIntent = reopened.AllocateDeathIntent(replacement);
		CHECK(replacementIntent.outcome == Outcome::Created && replacementIntent.generation == 1 && replacementIntent.deathId != created.deathId);
		CHECK(reopened.GetActiveDeathIntentId("quest-11111111111111111111111111111111") == created.deathId);
		CHECK(reopened.GetActiveDeathIntentId("quest-22222222222222222222222222222222") == replacementIntent.deathId);
	}
	std::filesystem::remove_all(directory);
}

int main()
{
	TestDeathIntentPersistence();
	TestServerAllocatedDeathIntent();
	TestStartingInventory();
	TestInventoryTransactions();
	TestLiveDropConsumption();
	TestSteamIdentitiesRoundTrip();
#ifdef __linux__
	CheckLinuxWriteFailures();
#endif
#ifdef _WIN32
	CheckWindowsReplacementFailures();
#endif
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	const auto path = directory / "durable-state.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	std::atomic<double> now{ 1000.0 };
	const std::string serverId = "0123456789abcdef0123456789abcdef";

	try {
		{
			DurableStateFileStore store(
				path,
				[&] { return now.load(); },
				[&] { return serverId; });
			CHECK(store.ServerInstanceId() == serverId);
			CHECK(!store.RecoveryNotice().has_value());
			CHECK(std::filesystem::exists(path));

			F4T::Identity::TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>(index + 1);
			}
			now = 1010.0;
			store.SaveResumeCredential("profile-one", hash, std::nullopt, true, 5000.0);
			now = 1020.0;
			CHECK(store.UpdatePlayerState("profile-one", Json{
				{ "type", "playerState" },
				{ "characterName", "Nomad" },
				{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
			}));
			now = 1025.0;
			const auto writeTimeBefore = std::filesystem::last_write_time(path);
			CHECK(!store.UpdatePlayerState("profile-one", Json{
				{ "type", "playerState" },
				{ "characterName", "Nomad" },
				{ "actionEvents", Json::array({ Json{ { "sequence", 2 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
			}));
			const auto writeTimeAfter = std::filesystem::last_write_time(path);
			CHECK(writeTimeBefore == writeTimeAfter);
			now = 1030.0;
			store.UpdateServerWorldState(Json{
				{ "timeHHmm", "1800" },
				{ "weatherConsoleArg", "001ca7e4" },
				{ "ignored", "transient" }
			});
			CHECK(std::filesystem::exists(backup));
		}

		{
			DurableStateFileStore restarted(
				path,
				[&] { return now.load(); },
				[&] { return "ffffffffffffffffffffffffffffffff"; });
			CHECK(restarted.ServerInstanceId() == serverId);
			const auto resume = restarted.GetResumeCredential("profile-one");
			CHECK(resume.has_value());
			CHECK(resume->expiresAt == 5000.0);
			const auto player = restarted.GetPlayerState("profile-one");
			CHECK(player.has_value());
			CHECK(player->at("characterName") == "Nomad");
			CHECK(!player->contains("actionEvents"));
			const auto world = restarted.GetServerWorldState();
			CHECK(world.at("timeHHmm") == "1800");
			CHECK(!world.contains("ignored"));
		}

		WriteText(path, "{ truncated");
		{
			DurableStateFileStore recovered(
				path,
				[&] { return now.load(); },
				[&] { return "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"; });
			CHECK(recovered.RecoveryNotice().has_value());
			CHECK(recovered.ServerInstanceId() == serverId);
			CHECK(std::filesystem::exists(path));
			CHECK(std::filesystem::exists(backup));
		}

		std::filesystem::remove(path);
		{
			DurableStateFileStore missingPrimary(
				path,
				[&] { return now.load(); },
				[&] { return "dddddddddddddddddddddddddddddddd"; });
			CHECK(missingPrimary.RecoveryNotice().has_value());
			CHECK(missingPrimary.ServerInstanceId() == serverId);
			CHECK(std::filesystem::exists(path));
		}

		std::filesystem::remove(path);
		WriteText(backup, "{ torn backup");
		try {
			DurableStateFileStore tornBackup(
				path,
				[&] { return now.load(); },
				[&] { return "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; });
			(void)tornBackup;
			CHECK(false && "a missing primary with a torn backup must fail startup");
		} catch (const std::runtime_error&) {
		}
		CHECK(!std::filesystem::exists(path));
		CHECK(ReadText(backup) == "{ torn backup");

		WriteText(path, "bad primary");
		WriteText(backup, "bad backup");
		try {
			DurableStateFileStore invalid(
				path,
				[&] { return now.load(); },
				[&] { return serverId; });
			(void)invalid;
			CHECK(false && "invalid primary and backup must fail startup");
		} catch (const std::runtime_error&) {
		}

		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		const Json schema1{
			{ "schemaVersion", 1 },
			{ "serverId", serverId },
			{ "players", Json{ { "legacy-profile", Json{ { "playerState", Json{ { "characterName", "Legacy" } } } } } } },
			{ "worldState", Json{ { "timeHHmm", "0900" } } }
		};
		WriteText(path, schema1.dump(2));
		now = 2000.0;
		{
			DurableStateFileStore migrated(
				path,
				[&] { return now.load(); },
				[&] { return "cccccccccccccccccccccccccccccccc"; });
			CHECK(migrated.RecoveryNotice().has_value());
			CHECK(migrated.ServerInstanceId() == serverId);
			const auto player = migrated.GetPlayerState("legacy-profile");
			CHECK(player.has_value() && player->at("characterName") == "Legacy");
			CHECK(migrated.GetServerWorldState().at("timeHHmm") == "0900");
			CHECK(std::filesystem::exists(backup));
		}

		// A write that fails must leave memory agreeing with disk: the store used
		// to mutate first and persist second, so a failed write left a change
		// visible in memory that a later unrelated save would commit.
		{
			const auto faultDirectory = UniqueDirectory();
			std::filesystem::create_directories(faultDirectory);
			const auto faultPath = faultDirectory / "durable-state.json";
			DurableStateFileStore store(
				faultPath,
				[&] { return now.load(); },
				[&] { return serverId; });
			CHECK(store.UpdatePlayerState("fault-profile", Json{
				{ "type", "playerState" },
				{ "characterName", "Before" }
			}));
			const auto diskBefore = ReadText(faultPath);

			// Renaming the backup into place cannot succeed onto a populated
			// directory, so the persist fails after the mutation was applied.
			const auto faultBackup = std::filesystem::path{ faultPath.string() + ".bak" };
			std::filesystem::remove(faultBackup);
			std::filesystem::create_directories(faultBackup / "occupied");

			bool threw = false;
			try {
				(void)store.UpdatePlayerState("fault-profile", Json{
					{ "type", "playerState" },
					{ "characterName", "After" }
				});
			} catch (const std::exception&) {
				threw = true;
			}
			CHECK(threw);

			const auto retained = store.GetPlayerState("fault-profile");
			CHECK(retained.has_value() && retained->at("characterName") == "Before");
			CHECK(ReadText(faultPath) == diskBefore);

			std::filesystem::remove_all(faultBackup);
			std::error_code ignored;
			std::filesystem::remove_all(faultDirectory, ignored);
		}

		{
			const auto warningDirectory = UniqueDirectory();
			std::filesystem::create_directories(warningDirectory);
			const auto warningPath = warningDirectory / "durable-state.json";
			bool failPostCommit = false;
			DurableStateFileStore store(
				warningPath,
				[&] { return now.load(); },
				[&] { return serverId; },
				[&] {
					if (failPostCommit) {
						throw std::runtime_error("post-commit cleanup failed");
					}
				});
			CHECK(store.UpdatePlayerState("warning-profile", Json{
				{ "type", "playerState" },
				{ "characterName", "Before" }
			}));
			failPostCommit = true;
			CHECK(store.UpdatePlayerState("warning-profile", Json{
				{ "type", "playerState" },
				{ "characterName", "After" }
			}));
			CHECK(store.TakePersistenceWarning().has_value());
			CHECK(store.GetPlayerState("warning-profile")->at("characterName") == "After");
			DurableStateFileStore reloaded(
				warningPath,
				[&] { return now.load(); },
				[&] { return serverId; });
			CHECK(reloaded.GetPlayerState("warning-profile")->at("characterName") == "After");
			std::error_code ignored;
			std::filesystem::remove_all(warningDirectory, ignored);
		}

		WriteText(std::filesystem::path{ path.string() + ".tmp-interrupted" }, "newer but incomplete");
		{
			DurableStateFileStore ignoresTemp(
				path,
				[&] { return now.load(); },
				[&] { return "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"; });
			CHECK(ignoresTemp.ServerInstanceId() == serverId);
		}
	} catch (...) {
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
		throw;
	}

	std::filesystem::remove_all(directory);
	return 0;
}
