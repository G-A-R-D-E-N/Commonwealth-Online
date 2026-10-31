#include "DurableStateFileStore.h"

#include <nlohmann/json.hpp>

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
		assert(output);
		output << a_text;
		output.flush();
		assert(output.good());
	}

	std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		assert(input);
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
			assert(threw && !store.GetInventory("unadmitted"));
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile", hash, std::nullopt, true, 5000.0);
			const auto diskBefore = ReadText(path);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			threw = false;
			try { (void)store.InitializeInventory("profile", catalog); }
			catch (const std::exception&) { threw = true; }
			assert(threw && !store.GetInventory("profile"));
			assert(ReadText(path) == diskBefore);
			std::filesystem::remove_all(backup);
			failPostCommit = true;
			const auto granted = store.InitializeInventory("profile", catalog);
			assert(store.TakePersistenceWarning());
			assert(granted.at("initialized") == true && granted.at("revision") == 0);
			assert(granted.at("items").at("test.esm:00000123").at("count") == 5);
			failPostCommit = false;
			const auto diskGranted = ReadText(path);
			assert(store.InitializeInventory("profile", Json::array()) == granted);
			assert(ReadText(path) == diskGranted);
			assert(store.DeleteResumeCredential("profile"));
			store.SaveResumeCredential("profile", hash, std::nullopt, false, 6000.0);
			assert(store.InitializeInventory("profile", Json::array()) == granted);
			store.SaveResumeCredential("empty", hash, std::nullopt, true, 6000.0);
			const auto empty = store.InitializeInventory("empty", Json::array());
			assert(empty.at("items").empty());
			assert(store.InitializeInventory("empty", catalog) == empty);
			DurableStateFileStore restarted(path, clock, id);
			assert(restarted.InitializeInventory("profile", Json::array()) == granted);
			assert(restarted.InitializeInventory("empty", catalog) == empty);
			WriteText(path, "corrupt");
			DurableStateFileStore recovered(path, clock, id);
			assert(recovered.RecoveryNotice());
			threw = false;
			try { (void)recovered.InitializeInventory("empty", catalog); }
			catch (const std::runtime_error&) { threw = true; }
			assert(threw && !recovered.GetInventory("empty"));
			assert(recovered.InitializeInventory("profile", catalog) == granted);
			DurableStateFileStore recoveredRestart(path, clock, id);
			threw = false;
			try { (void)recoveredRestart.InitializeInventory("empty", catalog); }
			catch (const std::runtime_error&) { threw = true; }
			assert(threw && recoveredRestart.InitializeInventory("profile", catalog) == granted);
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
			const auto before = ReadText(path);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			bool threw = false;
			try { (void)store.DropInventory("a", drop, position); }
			catch (const std::exception&) { threw = true; }
			assert(threw && ReadText(path) == before && store.GetInventory("a")->at("revision") == 0);
			assert(store.GetDropSnapshot()->empty());
			std::filesystem::remove_all(backup);
			failPostCommit = true;
			const auto committed = store.DropInventory("a", drop, position);
			assert(committed.outcome == Outcome::Committed && store.TakePersistenceWarning());
			assert(store.GetInventory("a")->at("items").empty());
			const auto projection = store.GetDropSnapshot();
			assert(projection && projection->size() == 1);
			assert(DecodeStableDroppedObject(projection->begin()->second)->item == committed.packet.at("drop").at("item"));
			failPostCommit = false;
			const auto droppedDisk = ReadText(path);
			assert(store.DropInventory("a", drop, position).packet == committed.packet);
			assert(ReadText(path) == droppedDisk);
			DurableStateFileStore restarted(path, clock, id);
			assert(restarted.DropInventory("a", drop, position).packet == committed.packet);
			assert(restarted.GetDropSnapshot() == projection);
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			threw = false;
			try { (void)restarted.PickupInventory("b", pickup, position); }
			catch (const std::exception&) { threw = true; }
			assert(threw && ReadText(path) == droppedDisk && restarted.GetInventory("b")->at("items").empty());
			assert(restarted.GetDropSnapshot() == projection);
			std::filesystem::remove_all(backup);
			TransactionResult first, second;
			std::thread one([&] { first = restarted.PickupInventory("b", pickup, position); });
			std::thread two([&] { second = restarted.PickupInventory("c", pickup, position); });
			one.join(); two.join();
			assert((first.outcome == Outcome::Committed) != (second.outcome == Outcome::Committed));
			const auto winner = first.outcome == Outcome::Committed ? "b" : "c";
			const auto loser = first.outcome == Outcome::Committed ? "c" : "b";
			const auto result = first.outcome == Outcome::Committed ? first : second;
			assert(restarted.GetInventory(winner)->at("items").at("test.esm:00000123").at("count") == 5);
			assert(restarted.GetInventory(loser)->at("items").empty());
			assert(restarted.GetDropSnapshot()->empty());
			const auto pickedDisk = ReadText(path);
			DurableStateFileStore finalRestart(path, clock, id);
			assert(finalRestart.PickupInventory(winner, pickup, position).packet == result.packet);
			assert(finalRestart.DropInventory("a", drop, position).packet == committed.packet);
			assert(ReadText(path) == pickedDisk);
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
			assert(!oldStore.GetDropSnapshot() && Json::parse(ReadText(oldPath)) == oversized);
			auto shrink = pickup;
			shrink["operationId"] = "shrink";
			shrink["expectedRevision"] = 200u;
			assert(oldStore.PickupInventory("a", shrink, position).outcome == Outcome::Committed);
			assert(oldStore.GetDropSnapshot()->size() == 399);
			WriteText(path, "corrupt");
			DurableStateFileStore recovered(path, clock, id);
			assert(recovered.RecoveryNotice());
			assert(!recovered.GetDropSnapshot());
			assert(recovered.DropInventory("a", drop, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			assert(recovered.PickupInventory(winner, pickup, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			DurableStateFileStore recoveredRestart(path, clock, id);
			assert(recoveredRestart.DropInventory("a", drop, position).packet.at("reason") == "inventoryAuthorityUnavailable");
			assert(!recoveredRestart.GetDropSnapshot());
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
			assert(store.UpdatePlayerStates(oldStates));
			store.UpdateServerWorldState(Json{ { "timeHHmm", "1200" } });
			const auto primaryBefore = ReadText(path), backupBefore = ReadText(backup);
			assert(primaryBefore.size() > 128);
			struct rlimit original{};
			assert(getrlimit(RLIMIT_FSIZE, &original) == 0 && original.rlim_cur >= 128);
			struct sigaction ignored{}, originalSignal{};
			ignored.sa_handler = SIG_IGN;
			assert(sigemptyset(&ignored.sa_mask) == 0);
			assert(sigaction(SIGXFSZ, &ignored, &originalSignal) == 0);
			auto restore = [&](struct rlimit*) {
				const int restoredLimit = setrlimit(RLIMIT_FSIZE, &original);
				const int restoredSignal = sigaction(SIGXFSZ, &originalSignal, nullptr);
				if (restoredLimit != 0 || restoredSignal != 0) std::abort();
			};
			std::unique_ptr<struct rlimit, decltype(restore)> restoration(&original, restore);
			auto limited = original;
			limited.rlim_cur = 128;
			assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
			assert(store.UpdatePlayerStates(newStates));
			store.WaitForCommits();
			const auto failure = store.TakePersistenceWarning().value_or("");
			restoration.reset();
			struct rlimit restored{};
			assert(getrlimit(RLIMIT_FSIZE, &restored) == 0 && restored.rlim_cur == original.rlim_cur && restored.rlim_max == original.rlim_max);
			struct sigaction restoredSignal{};
			assert(sigaction(SIGXFSZ, nullptr, &restoredSignal) == 0 && restoredSignal.sa_handler == originalSignal.sa_handler);
			assert(failure.starts_with("write failed for '" + path.string() + ".tmp-") && failure.ends_with("' (code " + std::to_string(EFBIG) + ")"));
			assert(ReadText(path) == primaryBefore && ReadText(backup) == backupBefore && !store.TakePersistenceWarning());
			for (const auto& entry : std::filesystem::directory_iterator(directory))
				assert(entry.path() == path || entry.path() == backup);
			auto verify = [&](DurableStateFileStore& checked, const auto& states) {
				assert(checked.ServerInstanceId() == serverId && !checked.RecoveryNotice());
				assert(checked.GetServerWorldState() == Json({ { "timeHHmm", "1200" } }));
				for (const auto& [profile, expected] : states) {
					assert(checked.GetPlayerState(profile) == expected);
					const auto credential = checked.GetResumeCredential(profile);
					assert(credential && credential->tokenHash == current && credential->previousTokenHash == previous && credential->confirmed && credential->expiresAt == 5000);
				}
			};
			verify(store, newStates);
			DurableStateFileStore reopened(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
			verify(reopened, oldStates);
			assert(store.UpdatePlayerStates(newStates));
			store.WaitForCommits();
			verify(store, newStates);
			DurableStateFileStore retried(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
			verify(retried, newStates);
			assert(ReadText(backup) == primaryBefore && !store.TakePersistenceWarning());
			for (const auto& entry : std::filesystem::directory_iterator(directory))
				assert(entry.path() == path || entry.path() == backup);
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
				assert(store.UpdatePlayerStates(oldStates));
				store.UpdateServerWorldState(Json{ { "timeHHmm", "1200" } });
				const auto primaryBefore = ReadText(path), backupBefore = ReadText(backup);
				const auto blocked = blockPrimary ? path : backup;
				std::unique_ptr<void, decltype(&CloseHandle)> handle(
					CreateFileW(blocked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &CloseHandle);
				assert(handle.get() != INVALID_HANDLE_VALUE);
				assert(store.UpdatePlayerStates(newStates));
				store.WaitForCommits();
				const auto failure = store.TakePersistenceWarning().value_or("");
				const auto errorPrefix = "MoveFileExW failed for '" + blocked.string() + "' (code ";
				const bool accessDenied = failure == errorPrefix + std::to_string(ERROR_ACCESS_DENIED) + ")";
				assert(accessDenied || failure == errorPrefix + std::to_string(ERROR_SHARING_VIOLATION) + ")");
				assert(ReadText(path) == primaryBefore);
				assert(ReadText(backup) == (blockPrimary ? primaryBefore : backupBefore));
				assert(!store.TakePersistenceWarning());
				for (const auto& entry : std::filesystem::directory_iterator(directory))
					assert(entry.path() == path || entry.path() == backup);
				auto verify = [&](DurableStateFileStore& checked, const auto& states) {
					assert(checked.ServerInstanceId() == serverId && !checked.RecoveryNotice());
					assert(checked.GetServerWorldState() == Json({ { "timeHHmm", "1200" } }));
					for (const auto& [profile, expected] : states) {
						assert(checked.GetPlayerState(profile) == expected);
						const auto credential = checked.GetResumeCredential(profile);
						assert(credential && credential->tokenHash == current && credential->previousTokenHash == previous && credential->confirmed && credential->expiresAt == 5000);
					}
				};
				verify(store, newStates);
				DurableStateFileStore reopened(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				verify(reopened, oldStates);
				const auto backupAfterFailure = ReadText(backup);
				handle.reset();
				assert(store.UpdatePlayerStates(newStates));
				store.WaitForCommits();
				verify(store, newStates);
				DurableStateFileStore retried(path, [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				verify(retried, newStates);
				assert(ReadText(backup) == primaryBefore && !store.TakePersistenceWarning());
				for (const auto& entry : std::filesystem::directory_iterator(directory))
					assert(entry.path() == path || entry.path() == backup);
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
			double clock = 1000.0;
			DurableStateFileStore store(path, [&] { return clock += 1.0; }, [&] { return serverId; });
			assert(store.GetSteamIdentities().is_null());
			store.SaveSteamIdentities(section);
			store.WaitForCommits();
			const auto written = ReadText(path);
			store.SaveSteamIdentities(section);
			store.WaitForCommits();
			assert(ReadText(path) == written);
			assert(store.GetSteamIdentities() == section);
		}
		{
			DurableStateFileStore reloaded(path, [] { return 2000.; }, [&] { return serverId; });
			assert(reloaded.GetSteamIdentities() == section);
		}
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
	}
}

int main()
{
	TestStartingInventory();
	TestInventoryTransactions();
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
	double now = 1000.0;
	const std::string serverId = "0123456789abcdef0123456789abcdef";

	try {
		{
			DurableStateFileStore store(
				path,
				[&] { return now; },
				[&] { return serverId; });
			assert(store.ServerInstanceId() == serverId);
			assert(!store.RecoveryNotice().has_value());
			assert(std::filesystem::exists(path));

			F4T::Identity::TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>(index + 1);
			}
			now = 1010.0;
			store.SaveResumeCredential("profile-one", hash, std::nullopt, true, 5000.0);
			now = 1020.0;
			assert(store.UpdatePlayerState("profile-one", Json{
				{ "type", "playerState" },
				{ "characterName", "Nomad" },
				{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
			}));
			now = 1025.0;
			const auto writeTimeBefore = std::filesystem::last_write_time(path);
			assert(!store.UpdatePlayerState("profile-one", Json{
				{ "type", "playerState" },
				{ "characterName", "Nomad" },
				{ "actionEvents", Json::array({ Json{ { "sequence", 2 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
			}));
			const auto writeTimeAfter = std::filesystem::last_write_time(path);
			assert(writeTimeBefore == writeTimeAfter);
			now = 1030.0;
			store.UpdateServerWorldState(Json{
				{ "timeHHmm", "1800" },
				{ "weatherConsoleArg", "001ca7e4" },
				{ "ignored", "transient" }
			});
			assert(std::filesystem::exists(backup));
		}

		{
			DurableStateFileStore restarted(
				path,
				[&] { return now; },
				[&] { return "ffffffffffffffffffffffffffffffff"; });
			assert(restarted.ServerInstanceId() == serverId);
			const auto resume = restarted.GetResumeCredential("profile-one");
			assert(resume.has_value());
			assert(resume->expiresAt == 5000.0);
			const auto player = restarted.GetPlayerState("profile-one");
			assert(player.has_value());
			assert(player->at("characterName") == "Nomad");
			assert(!player->contains("actionEvents"));
			const auto world = restarted.GetServerWorldState();
			assert(world.at("timeHHmm") == "1800");
			assert(!world.contains("ignored"));
		}

		WriteText(path, "{ truncated");
		{
			DurableStateFileStore recovered(
				path,
				[&] { return now; },
				[&] { return "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"; });
			assert(recovered.RecoveryNotice().has_value());
			assert(recovered.ServerInstanceId() == serverId);
			assert(std::filesystem::exists(path));
			assert(std::filesystem::exists(backup));
		}

		std::filesystem::remove(path);
		{
			DurableStateFileStore missingPrimary(
				path,
				[&] { return now; },
				[&] { return "dddddddddddddddddddddddddddddddd"; });
			assert(missingPrimary.RecoveryNotice().has_value());
			assert(missingPrimary.ServerInstanceId() == serverId);
			assert(std::filesystem::exists(path));
		}

		std::filesystem::remove(path);
		WriteText(backup, "{ torn backup");
		try {
			DurableStateFileStore tornBackup(
				path,
				[&] { return now; },
				[&] { return "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; });
			(void)tornBackup;
			assert(false && "a missing primary with a torn backup must fail startup");
		} catch (const std::runtime_error&) {
		}
		assert(!std::filesystem::exists(path));
		assert(ReadText(backup) == "{ torn backup");

		WriteText(path, "bad primary");
		WriteText(backup, "bad backup");
		try {
			DurableStateFileStore invalid(
				path,
				[&] { return now; },
				[&] { return serverId; });
			(void)invalid;
			assert(false && "invalid primary and backup must fail startup");
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
				[&] { return now; },
				[&] { return "cccccccccccccccccccccccccccccccc"; });
			assert(migrated.RecoveryNotice().has_value());
			assert(migrated.ServerInstanceId() == serverId);
			const auto player = migrated.GetPlayerState("legacy-profile");
			assert(player.has_value() && player->at("characterName") == "Legacy");
			assert(migrated.GetServerWorldState().at("timeHHmm") == "0900");
			assert(std::filesystem::exists(backup));
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
				[&] { return now; },
				[&] { return serverId; });
			assert(store.UpdatePlayerState("fault-profile", Json{
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
			assert(threw);

			const auto retained = store.GetPlayerState("fault-profile");
			assert(retained.has_value() && retained->at("characterName") == "Before");
			assert(ReadText(faultPath) == diskBefore);

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
				[&] { return now; },
				[&] { return serverId; },
				[&] {
					if (failPostCommit) {
						throw std::runtime_error("post-commit cleanup failed");
					}
				});
			assert(store.UpdatePlayerState("warning-profile", Json{
				{ "type", "playerState" },
				{ "characterName", "Before" }
			}));
			failPostCommit = true;
			assert(store.UpdatePlayerState("warning-profile", Json{
				{ "type", "playerState" },
				{ "characterName", "After" }
			}));
			assert(store.TakePersistenceWarning().has_value());
			assert(store.GetPlayerState("warning-profile")->at("characterName") == "After");
			DurableStateFileStore reloaded(
				warningPath,
				[&] { return now; },
				[&] { return serverId; });
			assert(reloaded.GetPlayerState("warning-profile")->at("characterName") == "After");
			std::error_code ignored;
			std::filesystem::remove_all(warningDirectory, ignored);
		}

		WriteText(std::filesystem::path{ path.string() + ".tmp-interrupted" }, "newer but incomplete");
		{
			DurableStateFileStore ignoresTemp(
				path,
				[&] { return now; },
				[&] { return "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"; });
			assert(ignoresTemp.ServerInstanceId() == serverId);
		}
	} catch (...) {
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
		throw;
	}

	std::filesystem::remove_all(directory);
	return 0;
}
