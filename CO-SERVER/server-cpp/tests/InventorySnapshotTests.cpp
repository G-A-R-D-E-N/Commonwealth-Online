#include "DurableStateFileStore.h"
#include "StartingGrantFixture.h"

#include "F4TInventorySnapshot.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	namespace Snapshot = F4T::InventorySnapshot;

	std::filesystem::path UniqueDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / ("co-inventory-snapshot-test-" + std::to_string(stamp));
	}

	Json Item(const std::string& a_plugin, const std::string& a_form, std::uint32_t a_count)
	{
		return Json{ { "plugin", a_plugin }, { "localFormId", a_form }, { "count", a_count } };
	}

	void TestMatchPersistsAndStaleRejects(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "snapshot-state.json";
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto items = Json::array({ Item("test.esm", "00000123", 3), Item("other.esm", "00000042", 1) });
		{
			DurableStateFileStore store(path, clock, id);
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-a", hash, std::nullopt, false, 5000.0);
			(void)store.InitializeInventory("profile-a", Json::array({ Item("test.esm", "00000123", 5) }));
			(void)ReconcileStartingGrantFixture(store, "profile-a");

			const auto first = Snapshot::NormalizeRequest("snap-1", 0, items);
			assert(first);
			const auto committed = store.ReconcileInventory("profile-a", *first);
			assert(committed.changed && committed.packet.at("status") == "committed" && committed.packet.at("revision") == 1u);

			const auto unchanged = store.ReconcileInventory("profile-a", *Snapshot::NormalizeRequest("snap-2", 1, items));
			assert(!unchanged.changed && unchanged.packet.at("status") == "committed" && unchanged.packet.at("revision") == 1u);

			const auto replay = store.ReconcileInventory("profile-a", *first);
			assert(!replay.changed && replay.packet.at("status") == "committed" && replay.packet.at("revision") == 1u);
			const auto stale = store.ReconcileInventory("profile-a", *Snapshot::NormalizeRequest("snap-stale", 0, items));
			assert(!stale.changed && stale.packet.at("status") == "rejected" && stale.packet.at("reason") == "staleRevision");
			assert(stale.packet.at("revision") == 1u);

			const auto missing = store.ReconcileInventory("nobody", *Snapshot::NormalizeRequest("snap-3", 0, items));
			assert(!missing.changed && missing.packet.at("reason") == "inventoryAuthorityUnavailable");
		}
		DurableStateFileStore restarted(path, clock, id);
		const auto inventory = restarted.GetInventory("profile-a");
		assert(inventory && inventory->at("revision") == 1u && inventory->at("items").size() == 2u);
		assert(inventory->at("items").contains("test.esm:00000123") && inventory->at("items").at("test.esm:00000123").at("count") == 3u);
	}

	void TestPendingStartingGrantHoldsSnapshotUntilAcked(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "starting-grant-state.json";
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto items = Json::array({ Item("test.esm", "00000123", 3) });
		{
			DurableStateFileStore store(path, clock, id);
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-grant", hash, std::nullopt, false, 5000.0);
			(void)store.InitializeInventory("profile-grant", Json::array({ Item("test.esm", "00000042", 1) }));
			const auto pending = store.PendingStartingGrant("profile-grant");
			assert(pending && pending->at("grantId") == "starting-inventory" && pending->at("items").size() == 1u);

			const auto held = store.ReconcileInventory("profile-grant", *Snapshot::NormalizeRequest("snap-held", 0, items));
			assert(!held.changed && held.packet.at("status") == "rejected" && held.packet.at("reason") == "startingInventoryPending");
			assert(!held.packet.contains("revision"));
		}
		DurableStateFileStore restarted(path, clock, id);
		assert(restarted.PendingStartingGrant("profile-grant"));
		(void)ReconcileStartingGrantFixture(restarted, "profile-grant");
		assert(!restarted.PendingStartingGrant("profile-grant"));
		const auto committed = restarted.ReconcileInventory("profile-grant", *Snapshot::NormalizeRequest("snap-after", 0, items));
		assert(committed.changed && committed.packet.at("status") == "committed");
	}

	void TestLimitsRejectOversizedRequests()
	{
		Json maximum = Json::array();
		Json tooMany = Json::array();
		for (std::uint32_t index = 0; index < 2049; ++index) {
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", index + 1);
			if (index < 2048) maximum.push_back(Item("test.esm", form, 1));
			tooMany.push_back(Item("test.esm", form, 1));
		}
		assert(Snapshot::NormalizeRequest("snap-max", 0, maximum));
		assert(!Snapshot::NormalizeRequest("snap-big", 0, tooMany));
		assert(!Snapshot::NormalizeRequest("snap-overflow", 0, Json::array({ Item("test.esm", "00000001", (std::numeric_limits<std::uint32_t>::max)()) })));
		assert(Snapshot::NormalizeRequest("snap-int32", 0, Json::array({ Item("test.esm", "00000001", Snapshot::kMaximumItemCount) })));
		assert(!Snapshot::NormalizeRequest("snap-count", 0, Json::array({ Item("test.esm", "00000001", 0) })));
		assert(!Snapshot::NormalizeRequest("snap-wide", 0, Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "00000001" }, { "count", 4294967296ULL } } })));
		assert(!Snapshot::NormalizeRequest("", 0, Json::array()));
		assert(!Snapshot::NormalizePart(Json{ { "type", "inventorySnapshotRequest" }, { "operationId", "snap-extra" },
			{ "baseRevision", 0u }, { "part", 0u }, { "parts", 1u }, { "items", Json::array() }, { "extra", true } }));
	}
}

int main()
{
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	try {
		TestMatchPersistsAndStaleRejects(directory);
		TestPendingStartingGrantHoldsSnapshotUntilAcked(directory);
		TestLimitsRejectOversizedRequests();
	} catch (...) {
		std::filesystem::remove_all(directory);
		throw;
	}
	std::filesystem::remove_all(directory);
	return 0;
}
