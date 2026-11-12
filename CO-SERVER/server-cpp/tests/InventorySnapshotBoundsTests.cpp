#include "DurableStateFileStore.h"

#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
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
		return std::filesystem::temp_directory_path() / ("co-inventory-snapshot-bounds-test-" + std::to_string(stamp));
	}

	Json Item(const std::string& a_plugin, const std::string& a_form, std::uint32_t a_count)
	{
		return Json{ { "plugin", a_plugin }, { "localFormId", a_form }, { "count", a_count } };
	}

	void TestLegitimateCountsReconcile(const std::filesystem::path& a_directory)
	{
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(a_directory / "legit-state.json", clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-legit", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-legit", Json::array());

		const auto caps = Snapshot::NormalizeRequest("snap-caps", 0, Json::array({ Item("fallout4.esm", "0000000F", 100000) }));
		assert(caps);
		const auto first = store.ReconcileInventory("profile-legit", *caps);
		assert(first.changed && first.packet.at("status") == "committed" && first.packet.at("revision") == 1u);

		const auto ceiling = Snapshot::NormalizeRequest("snap-ceiling", 1, Json::array({ Item("fallout4.esm", "0000000F", Snapshot::kMaximumItemCount) }));
		assert(ceiling);
		const auto second = store.ReconcileInventory("profile-legit", *ceiling);
		assert(second.changed && second.packet.at("status") == "committed" && second.packet.at("revision") == 2u);
		const auto inventory = store.GetInventory("profile-legit");
		assert(inventory && inventory->at("revision") == 2u);
		assert(inventory->at("items").at("fallout4.esm:0000000F").at("count") == Snapshot::kMaximumItemCount);
	}

	void TestRefusedSnapshotPreservesBalances(const std::filesystem::path& a_directory)
	{
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(a_directory / "refused-state.json", clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-kept", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-kept", Json::array());

		const auto honest = Json::array({ Item("fallout4.esm", "0000000F", 250), Item("test.esm", "00000123", 3) });
		const auto committed = store.ReconcileInventory("profile-kept", *Snapshot::NormalizeRequest("snap-honest", 0, honest));
		assert(committed.changed && committed.packet.at("revision") == 1u);
		const auto before = store.GetInventory("profile-kept");
		assert(before && before->at("revision") == 1u);

		const auto inflated = Json::array({ Item("fallout4.esm", "0000000F", (std::numeric_limits<std::uint32_t>::max)()), Item("test.esm", "00000123", 3) });
		assert(!Snapshot::NormalizeRequest("snap-inflated", 1, inflated));
		const auto invented = Json::array({ Item("fallout4.esm", "0000000F", 250), Item("test.esm", "01000000", 1) });
		assert(!Snapshot::NormalizeRequest("snap-invented", 1, invented));

		const auto repeat = store.ReconcileInventory("profile-kept", *Snapshot::NormalizeRequest("snap-repeat", 1, honest));
		assert(!repeat.changed && repeat.packet.at("status") == "committed" && repeat.packet.at("revision") == 1u);
		assert(store.GetInventory("profile-kept") == before);
	}

	void TestRefusesCountsAboveEngineRange()
	{
		assert(Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "0000000F", Snapshot::kMaximumItemCount) })));
		assert(!Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "0000000F", Snapshot::kMaximumItemCount + 1) })));
		assert(!Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "0000000F", (std::numeric_limits<std::uint32_t>::max)()) })));
		assert(!Snapshot::NormalizeRequest("snap-over", 0, Json::array({ Item("fallout4.esm", "0000000F", (std::numeric_limits<std::uint32_t>::max)()) })));
		const Json part{ { "type", "inventorySnapshotRequest" }, { "operationId", "snap-over-part" }, { "baseRevision", 0u }, { "part", 0u }, { "parts", 1u },
			{ "items", Json::array({ Item("fallout4.esm", "0000000F", Snapshot::kMaximumItemCount + 1) }) } };
		assert(!Snapshot::NormalizePart(part));
		assert(Snapshot::NormalizeItems(Json::array({ Item("fallout4.esm", "0000000F", (std::numeric_limits<std::uint32_t>::max)()) })));
	}

	void TestRefusesLocalIdsOutsideFormIdRange()
	{
		assert(Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "00FFFFFF", 1) })));
		assert(Snapshot::NormalizeSnapshotItems(Json::array({ Item("light-fixture.esl", "00000FFF", 1) })));
		assert(!Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "01000000", 1) })));
		assert(!Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "FE000001", 1) })));
		assert(!Snapshot::NormalizeSnapshotItems(Json::array({ Item("fallout4.esm", "FFFFFFFF", 1) })));
		assert(!Snapshot::NormalizeRequest("snap-form", 0, Json::array({ Item("fallout4.esm", "01000000", 1) })));
		assert(Snapshot::NormalizeItems(Json::array({ Item("fallout4.esm", "01000000", 1) })));
	}

	void TestEnforceRejectsMintAndKeepsLedger(const std::filesystem::path& a_directory)
	{
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto path = a_directory / "enforce-state.json";
		F4T::Identity::TokenHash hash{};
		{
			DurableStateFileStore store(path, clock, id);
			store.SetInventoryGainPolicy(Snapshot::DefaultGainPolicy(Snapshot::GainMode::Enforce));
			store.SaveResumeCredential("profile-enforce", hash, std::nullopt, false, 5000.0);
			(void)store.InitializeInventory("profile-enforce", Json::array());
			const auto onboarding = Snapshot::NormalizeRequest("snap-onboard", 0, Json::array({ Item("fallout4.esm", "0000000F", 1000) }));
			assert(onboarding);
			const auto first = store.ReconcileInventory("profile-enforce", *onboarding);
			assert(first.changed && first.packet.at("revision") == 1u);
			const auto before = store.GetInventory("profile-enforce");
			assert(before);
			const auto mint = Snapshot::NormalizeRequest("snap-mint", 1, Json::array({ Item("fallout4.esm", "0000000F", 10000000) }));
			assert(mint);
			const auto rejected = store.ReconcileInventory("profile-enforce", *mint);
			assert(!rejected.changed && rejected.packet.at("status") == "rejected");
			assert(rejected.packet.at("reason") == "gainLimited" && rejected.packet.at("revision") == 1u);
			assert(store.GetInventory("profile-enforce") == before);
		}
		DurableStateFileStore restarted(path, clock, id);
		assert(restarted.GetInventory("profile-enforce")->at("revision") == 1u);
		assert(restarted.GetInventory("profile-enforce")->at("items").at("fallout4.esm:0000000F").at("count") == 1000u);
	}

	void TestBucketsRefillWithClockAndArePerProfile(const std::filesystem::path& a_directory)
	{
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(a_directory / "refill-state.json", clock, id);
		store.SetInventoryGainPolicy(Snapshot::DefaultGainPolicy(Snapshot::GainMode::Enforce));
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-refill-a", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-refill-a", Json::array());
		store.SaveResumeCredential("profile-refill-b", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-refill-b", Json::array());
		const auto commit = [&store](const char* a_profile, const char* a_operation, std::uint32_t a_baseRevision, std::uint32_t a_count) {
			return store.ReconcileInventory(a_profile, *Snapshot::NormalizeRequest(a_operation, a_baseRevision,
				Json::array({ Item("test.esm", "00000123", a_count) })));
		};
		assert(commit("profile-refill-a", "a0", 0, 990000).changed);
		const auto limited = commit("profile-refill-a", "a1", 1, 1015000);
		assert(!limited.changed && limited.packet.at("reason") == "gainLimited" && limited.packet.at("revision") == 1u);
		now = 1060.0;
		const auto refilled = commit("profile-refill-a", "a2", 1, 1015000);
		assert(refilled.changed && refilled.packet.at("revision") == 2u);
		assert(commit("profile-refill-b", "b0", 0, 1).changed);
		assert(commit("profile-refill-b", "b1", 1, 40001).changed);
	}

	void TestWarnModeCommitsOverBudget(const std::filesystem::path& a_directory)
	{
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(a_directory / "warn-state.json", clock, id);
		store.SetInventoryGainPolicy(Snapshot::DefaultGainPolicy(Snapshot::GainMode::Warn));
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-warn", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-warn", Json::array());
		assert(store.ReconcileInventory("profile-warn", *Snapshot::NormalizeRequest("snap-w0", 0, Json::array({ Item("fallout4.esm", "0000000F", 1000) }))).changed);
		const auto mint = store.ReconcileInventory("profile-warn", *Snapshot::NormalizeRequest("snap-w1", 1, Json::array({ Item("fallout4.esm", "0000000F", 10000000) })));
		assert(mint.changed && mint.packet.at("status") == "committed" && mint.packet.at("revision") == 2u);
		assert(store.GetInventory("profile-warn")->at("items").at("fallout4.esm:0000000F").at("count") == 10000000u);
	}

	void TestRejectedGainDoesNotChargeBuckets(const std::filesystem::path& a_directory)
	{
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(a_directory / "uncharged-state.json", clock, id);
		store.SetInventoryGainPolicy(Snapshot::DefaultGainPolicy(Snapshot::GainMode::Enforce));
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-uncharged", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-uncharged", Json::array());
		assert(store.ReconcileInventory("profile-uncharged", *Snapshot::NormalizeRequest("snap-u0", 0, Json::array({ Item("test.esm", "00000123", 1) }))).changed);
		const auto oversize = store.ReconcileInventory("profile-uncharged", *Snapshot::NormalizeRequest("snap-u1", 1,
			Json::array({ Item("test.esm", "00000123", 1), Item("fallout4.esm", "0000000F", 10000000) })));
		assert(!oversize.changed && oversize.packet.at("reason") == "gainLimited");
		const auto legit = store.ReconcileInventory("profile-uncharged", *Snapshot::NormalizeRequest("snap-u2", 1,
			Json::array({ Item("test.esm", "00000123", 1), Item("fallout4.esm", "0000000F", 200000) })));
		assert(legit.changed && legit.packet.at("revision") == 2u);
	}

	void TestDurableValidationKeepsStoredLedgersLoadable()
	{
		const Json stored{ { "initialized", true }, { "revision", 3u },
			{ "items", Json{ { "test.esm:01000000", Json{ { "plugin", "test.esm" }, { "localFormId", "01000000" }, { "count", 3000000000u } } } } } };
		F4T::DurableState::ValidateInventory(stored);
	}
}

int main()
{
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	try {
		TestLegitimateCountsReconcile(directory);
		TestRefusedSnapshotPreservesBalances(directory);
		TestRefusesCountsAboveEngineRange();
		TestRefusesLocalIdsOutsideFormIdRange();
		TestEnforceRejectsMintAndKeepsLedger(directory);
		TestBucketsRefillWithClockAndArePerProfile(directory);
		TestWarnModeCommitsOverBudget(directory);
		TestRejectedGainDoesNotChargeBuckets(directory);
		TestDurableValidationKeepsStoredLedgersLoadable();
	} catch (...) {
		std::filesystem::remove_all(directory);
		throw;
	}
	std::filesystem::remove_all(directory);
	return 0;
}
