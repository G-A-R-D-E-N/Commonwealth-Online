#include "DurableStateFileStore.h"
#include "DurableWritePipeline.h"
#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DurableStateTestAccess
	{
		static void AuditUnlistedProfiles(DurableStateFileStore& a_store, bool a_enabled)
		{
			a_store._auditUnlistedProfiles = a_enabled;
		}

		static nlohmann::json Profiles(const DurableStateFileStore& a_store)
		{
			const std::scoped_lock lock(a_store._gate);
			return a_store._root.at("profiles");
		}

		static bool MutateProfilesUnscoped(DurableStateFileStore& a_store, std::vector<std::string> a_profileIds,
			const std::function<bool(nlohmann::json&)>& a_mutate)
		{
			const std::scoped_lock lock(a_store._gate);
			return a_store.MutateProfilesAndPersistLocked(std::move(a_profileIds), {}, a_mutate, false);
		}
	};
}

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableSectionDelta;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::DurableStateTestAccess;
	using CommonwealthOnline::Server::DurableWritePipeline;

	void Require(bool a_condition, const char* a_what)
	{
		if (!a_condition) {
			std::cerr << "check failed: " << a_what << '\n';
			std::abort();
		}
	}

	std::filesystem::path MakeDirectory(const std::string& a_name)
	{
		const auto directory = std::filesystem::temp_directory_path() / a_name;
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory;
	}

	Json ReadDocument(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		std::ostringstream buffer;
		buffer << input.rdbuf();
		return Json::parse(buffer.str());
	}

	std::string Account(int a_index)
	{
		std::string hex = "00000000000000000000000000000000";
		for (std::size_t digit = hex.size(); digit-- > 0 && a_index > 0; a_index >>= 4) hex[digit] = "0123456789abcdef"[a_index & 15];
		return "co-" + hex;
	}

	std::string Slot(int a_index)
	{
		return Account(a_index) + "#0";
	}

	Json Appearance(int a_salt)
	{
		Json morphs = Json::array();
		for (int value = 0; value < 110; ++value) {
			std::string id = "00020000";
			id[7] = "0123456789ABCDEF"[value & 15];
			id[6] = "0123456789ABCDEF"[(value >> 4) & 15];
			morphs.push_back(Json{ { "id", id }, { "value", 0.001 * ((value * 11 + a_salt) % 997) + 0.0001234 } });
		}
		return Json{ { "version", 1 }, { "morphs", morphs } };
	}

	Json PlayerState(const std::string& a_name, int a_salt = 0)
	{
		return Json{ { "type", "playerState" }, { "characterName", a_name }, { "appearance", Appearance(a_salt) } };
	}

	void SeedProfiles(DurableStateFileStore& a_store, int a_count, std::atomic<double>& a_now)
	{
		F4T::Identity::TokenHash hash{};
		hash.fill(3);
		for (int index = 0; index < a_count; ++index) {
			a_now.store(a_now.load() + 1.0);
			a_store.SaveResumeCredential(Account(index), hash, std::nullopt, true, 1.0e9);
			Require(a_store.UpdatePlayerState(Slot(index), PlayerState("Seed " + std::to_string(index), index)), "seed player state");
		}
		a_store.WaitForCommits();
	}

	double MedianSaveMilliseconds(DurableStateFileStore& a_store, int a_profiles, std::atomic<double>& a_now)
	{
		std::vector<double> samples;
		for (int sample = 0; sample < 15; ++sample) {
			a_now.store(a_now.load() + 1.0);
			const auto started = std::chrono::steady_clock::now();
			Require(a_store.UpdatePlayerState(Slot(sample % a_profiles), Json{ { "type", "playerState" }, { "characterName", "Timed " + std::to_string(sample) } }), "timed save");
			samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
			a_store.WaitForCommits();
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	void AProfileSaveCostsTheSameWithOneOrManyProfiles()
	{
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		const auto small = MakeDirectory("co-scoped-save-cost-small");
		const auto large = MakeDirectory("co-scoped-save-cost-large");
		DurableStateFileStore one(small / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		DurableStateFileStore many(large / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		one.SetDeferCommits(true);
		many.SetDeferCommits(true);
		SeedProfiles(one, 1, now);
		SeedProfiles(many, 2000, now);
		const auto oneMs = MedianSaveMilliseconds(one, 1, now);
		const auto manyMs = MedianSaveMilliseconds(many, 2000, now);
		std::cout << "scoped save median: 1 profile " << oneMs << " ms, 2000 profiles " << manyMs << " ms\n";
		Require(manyMs <= oneMs * 10.0 + 10.0, "a one-profile save must not scale with the number of stored profiles");
	}

	void ScopedSavesLeaveDiskMatchingMemory()
	{
		const auto directory = MakeDirectory("co-scoped-save-disk");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		Json untouched;
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
			store.SetDeferCommits(true);
			SeedProfiles(store, 4, now);
			untouched = ReadDocument(path).at("profiles").at(Slot(3));
			(void)store.InitializeInventory(Slot(0), Json::array());
			const auto request = F4T::InventorySnapshot::NormalizeRequest("op-1", 0, Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "0000000F" }, { "count", 5 } } }));
			Require(request && store.ReconcileInventory(Slot(0), *request).changed, "reconcile");
			Require(store.EnsureProfileSecret(Account(1), std::string(32, 'b')), "secret");
			Require(store.RecordSaveAttestation(Account(1), F4T::SaveProfileLogic::AttestedSave{ "Slot1.fos", std::string(64, 'c'), 1700000000 }), "attestation");
			Require(store.MutateSurvivalNeeds({ Slot(0), Slot(1), Slot(0) }, [](std::string_view, F4T::SurvivalNeeds::Record& a_record) {
				a_record.state.hunger = 0.25;
				return true;
			}, false), "survival needs");
			Require(store.UpdatePlayerStates({ { Slot(0), Json{ { "type", "playerState" }, { "characterName", "Batch A" } } },
				{ Slot(2), Json{ { "type", "playerState" }, { "characterName", "Batch C" } } } }), "batch");
			Require(store.UpdatePlayerState(Slot(1), Json{ { "type", "playerState" }, { "characterName", "Before erase" } }), "pre-erase");
			Require(store.EraseProfileByKey(Slot(2)), "whole-section erase");
			Require(store.UpdatePlayerState(Slot(1), Json{ { "type", "playerState" }, { "characterName", "After erase" } }), "post-erase");
			Require(store.UpdatePlayerState(Slot(4), PlayerState("Created")), "created profile");
			store.WaitForCommits();
			Require(!store.TakePersistenceWarning(), "no persistence warning");
		}
		const auto profiles = ReadDocument(path).at("profiles");
		Require(profiles.at(Slot(3)) == untouched, "an untouched profile is byte-for-byte preserved");
		Require(!profiles.contains(Slot(2)), "the whole-section erase stays applied after later member writes");
		Require(profiles.at(Slot(1)).at("playerState").at("characterName") == "After erase", "member write after a whole-section write wins");
		Require(profiles.at(Slot(0)).at("playerState").at("characterName") == "Batch A", "batched member write");
		Require(profiles.at(Slot(0)).at("inventory").at("revision") == 1, "inventory member write");
		Require(profiles.at(Slot(0)).at("survivalNeeds").at("hunger") == 0.25, "survival member write");
		Require(profiles.at(Account(1)).contains("saveAttestations"), "attestation member write");
		Require(profiles.at(Slot(4)).at("playerState").at("characterName") == "Created", "created profile reaches disk");

		DurableStateFileStore reopened(path, [&] { return now.load(); }, [&] { return instance; });
		Require(reopened.GetProfileSecret(Account(1)) == std::string(32, 'b'), "secret survives reopen");
		Require(reopened.GetSurvivalNeeds(Slot(1)).state.hunger == 0.25, "needs survive reopen");
		Require(reopened.GetInventory(Slot(0))->at("revision") == 1, "inventory survives reopen");
	}

	void AnUnlistedProfileChangedInPlaceIsPersisted()
	{
		const auto directory = MakeDirectory("co-scoped-save-unlisted");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedProfiles(store, 3, now);
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const bool saved = DurableStateTestAccess::MutateProfilesUnscoped(store, { Slot(0) }, [](nlohmann::json& a_root) {
			a_root.at("profiles").at(Slot(2)).at("playerState")["characterName"] = "Changed in place";
			return true;
		});
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(saved, "the unlisted in-place change is saved");
		store.WaitForCommits();
		const auto memory = store.GetPlayerState(Slot(2));
		auto disk = ReadDocument(path).at("profiles").at(Slot(2)).at("playerState");
		disk.erase("updatedAt");
		auto expected = memory.value_or(Json::object());
		expected.erase("type");
		Require(memory && disk == expected, "disk matches memory for the unlisted profile");
		Require(disk.at("characterName") == "Changed in place", "the in-place change reaches disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void AThrowingMutationRestoresEveryScopedProfile()
	{
		const auto directory = MakeDirectory("co-scoped-save-throw");
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedProfiles(store, 2, now);
		const auto before = store.GetSurvivalNeeds(Slot(0));
		bool threw = false;
		try {
			(void)store.MutateSurvivalNeeds({ Slot(0), Slot(1) }, [](std::string_view a_profile, F4T::SurvivalNeeds::Record& a_record) {
				if (a_profile.ends_with("1#0")) throw std::runtime_error("mutation fault");
				a_record.state.hunger = 0.9;
				return true;
			}, false);
		} catch (const std::runtime_error&) {
			threw = true;
		}
		Require(threw, "the mutation fault propagates");
		Require(store.GetSurvivalNeeds(Slot(0)).state.hunger == before.state.hunger, "the profile changed before the fault is restored");
		Require(store.GetSurvivalNeeds(Slot(0)).revision == before.revision, "revision restored");
	}

	void AnUnserialisableProfileIsQuarantinedAndRestored()
	{
		const auto directory = MakeDirectory("co-scoped-save-quarantine");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedProfiles(store, 2, now);
		const auto ticketBefore = store.LastPostedTicket();
		const bool saved = store.MutateSurvivalNeeds({ Slot(0) }, [](std::string_view, F4T::SurvivalNeeds::Record& a_record) {
			a_record.claims.push_back(F4T::SurvivalNeeds::Claim{ .operationId = "op", .kind = "eat", .itemKey = std::string{ "\xff\xfe" }, .baseCount = 1, .deadline = 5.0 });
			return true;
		}, false);
		Require(!saved, "the unserialisable save is refused");
		Require(store.QuarantinedSince(ticketBefore, store.LastPostedTicket()), "the refusal is reported as a quarantine");
		Require(store.GetSurvivalNeeds(Slot(0)).claims.empty(), "the refused profile is restored in memory");
		Require(store.UpdatePlayerState(Slot(1), Json{ { "type", "playerState" }, { "characterName", "Still saving" } }), "later saves still work");
		store.WaitForCommits();
		const auto document = ReadDocument(path);
		Require(document.at("profiles").at(Slot(1)).at("playerState").at("characterName") == "Still saving", "later save reaches disk");
		Require(document.dump().find("\"op\"") == std::string::npos, "the refused claim never reaches disk");
	}

	void MainThreadSaveTotalsCountSavesAndCommitWaits()
	{
		const auto directory = MakeDirectory("co-scoped-save-totals");
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedProfiles(store, 1, now);
		(void)DurableStateFileStore::TakeMainThreadSaveTotals();
		Require(store.UpdatePlayerState(Slot(0), Json{ { "type", "playerState" }, { "characterName", "Counted" } }), "counted save");
		Require(!store.UpdatePlayerState(Slot(0), Json{ { "type", "playerState" }, { "characterName", "Counted" } }), "an unchanged state is not saved");
		store.WaitForCommits();
		const auto totals = DurableStateFileStore::TakeMainThreadSaveTotals();
		Require(totals.saves == 1 && totals.commitWaits == 1 && totals.saveSeconds >= 0.0 && totals.commitWaitSeconds >= 0.0, "totals count saves and waits");
		const auto reset = DurableStateFileStore::TakeMainThreadSaveTotals();
		Require(reset.saves == 0 && reset.commitWaits == 0, "taking the totals resets them");
	}

	DurableSectionDelta Member(const std::string& a_key, Json a_value)
	{
		return DurableSectionDelta{ .name = "profiles", .value = std::optional<Json>{ std::move(a_value) }, .key = a_key };
	}

	void AnInvalidMemberWriteIsDroppedWithoutUndoingTheValidWriteBeforeIt()
	{
		const auto directory = MakeDirectory("co-scoped-pipeline-undo");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000 };
		std::mutex gateMutex;
		std::condition_variable gate;
		bool entered = false;
		bool released = false;
		bool hold = false;
		DurableWritePipeline pipeline(path, [&] { return now.load(); }, {}, [&] {
			std::unique_lock lock(gateMutex);
			if (!hold) return;
			entered = true;
			gate.notify_all();
			gate.wait(lock, [&] { return released; });
		});
		auto document = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		document["profiles"] = Json{ { "a", Json{ { "v", 0 } } } };
		pipeline.PersistDocument(document);

		pipeline.Post({ Member("b", Json{ { "v", 1 } }) }, true);
		Require(ReadDocument(path).at("profiles") == Json{ { "a", Json{ { "v", 0 } } }, { "b", Json{ { "v", 1 } } } }, "a member write changes only its key");
		pipeline.Post({ DurableSectionDelta{ .name = "profiles", .value = std::nullopt, .key = std::string{ "b" } } }, true);
		Require(ReadDocument(path).at("profiles") == Json{ { "a", Json{ { "v", 0 } } } }, "an empty member write erases only its key");

		{
			const std::scoped_lock lock(gateMutex);
			hold = true;
		}
		pipeline.Post({ DurableSectionDelta{ .name = "factions", .value = Json{ { "version", 1 } }, .key = std::nullopt } }, false);
		{
			std::unique_lock lock(gateMutex);
			gate.wait(lock, [&] { return entered; });
			hold = false;
		}
		pipeline.Post({ DurableSectionDelta{ .name = "profiles", .value = Json{ { "a", Json{ { "v", 1 } } } }, .key = std::nullopt } }, false);
		pipeline.Post({ Member("a", Json("not an object")) }, false);
		{
			const std::scoped_lock lock(gateMutex);
			released = true;
		}
		gate.notify_all();
		pipeline.WaitForCommits();
		(void)pipeline.TakeFailure();

		pipeline.Post({ Member("c", Json{ { "v", 5 } }) }, true);
		const auto profiles = ReadDocument(path).at("profiles");
		Require(profiles == Json{ { "a", Json{ { "v", 1 } } }, { "c", Json{ { "v", 5 } } } }, "only the invalid member write is dropped, the valid write before it in the batch is kept");
	}

	const std::string kActor = "camp-actor";
	const std::string kOther = "camp-other";
	const std::string kItemKey = "commonwealthonline.esp:0000997F";

	Json CampCatalog()
	{
		return Json::array({ Json{ { "plugin", "commonwealthonline.esp" }, { "localFormId", "997F" }, { "count", 1u } } });
	}

	F4T::Drops::InventoryPosition CampPosition()
	{
		return F4T::Drops::InventoryPosition{ "CAFE", "F00D", 1, 2, 3 };
	}

	Json CampWorkshop(int a_marker)
	{
		return Json{ { "camps", Json::array({ Json{ { "id", a_marker } } }) } };
	}

	Json CampSettlements()
	{
		return Json::array({ Json{ { "buildZoneId", 1 } } });
	}

	std::vector<F4T::Trade::Line> CampRefund()
	{
		return { F4T::Trade::Line{ kItemKey, 2 } };
	}

	Json Unlisted(Json a_profiles, const std::vector<std::string>& a_listed)
	{
		for (const auto& id : a_listed) a_profiles.erase(id);
		return a_profiles;
	}

	void AdmitCampProfile(DurableStateFileStore& a_store, const std::string& a_profile)
	{
		F4T::Identity::TokenHash hash{};
		hash.fill(3);
		a_store.SaveResumeCredential(a_profile, hash, std::nullopt, true, 1.0e9);
		const auto granted = a_store.InitializeInventory(a_profile, CampCatalog());
		Require(granted.at("items").contains(kItemKey), "the camp catalog item is granted");
		(void)ReconcileStartingGrantFixture(a_store, a_profile);
	}

	void SeedCampStore(DurableStateFileStore& a_store)
	{
		AdmitCampProfile(a_store, kActor);
		AdmitCampProfile(a_store, kOther);
		a_store.WaitForCommits();
	}

	Json DiskProfiles(DurableStateFileStore& a_store, const std::filesystem::path& a_path)
	{
		a_store.WaitForCommits();
		return ReadDocument(a_path).at("profiles");
	}

	Json DiskLiveDrops(DurableStateFileStore& a_store, const std::filesystem::path& a_path)
	{
		a_store.WaitForCommits();
		return ReadDocument(a_path).at("drops").at("live");
	}

	void ACampStoreWritesNoProfile()
	{
		const auto directory = MakeDirectory("co-camp-store-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		const auto ticketBefore = store.LastPostedTicket();
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const bool stored = store.CommitCampStore(CampWorkshop(2), kActor, kItemKey, CampPosition());
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(stored, "the camp store commits");
		Require(DurableStateTestAccess::Profiles(store) == memoryBefore, "a camp store changes no profile in memory");
		Require(store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId).at("ownerProfileId") == kActor, "the camp store places the drop for the actor");
		Require(store.GetWorkshop() == CampWorkshop(2), "the camp store writes the workshop");
		Require(DiskProfiles(store, path) == diskBefore, "a camp store leaves every profile unchanged on disk");
		Require(DiskLiveDrops(store, path).contains(std::to_string(F4T::Drops::kFirstDroppedEntityId)), "the camp drop reaches disk");
		Require(!store.QuarantinedSince(ticketBefore, store.LastPostedTicket()), "the camp store batch is not quarantined");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void AConsumedLiveDropChangesNoProfile()
	{
		const auto directory = MakeDirectory("co-camp-consume-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		const auto ticketBefore = store.LastPostedTicket();
		Require(store.CommitCampStore(CampWorkshop(2), kActor, kItemKey, CampPosition()), "a live drop to consume");
		Require(DiskLiveDrops(store, path).contains(std::to_string(F4T::Drops::kFirstDroppedEntityId)), "the camp drop reaches disk before it is consumed");
		Require(!store.QuarantinedSince(ticketBefore, store.LastPostedTicket()), "the seeding camp store batch is not quarantined");
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const bool consumed = store.ConsumeLiveDrop(F4T::Drops::kFirstDroppedEntityId);
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(consumed, "the live drop is consumed");
		Require(DurableStateTestAccess::Profiles(store) == memoryBefore, "consuming a drop changes no profile in memory");
		Require(store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId).is_null(), "the consumed drop is gone");
		Require(DiskProfiles(store, path) == diskBefore, "consuming a drop leaves every profile unchanged on disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void ACampCommitChangesOnlyTheActor()
	{
		const auto directory = MakeDirectory("co-camp-commit-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		Require(store.CommitCampStore(CampWorkshop(2), kActor, kItemKey, CampPosition()), "a live drop to commit");
		const auto expectedDrop = store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId);
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		const auto revisionBefore = store.GetInventory(kActor)->at("revision").get<std::uint32_t>();
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const auto commit = store.CommitCamp(F4T::Drops::kFirstDroppedEntityId, expectedDrop, CampWorkshop(3), CampSettlements(), kActor, 7, CampRefund());
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(commit.outcome == F4T::CampGrants::Outcome::Committed, "the camp commits");
		Require(Unlisted(DurableStateTestAccess::Profiles(store), { kActor }) == Unlisted(memoryBefore, { kActor }), "a camp commit changes no other profile in memory");
		Require(store.GetInventory(kActor)->at("items").at(kItemKey).at("count") == 3, "the refund is credited to the actor");
		Require(store.GetInventory(kActor)->at("revision").get<std::uint32_t>() == revisionBefore + 1, "the actor's inventory revision advances");
		Require(store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId).is_null(), "the camp consumes the drop");
		Require(store.GetWorkshop() == CampWorkshop(3), "the camp writes the workshop");
		Require(store.FindCampMove(kActor, 7).has_value(), "the camp move is recorded for the actor");
		const auto disk = DiskProfiles(store, path);
		Require(disk.at(kOther) == diskBefore.at(kOther), "the other player is unchanged on disk");
		Require(Unlisted(disk, { kActor }) == Unlisted(diskBefore, { kActor }), "a camp commit leaves every other profile unchanged on disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void ACampMoveChangesOnlyTheActor()
	{
		const auto directory = MakeDirectory("co-camp-move-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const auto commit = store.CommitCampMove(CampWorkshop(4), kActor, 9, CampRefund());
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(commit.outcome == F4T::CampGrants::Outcome::Committed, "the camp move commits");
		Require(commit.pending, "the camp move waits for the actor's acknowledgement");
		Require(Unlisted(DurableStateTestAccess::Profiles(store), { kActor }) == Unlisted(memoryBefore, { kActor }), "a camp move changes no other profile in memory");
		Require(store.GetInventory(kActor)->at("items").at(kItemKey).at("count") == 3, "the moved items are credited to the actor");
		Require(store.GetWorkshop() == CampWorkshop(4), "the camp move writes the workshop");
		Require(store.PendingCampGrants(kActor).size() == 1, "one camp grant is pending for the actor");
		const auto disk = DiskProfiles(store, path);
		Require(disk.at(kOther) == diskBefore.at(kOther), "the other player is unchanged on disk after a camp move");
		Require(Unlisted(disk, { kActor }) == Unlisted(diskBefore, { kActor }), "a camp move leaves every other profile unchanged on disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void AnAckedCampGrantChangesOnlyTheActor()
	{
		const auto directory = MakeDirectory("co-camp-ack-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		Require(store.CommitCampMove(CampWorkshop(4), kActor, 9, CampRefund()).outcome == F4T::CampGrants::Outcome::Committed, "a camp grant to acknowledge");
		store.WaitForCommits();
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const bool acked = store.AckCampGrant(kActor, 9);
		const bool repeated = store.AckCampGrant(kActor, 9);
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(acked && !repeated, "a camp grant is acknowledged exactly once");
		Require(store.PendingCampGrants(kActor).empty(), "no camp grant stays pending");
		Require(store.FindCampMove(kActor, 9)->pending == false, "the acknowledged move is no longer pending");
		Require(Unlisted(DurableStateTestAccess::Profiles(store), { kActor }) == Unlisted(memoryBefore, { kActor }), "an acknowledgement changes no other profile in memory");
		Require(store.GetInventory(kActor)->at("items").at(kItemKey).at("count") == 3, "the acknowledged items stay with the actor");
		const auto disk = DiskProfiles(store, path);
		Require(disk.at(kOther) == diskBefore.at(kOther), "the other player is unchanged on disk after an acknowledgement");
		Require(Unlisted(disk, { kActor }) == Unlisted(diskBefore, { kActor }), "an acknowledgement leaves every other profile unchanged on disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void ACampRefundChangesOnlyTheActor()
	{
		const auto directory = MakeDirectory("co-camp-refund-scope");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		store.SetDeferCommits(true);
		SeedCampStore(store);
		const auto memoryBefore = DurableStateTestAccess::Profiles(store);
		const auto diskBefore = DiskProfiles(store, path);
		DurableStateTestAccess::AuditUnlistedProfiles(store, true);
		const auto refund = store.CommitChargeRefund(kActor, 11, CampRefund());
		DurableStateTestAccess::AuditUnlistedProfiles(store, false);
		Require(refund.outcome == F4T::CampGrants::Outcome::Committed, "the charge refund commits");
		Require(Unlisted(DurableStateTestAccess::Profiles(store), { kActor }) == Unlisted(memoryBefore, { kActor }), "a charge refund changes no other profile in memory");
		Require(store.GetInventory(kActor)->at("items").at(kItemKey).at("count") == 3, "the refund is credited to the actor");
		Require(store.FindCampMove(kActor, 11).has_value(), "the charge refund is recorded for the actor");
		const auto disk = DiskProfiles(store, path);
		Require(disk.at(kOther) == diskBefore.at(kOther), "the other player is unchanged on disk after a charge refund");
		Require(Unlisted(disk, { kActor }) == Unlisted(diskBefore, { kActor }), "a charge refund leaves every other profile unchanged on disk");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	void AContainerTakeWithNothingHeldWritesNothing()
	{
		const auto directory = MakeDirectory("co-scoped-save-container-take");
		const auto path = directory / "durable-state.json";
		const std::string instance = "0123456789abcdef0123456789abcdef";
		std::atomic<double> now{ 1000 };
		DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
		AdmitCampProfile(store, kActor);
		const F4T::Drops::InventoryPosition position{ "0000CAFE", "F00D", 1, 2, 3 };
		const auto request = [](const char* a_operationId, std::uint32_t a_revision, const char* a_direction, const char* a_refId) {
			return Json{ { "type", "inventoryContainerRequest" }, { "operationId", a_operationId }, { "expectedRevision", a_revision },
				{ "direction", a_direction }, { "container", Json{ { "cellId", "0000CAFE" }, { "refId", a_refId } } },
				{ "itemKey", kItemKey }, { "count", 1u } };
		};
		const auto put = store.TransferContainer(kActor, request("put-1", 0u, "put", "00001234"), position);
		Require(put.outcome == F4T::Drops::Outcome::Committed && put.packet.at("committedCount") == 1u, "the put moves the item into the container");
		const auto ticketBefore = store.LastPostedTicket();
		const auto diskBefore = DiskProfiles(store, path);
		const auto empty = store.TransferContainer(kActor, request("take-empty", 1u, "take", "00005678"), position);
		Require(empty.outcome == F4T::Drops::Outcome::Committed && empty.packet.at("committedCount") == 0u && empty.packet.at("revision") == 1u,
			"a take of nothing held commits zero at the current revision");
		Require(store.LastPostedTicket() == ticketBefore, "a take of nothing held posts no save");
		Require(DiskProfiles(store, path) == diskBefore, "a take of nothing held leaves every profile unchanged on disk");
		const auto taken = store.TransferContainer(kActor, request("take-1", 1u, "take", "00001234"), position);
		Require(taken.outcome == F4T::Drops::Outcome::Committed && taken.packet.at("committedCount") == 1u, "the put item is taken back from the container");
		Require(store.LastPostedTicket() != ticketBefore, "a take that moves an item posts a save");
		Require(!store.TakePersistenceWarning(), "no persistence warning");
	}

	Json ContainerMove(const std::string& a_operationId, std::uint32_t a_revision, const char* a_direction)
	{
		return Json{ { "type", "inventoryContainerRequest" }, { "operationId", a_operationId }, { "expectedRevision", a_revision },
			{ "direction", a_direction }, { "container", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
			{ "itemKey", kItemKey }, { "count", 1u } };
	}

	std::uint32_t Revision(DurableStateFileStore& a_store, const std::string& a_profile)
	{
		return a_store.GetInventory(a_profile)->at("revision").get<std::uint32_t>();
	}

	std::uint32_t Moved(DurableStateFileStore& a_store, const std::string& a_profile, const char* a_operationId, const char* a_direction)
	{
		const auto result = a_store.TransferContainer(a_profile, ContainerMove(a_operationId, Revision(a_store, a_profile), a_direction),
			F4T::Drops::InventoryPosition{ "0000CAFE", "F00D", 1, 2, 3 });
		return result.outcome == F4T::Drops::Outcome::Committed ? result.packet.value("committedCount", 0u) : 0u;
	}

	void Refused(DurableStateFileStore& a_store, const std::string& a_profile, const char* a_operationId, const char* a_direction, const char* a_reason)
	{
		a_store.NoteRefusedContainerMove(a_profile, ContainerMove(a_operationId, Revision(a_store, a_profile), a_direction), F4T::Drops::InventoryPosition{ "0000CAFE", "F00D", 1, 2, 3 }, a_reason);
	}

	bool SnapshotHeld(DurableStateFileStore& a_store, const std::string& a_profile, std::uint32_t a_count)
	{
		static int serial = 0;
		Json items = Json::array();
		const auto inventory = a_store.GetInventory(a_profile);
		for (const auto& entry : inventory->at("items").items()) {
			if (entry.key() != kItemKey) items.push_back(entry.value());
		}
		if (a_count != 0) items.push_back(Json{ { "plugin", "commonwealthonline.esp" }, { "localFormId", "0000997F" }, { "count", a_count } });
		const auto request = F4T::InventorySnapshot::NormalizeRequest("claim-snap-" + std::to_string(++serial), Revision(a_store, a_profile), items);
		Require(request.has_value(), "the claim snapshot is well formed");
		return a_store.ReconcileInventory(a_profile, *request).changed;
	}

	void AdmitBoth(DurableStateFileStore& a_store)
	{
		AdmitCampProfile(a_store, kActor);
		AdmitCampProfile(a_store, kOther);
	}

	void ADroppedTakeIsSettledFromTheSnapshotOnce()
	{
		const auto directory = MakeDirectory("co-container-claim-settle");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		Refused(store, kOther, "take-r", "take", "rateLimited");
		Require(SnapshotHeld(store, kOther, 2), "the client snapshot shows the item the refused take gave it");
		Require(Moved(store, kActor, "take-late", "take") == 0, "the settled take leaves nothing in the container for a second player");
		Require(Moved(store, kOther, "put-back", "put") == 1, "the taker puts one back");
		Require(SnapshotHeld(store, kOther, 2), "a second increase is reported");
		Require(Moved(store, kActor, "take-back", "take") == 1, "a second increase is not credited against the settled claim");
	}

	void AnUnconfirmedTakeRollsBackAndOtherRefusalsRecordNothing()
	{
		const auto directory = MakeDirectory("co-container-claim-expiry");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		Refused(store, kOther, "take-v", "take", "vetoed");
		Require(SnapshotHeld(store, kOther, 2), "the increase is reported");
		Require(Moved(store, kActor, "take-1", "take") == 1, "a veto records no claim, so the item stays in the container");
		Require(Moved(store, kOther, "put-2", "put") == 1, "the item is back in the container");
		Refused(store, kOther, "take-e", "take", "rateLimited");
		now.store(1061);
		Require(SnapshotHeld(store, kOther, 2), "the increase arrives after the window");
		Require(Moved(store, kActor, "take-2", "take") == 1, "an expired claim leaves the item in the container");
	}

	void APutOrAStaleTakeRecordsNoClaim()
	{
		const auto directory = MakeDirectory("co-container-claim-put");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Refused(store, kActor, "put-r", "put", "rateLimited");
		Require(SnapshotHeld(store, kActor, 0), "the client snapshot shows the item left the inventory");
		Require(Moved(store, kOther, "take-put", "take") == 0, "a refused put is never credited to the container");
		Require(Moved(store, kOther, "put-1", "put") == 1, "the other player puts the item in the container");
		Refused(store, kActor, "take-s", "take", "staleRevision");
		Require(SnapshotHeld(store, kActor, 2), "an increase is reported");
		Require(Moved(store, kOther, "take-after", "take") == 1, "a stale take records no claim, so the item stays in the container");
	}

	void AnIdRefusedThreeTimesThenCommittedLeavesNoClaim()
	{
		const auto directory = MakeDirectory("co-container-claim-ops");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		for (int i = 0; i < 3; ++i) Refused(store, kOther, "take-r", "take", "rateLimited");
		Require(Moved(store, kOther, "take-r", "take") == 1, "the same operation id commits the take");
		Require(Moved(store, kOther, "put-back", "put") == 1, "the taker puts the item back");
		Require(SnapshotHeld(store, kOther, 2), "an unrelated increase is reported");
		Require(Moved(store, kActor, "take-1", "take") == 1, "three refusals of one id left no claim behind");
	}

	void AClaimOutsideThePlayersCellIsNotRecorded()
	{
		const auto directory = MakeDirectory("co-container-claim-scope");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		const auto take = ContainerMove("take-far", Revision(store, kOther), "take");
		store.NoteRefusedContainerMove(kOther, take, F4T::Drops::InventoryPosition{ "0000BEEF", "F00D", 1, 2, 3 }, "rateLimited");
		store.NoteRefusedContainerMove(kOther, take, F4T::Drops::InventoryPosition{}, "rateLimited");
		Require(SnapshotHeld(store, kOther, 2), "a real gain of the same item is reported");
		Require(Moved(store, kActor, "take-1", "take") == 1, "a claim against a container in another cell was not recorded and debited nothing");
	}

	void AStaleRefusalReleasesTheClaimOfThatOperationId()
	{
		const auto directory = MakeDirectory("co-container-claim-stale-release");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		Refused(store, kOther, "take-x", "take", "rateLimited");
		Refused(store, kOther, "take-x", "take", "staleRevision");
		Require(SnapshotHeld(store, kOther, 2), "an unrelated increase is reported");
		Require(Moved(store, kActor, "take-1", "take") == 1, "a stale refusal released the claim of an id that will never commit");
	}

	void ALateResendAfterASessionResetReplaysAndRecordsNoClaim()
	{
		const auto directory = MakeDirectory("co-container-claim-late");
		const auto path = directory / "durable-state.json";
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		const F4T::Drops::InventoryPosition position{ "0000CAFE", "F00D", 1, 2, 3 };
		Json take;
		Json first;
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
			AdmitBoth(store);
			Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
			take = ContainerMove("take-late", Revision(store, kOther), "take");
			const auto result = store.TransferContainer(kOther, take, position);
			Require(result.outcome == F4T::Drops::Outcome::Committed && result.packet.at("committedCount") == 1u, "the take commits");
			first = result.packet;
			store.WaitForCommits();
		}
		DurableStateFileStore reopened(path, [&] { return now.load(); }, [&] { return instance; });
		const auto resent = reopened.TransferContainer(kOther, take, position);
		Require(resent.outcome == F4T::Drops::Outcome::Replayed && resent.packet == first, "a carried take resent after a session reset replays the stored result");
		reopened.NoteRefusedContainerMove(kOther, take, position, "rateLimited");
		Require(Moved(reopened, kOther, "put-back", "put") == 1, "the taker puts the item back");
		Require(SnapshotHeld(reopened, kOther, 2), "an unrelated increase is reported");
		Require(Moved(reopened, kActor, "take-1", "take") == 1, "the late resend wrote no second take and left no claim");
	}

	void AClaimSurvivesARestart()
	{
		const auto directory = MakeDirectory("co-container-claim-restart");
		const auto path = directory / "durable-state.json";
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, [&] { return instance; });
			AdmitBoth(store);
			Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
			Refused(store, kOther, "take-r", "take", "rateLimited");
			store.WaitForCommits();
			Require(!store.TakePersistenceWarning(), "no persistence warning");
		}
		DurableStateFileStore reopened(path, [&] { return now.load(); }, [&] { return instance; });
		Require(SnapshotHeld(reopened, kOther, 2), "the client snapshot arrives after the restart");
		Require(Moved(reopened, kActor, "take-late", "take") == 0, "the claim settled after the restart");
	}

	void ADuplicateOperationAppliesOnce()
	{
		const auto directory = MakeDirectory("co-container-claim-duplicate");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		const F4T::Drops::InventoryPosition position{ "0000CAFE", "F00D", 1, 2, 3 };
		const auto put = ContainerMove("dup-put", Revision(store, kActor), "put");
		const auto first = store.TransferContainer(kActor, put, position);
		Require(first.outcome == F4T::Drops::Outcome::Committed && first.packet.at("committedCount") == 1u, "the put applies");
		const auto revision = Revision(store, kActor);
		const auto ticket = store.LastPostedTicket();
		const auto resent = store.TransferContainer(kActor, put, position);
		Require(resent.outcome == F4T::Drops::Outcome::Replayed && resent.packet == first.packet, "a resent operation id replays the stored result");
		Require(Revision(store, kActor) == revision && store.LastPostedTicket() == ticket, "a replay changes and saves nothing");
		const auto take = ContainerMove("dup-take", Revision(store, kOther), "take");
		const auto taken = store.TransferContainer(kOther, take, position);
		Require(taken.outcome == F4T::Drops::Outcome::Committed && taken.packet.at("committedCount") == 1u, "the item is taken once");
		Require(store.TransferContainer(kOther, take, position).outcome == F4T::Drops::Outcome::Replayed, "a resent take replays");
		Require(Moved(store, kActor, "take-after", "take") == 0, "the replayed put put one item in the ledger, not two");
		Require(Moved(store, kOther, "put-back", "put") == 1, "the taker puts the item back");
		store.NoteRefusedContainerMove(kOther, take, position, "rateLimited");
		Require(SnapshotHeld(store, kOther, 2), "an unrelated increase is reported");
		Require(Moved(store, kActor, "take-final", "take") == 1, "a refused resend of an applied operation records no claim");
	}

	void ACommittedRetryReleasesTheClaim()
	{
		const auto directory = MakeDirectory("co-container-claim-retry");
		std::atomic<double> now{ 1000 };
		const std::string instance = "0123456789abcdef0123456789abcdef";
		DurableStateFileStore store(directory / "durable-state.json", [&] { return now.load(); }, [&] { return instance; });
		AdmitBoth(store);
		Require(Moved(store, kActor, "put-1", "put") == 1, "the actor puts the item in the container");
		Refused(store, kOther, "take-r", "take", "rateLimited");
		Require(Moved(store, kOther, "take-r", "take") == 1, "the same-id retry commits the take");
		Require(Moved(store, kOther, "put-back", "put") == 1, "the taker puts the item back");
		Require(SnapshotHeld(store, kOther, 2), "an unrelated increase is reported");
		Require(Moved(store, kActor, "take-1", "take") == 1, "the retried take left no claim to drain the container");
	}
}

int main()
{
	try {
		AnInvalidMemberWriteIsDroppedWithoutUndoingTheValidWriteBeforeIt();
		MainThreadSaveTotalsCountSavesAndCommitWaits();
		AThrowingMutationRestoresEveryScopedProfile();
		AnUnserialisableProfileIsQuarantinedAndRestored();
		ScopedSavesLeaveDiskMatchingMemory();
		AnUnlistedProfileChangedInPlaceIsPersisted();
		AProfileSaveCostsTheSameWithOneOrManyProfiles();
		ACampStoreWritesNoProfile();
		AConsumedLiveDropChangesNoProfile();
		ACampCommitChangesOnlyTheActor();
		ACampMoveChangesOnlyTheActor();
		AnAckedCampGrantChangesOnlyTheActor();
		ACampRefundChangesOnlyTheActor();
		AContainerTakeWithNothingHeldWritesNothing();
		ADroppedTakeIsSettledFromTheSnapshotOnce();
		AnUnconfirmedTakeRollsBackAndOtherRefusalsRecordNothing();
		APutOrAStaleTakeRecordsNoClaim();
		AClaimSurvivesARestart();
		ACommittedRetryReleasesTheClaim();
		AnIdRefusedThreeTimesThenCommittedLeavesNoClaim();
		AClaimOutsideThePlayersCellIsNotRecorded();
		AStaleRefusalReleasesTheClaimOfThatOperationId();
		ALateResendAfterASessionResetReplaysAndRecordsNoClaim();
		ADuplicateOperationAppliesOnce();
	} catch (const std::exception& error) {
		std::cerr << "unexpected exception: " << error.what() << '\n';
		return 1;
	}
	std::cout << "DurableProfileScopedSaveTests passed\n";
	return 0;
}
