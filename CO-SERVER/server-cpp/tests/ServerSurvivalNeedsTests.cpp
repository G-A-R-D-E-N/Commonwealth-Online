#include "DurableStateFileStore.h"
#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{
	using Json = nlohmann::json;
	namespace SN = F4T::SurvivalNeeds;

	std::filesystem::path UniqueDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / ("co-survival-needs-test-" + std::to_string(stamp));
	}

	Json Item(const std::string& a_plugin, const std::string& a_form, std::uint32_t a_count)
	{
		return Json{ { "plugin", a_plugin }, { "localFormId", a_form }, { "count", a_count } };
	}

	SN::Claim Consume(const std::string& a_operation)
	{
		return SN::Claim{ a_operation, "food", "test.esm:00000123", 0, 0.0 };
	}

	void TestConsumesSettleIndependentlyAcrossReload(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-needs.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-a", hash, std::nullopt, false, 5000.0);
			(void)store.InitializeInventory("profile-a", Json::array({ Item("test.esm", "00000123", 5) }));
			(void)ReconcileStartingGrantFixture(store, "profile-a");
			assert(store.MutateSurvivalNeeds({ "profile-a" }, [](std::string_view, SN::Record& a_record) {
				a_record.state.hunger = 0.9;
				return true;
			}, true));
			assert(store.BeginSurvivalClaim("profile-a", Consume("op-1")) == SN::Admission::Started);
			assert(store.BeginSurvivalClaim("profile-a", Consume("op-1")) == SN::Admission::Pending);
			assert(store.BeginSurvivalClaim("profile-a", Consume("op-2")) == SN::Admission::Started);
			assert(store.BeginSurvivalClaim("profile-a", SN::Claim{ "op-3", "food", "test.esm:00000999", 0, 0.0 }) == SN::Admission::Missing);

			const auto firstDrop = store.ReconcileInventory("profile-a",
				*F4T::InventorySnapshot::NormalizeRequest("snap-1", 0, Json::array({ Item("test.esm", "00000123", 4) })));
			assert(firstDrop.changed);
			const auto beforeSettle = store.GetSurvivalNeeds("profile-a");
			assert(beforeSettle.claims.size() == 2 && beforeSettle.state.hunger == 0.9);

			const auto firstSettle = store.SettleSurvivalNeeds("profile-a");
			assert(firstSettle.size() == 1 && firstSettle.front().first == "op-2" && firstSettle.front().second == SN::Status::Applied);
			const auto afterFirst = store.GetSurvivalNeeds("profile-a");
			assert(afterFirst.claims.size() == 1 && afterFirst.claims.front().operationId == "op-1" && afterFirst.state.hunger > 0.49 && afterFirst.state.hunger < 0.51);

			const auto secondDrop = store.ReconcileInventory("profile-a",
				*F4T::InventorySnapshot::NormalizeRequest("snap-2", 1, Json::array({ Item("test.esm", "00000123", 3) })));
			assert(secondDrop.changed);
			const auto secondSettle = store.SettleSurvivalNeeds("profile-a");
			assert(secondSettle.size() == 1 && secondSettle.front().first == "op-1" && secondSettle.front().second == SN::Status::Applied);
			const auto settled = store.GetSurvivalNeeds("profile-a");
			assert(settled.claims.empty() && settled.state.hunger > 0.09 && settled.state.hunger < 0.11);
			const auto status = SN::FindConsumed(settled, "op-1");
			assert(status && *status == SN::Status::Applied);
			assert(store.BeginSurvivalClaim("profile-a", Consume("op-1")) == SN::Admission::Replayed);
			store.WaitForCommits();
		}
		now = 1500.0;
		CommonwealthOnline::Server::DurableStateFileStore restarted(path, clock, id);
		const auto reloaded = restarted.GetSurvivalNeeds("profile-a");
		assert(reloaded.state.hunger > 0.09 && reloaded.state.hunger < 0.11 && reloaded.claims.empty());
		assert(restarted.BeginSurvivalClaim("profile-a", Consume("op-1")) == SN::Admission::Replayed);
	}

	void TestOutstandingClaimsAreBoundedAndPersist(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-bounded.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-c", hash, std::nullopt, false, 5000.0);
			(void)store.InitializeInventory("profile-c", Json::array({ Item("test.esm", "00000123", 5) }));
			(void)ReconcileStartingGrantFixture(store, "profile-c");
			for (const char* operation : { "op-a", "op-b", "op-c", "op-d" }) {
				assert(store.BeginSurvivalClaim("profile-c", Consume(operation)) == SN::Admission::Started);
			}
			assert(store.BeginSurvivalClaim("profile-c", Consume("op-e")) == SN::Admission::Busy);
			assert(store.BeginSurvivalClaim("profile-c", Consume("op-a")) == SN::Admission::Pending);
			assert(store.SettleSurvivalNeeds("profile-c").empty());
			store.WaitForCommits();
		}
		CommonwealthOnline::Server::DurableStateFileStore restarted(path, clock, id);
		assert(restarted.GetSurvivalNeeds("profile-c").claims.size() == 4);
		assert(restarted.BeginSurvivalClaim("profile-c", Consume("op-e")) == SN::Admission::Busy);
		now = 1031.0;
		const auto expired = restarted.SettleSurvivalNeeds("profile-c");
		assert(expired.size() == 4);
		for (const auto& [operation, status] : expired) {
			assert(status == SN::Status::Refused);
		}
		assert(restarted.GetSurvivalNeeds("profile-c").claims.empty());
	}

	void TestUnprovenLossRefusesAtDeadline(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-deadline.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "fedcba9876543210fedcba9876543210" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-b", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-b", Json::array({ Item("test.esm", "00000123", 2) }));
		(void)ReconcileStartingGrantFixture(store, "profile-b");
		assert(store.MutateSurvivalNeeds({ "profile-b" }, [](std::string_view, SN::Record& a_record) {
			a_record.state.thirst = 0.6;
			return true;
		}, true));
		assert(store.BeginSurvivalClaim("profile-b", SN::Claim{ "op-late", "drink", "test.esm:00000123", 0, 0.0 }) == SN::Admission::Started);
		now = 1031.0;
		const auto refused = store.SettleSurvivalNeeds("profile-b");
		assert(refused.size() == 1 && refused.front().second == SN::Status::Refused);
		const auto record = store.GetSurvivalNeeds("profile-b");
		assert(record.claims.empty() && record.state.thirst == 0.6);
		const auto status = SN::FindConsumed(record, "op-late");
		assert(status && *status == SN::Status::Refused);
		assert(store.BeginSurvivalClaim("profile-b", SN::Claim{ "op-late", "drink", "test.esm:00000123", 0, 0.0 }) == SN::Admission::Replayed);
	}

	void TestFailedDeferredClaimIsUnavailable(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-unavailable.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "11223344556677889900aabbccddeeff" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-d", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-d", Json::array({ Item("test.esm", "00000123", 5) }));
		(void)ReconcileStartingGrantFixture(store, "profile-d");
		store.SetDeferCommits(true);
		assert(store.BeginSurvivalClaim("profile-d", SN::Claim{ std::string{ 1, '\xff' }, "food", "test.esm:00000123", 0, 0.0 }) == SN::Admission::Unavailable);
		assert(store.GetSurvivalNeeds("profile-d").claims.empty());
		store.SetDeferCommits(false);
		assert(store.BeginSurvivalClaim("profile-d", Consume("op-ok")) == SN::Admission::Started);
		assert(store.GetSurvivalNeeds("profile-d").claims.size() == 1);
	}

	void TestBatchedMutationCommitsEveryProfile(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-batch.json";
		const auto clock = [] { return 1000.0; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		assert(store.MutateSurvivalNeeds({ "batch-a", "batch-b" }, [](std::string_view a_profileId, SN::Record& a_record) {
			a_record.state.sleep = a_profileId == "batch-a" ? 0.25 : 0.75;
			return true;
		}, false));
		assert(store.GetSurvivalNeeds("batch-a").state.sleep == 0.25 && store.GetSurvivalNeeds("batch-b").state.sleep == 0.75);
	}

	void InitializeProfile(CommonwealthOnline::Server::DurableStateFileStore& a_store, const std::string& a_profileId, std::uint32_t a_count)
	{
		F4T::Identity::TokenHash hash{};
		a_store.SaveResumeCredential(a_profileId, hash, std::nullopt, false, 5000.0);
		(void)a_store.InitializeInventory(a_profileId, Json::array({ Item("test.esm", "00000123", a_count) }));
		(void)ReconcileStartingGrantFixture(a_store, a_profileId);
	}

	void TestSnapshotBeforeClaimCreditsOnce(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-credit.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		InitializeProfile(store, "profile-f", 3);
		assert(store.MutateSurvivalNeeds({ "profile-f" }, [](std::string_view, SN::Record& a_record) {
			a_record.state.hunger = 0.9;
			return true;
		}, true));
		assert(store.ReconcileInventory("profile-f", *F4T::InventorySnapshot::NormalizeRequest("snap-f", 0, Json::array({ Item("test.esm", "00000123", 2) }))).changed);
		now = 1001.0;
		assert(store.BeginSurvivalClaim("profile-f", Consume("op-credit")) == SN::Admission::Started);
		now = 1002.0;
		const auto settled = store.SettleSurvivalNeeds("profile-f");
		assert(settled.size() == 1 && settled.front().first == "op-credit" && settled.front().second == SN::Status::Applied);
		const auto relieved = store.GetSurvivalNeeds("profile-f");
		assert(relieved.state.hunger > 0.49 && relieved.state.hunger < 0.51);
		assert(store.BeginSurvivalClaim("profile-f", Consume("op-second")) == SN::Admission::Started);
		now = 1003.0;
		assert(store.SettleSurvivalNeeds("profile-f").empty());
		assert(store.GetSurvivalNeeds("profile-f").state.hunger == relieved.state.hunger);
		store.WaitForCommits();
	}

	void TestStaleDropIsNotCredited(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-stale.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		InitializeProfile(store, "profile-g", 3);
		assert(store.ReconcileInventory("profile-g", *F4T::InventorySnapshot::NormalizeRequest("snap-g", 0, Json::array({ Item("test.esm", "00000123", 2) }))).changed);
		now = 1006.0;
		assert(store.BeginSurvivalClaim("profile-g", Consume("op-stale")) == SN::Admission::Started);
		now = 1007.0;
		assert(store.SettleSurvivalNeeds("profile-g").empty());
		now = 1036.0;
		const auto refused = store.SettleSurvivalNeeds("profile-g");
		assert(refused.size() == 1 && refused.front().first == "op-stale" && refused.front().second == SN::Status::Refused);
		store.WaitForCommits();
	}

	void TestOneDropCannotSatisfyTwoClaims(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-two-claims.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		InitializeProfile(store, "profile-h", 3);
		assert(store.ReconcileInventory("profile-h", *F4T::InventorySnapshot::NormalizeRequest("snap-h", 0, Json::array({ Item("test.esm", "00000123", 2) }))).changed);
		now = 1001.0;
		assert(store.BeginSurvivalClaim("profile-h", Consume("op-a")) == SN::Admission::Started);
		assert(store.BeginSurvivalClaim("profile-h", Consume("op-b")) == SN::Admission::Started);
		now = 1002.0;
		const auto first = store.SettleSurvivalNeeds("profile-h");
		assert(first.size() == 1 && first.front().first == "op-a" && first.front().second == SN::Status::Applied);
		now = 1031.0;
		const auto second = store.SettleSurvivalNeeds("profile-h");
		assert(second.size() == 1 && second.front().first == "op-b" && second.front().second == SN::Status::Refused);

		InitializeProfile(store, "profile-i", 3);
		now = 1040.0;
		assert(store.BeginSurvivalClaim("profile-i", Consume("op-outstanding")) == SN::Admission::Started);
		now = 1041.0;
		assert(store.ReconcileInventory("profile-i", *F4T::InventorySnapshot::NormalizeRequest("snap-i", 0, Json::array({ Item("test.esm", "00000123", 2) }))).changed);
		now = 1042.0;
		assert(store.BeginSurvivalClaim("profile-i", Consume("op-later")) == SN::Admission::Started);
		now = 1043.0;
		const auto proven = store.SettleSurvivalNeeds("profile-i");
		assert(proven.size() == 1 && proven.front().first == "op-outstanding" && proven.front().second == SN::Status::Applied);
		now = 1072.0;
		const auto late = store.SettleSurvivalNeeds("profile-i");
		assert(late.size() == 1 && late.front().first == "op-later" && late.front().second == SN::Status::Refused);
		store.WaitForCommits();
	}

	void TestRecentDropIsScopedToProfileAndItem(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "survival-drop-scope.json";
		double now = 1000.0;
		const auto clock = [&now] { return now; };
		const auto id = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		CommonwealthOnline::Server::DurableStateFileStore store(path, clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-j", hash, std::nullopt, false, 5000.0);
		(void)store.InitializeInventory("profile-j", Json::array({ Item("test.esm", "00000123", 3), Item("test.esm", "00000456", 3) }));
		(void)ReconcileStartingGrantFixture(store, "profile-j");
		assert(store.ReconcileInventory("profile-j", *F4T::InventorySnapshot::NormalizeRequest("snap-j", 0, Json::array({ Item("test.esm", "00000123", 2), Item("test.esm", "00000456", 3) }))).changed);
		InitializeProfile(store, "profile-k", 3);
		now = 1001.0;
		assert(store.BeginSurvivalClaim("profile-k", Consume("op-other-profile")) == SN::Admission::Started);
		assert(store.BeginSurvivalClaim("profile-j", SN::Claim{ "op-other-item", "food", "test.esm:00000456", 0, 0.0 }) == SN::Admission::Started);
		now = 1002.0;
		assert(store.SettleSurvivalNeeds("profile-k").empty());
		assert(store.SettleSurvivalNeeds("profile-j").empty());
		assert(store.BeginSurvivalClaim("profile-j", Consume("op-same-item")) == SN::Admission::Started);
		const auto settled = store.SettleSurvivalNeeds("profile-j");
		assert(settled.size() == 1 && settled.front().first == "op-same-item" && settled.front().second == SN::Status::Applied);
		store.WaitForCommits();
	}

	void TestMissingOrCorruptNeedsLoadAsDefault()
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		root["profiles"]["missing"] = Json{ { "updatedAt", 1.0 } };
		const auto missing = F4T::DurableState::GetSurvivalNeeds(root, "missing");
		assert(missing.state.hunger == 0.0 && missing.state.thirst == 0.0 && missing.state.sleep == 0.0 && missing.revision == 0 && missing.claims.empty());

		root["profiles"]["corrupt"] = Json{ { "updatedAt", 1.0 }, { "survivalNeeds", "garbage" } };
		const auto text = F4T::DurableState::GetSurvivalNeeds(root, "corrupt");
		assert(text.state.hunger == 0.0 && text.revision == 0 && text.consumed.empty());

		root["profiles"]["corrupt"]["survivalNeeds"] = Json{ { "hunger", 2.0 }, { "thirst", 0.0 }, { "sleep", 0.0 }, { "revision", 3 }, { "claims", Json::array() }, { "consumed", Json::array() } };
		const auto outOfRange = F4T::DurableState::GetSurvivalNeeds(root, "corrupt");
		assert(outOfRange.state.hunger == 0.0 && outOfRange.revision == 0);

		root["profiles"]["corrupt"]["survivalNeeds"] = Json{ { "hunger", 0.5 }, { "thirst", 0.0 }, { "sleep", 0.0 }, { "revision", 3 }, { "claims", Json::array() }, { "consumed", "nope" } };
		const auto badList = F4T::DurableState::GetSurvivalNeeds(root, "corrupt");
		assert(badList.state.hunger == 0.0 && badList.revision == 0);

		root["profiles"]["corrupt"]["survivalNeeds"] = Json{ { "hunger", 0.5 }, { "thirst", 0.0 }, { "sleep", 0.0 }, { "revision", 3 }, { "claims", "nope" }, { "consumed", Json::array() } };
		const auto badClaims = F4T::DurableState::GetSurvivalNeeds(root, "corrupt");
		assert(badClaims.state.hunger == 0.0 && badClaims.revision == 0);
	}
}

int main()
{
	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	try {
		TestConsumesSettleIndependentlyAcrossReload(directory);
		TestOutstandingClaimsAreBoundedAndPersist(directory);
		TestUnprovenLossRefusesAtDeadline(directory);
		TestFailedDeferredClaimIsUnavailable(directory);
		TestBatchedMutationCommitsEveryProfile(directory);
		TestSnapshotBeforeClaimCreditsOnce(directory);
		TestStaleDropIsNotCredited(directory);
		TestOneDropCannotSatisfyTwoClaims(directory);
		TestRecentDropIsScopedToProfileAndItem(directory);
		TestMissingOrCorruptNeedsLoadAsDefault();
	} catch (...) {
		std::filesystem::remove_all(directory);
		throw;
	}
	std::filesystem::remove_all(directory);
	return 0;
}
