#include "DurableStateFileStore.h"
#include "PersistenceGate.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

int main()
{
	using CommonwealthOnline::Server::DurableStateFileStore;
	using F4T::SaveProfileLogic::AttestedSave;
	using Json = nlohmann::json;
	const auto directory = std::filesystem::temp_directory_path() /
		("co-grant-recovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto path = directory / "state.json";
	const auto clock = [] { return 1000.0; };
	const auto id = [] { return "0123456789abcdef0123456789abcdef"; };
	const AttestedSave before{ "Save_Character_1", std::string(64, 'a'), 0 };
	const AttestedSave after{ "Save_Character_2", std::string(64, 'b'), 0 };
	{
		DurableStateFileStore store(path, clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("gate", hash, std::nullopt, true, 5000.0);
		assert(store.RecordSaveAttestation("gate", before));
		CommonwealthOnline::Server::PersistenceGate gate(store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		const auto readyText = Json{ { "type", "sessionReady" }, { "profileId", "gate" }, { "playerId", 1 }, { "resumeToken", "opaque" } }.dump();
		CommonwealthOnline::Server::OutboundFrame ready{ 10, CommonwealthOnline::Server::Delivery::ReliableOrdered, { readyText.begin(), readyText.end() } };
		std::string error;
		assert(gate.PrepareSessionReady(ready, error));
		assert(Json::parse(ready.payload).at("startingInventoryRecovery").at("state") == "unstarted");
		const auto notice = gate.TakeStartingGrantFrames(10);
		assert(notice.size() == 1 && notice[0].delivery == CommonwealthOnline::Server::Delivery::ReliableOrdered && Json::parse(notice[0].payload).at("state") == "unstarted");
		(void)gate.SessionReadyDelivered(ready, clock());
		assert(gate.BeginStartingGrant(10, "wrong", "Character", before).empty());
		const auto frames = gate.BeginStartingGrant(10, "gate", "Character", before);
		assert(frames.size() == 1 && Json::parse(frames[0].payload).at("type") == "startingInventoryGrant");
		assert(gate.BeginStartingGrant(10, "gate", "Character", before).empty());
	}
	{
		DurableStateFileStore restarted(path, clock, id);
		assert(restarted.PendingStartingGrant("gate")->at("state") == "intent");
		assert(!restarted.BeginStartingGrant("gate", "Character", before));
		CommonwealthOnline::Server::PersistenceGate gate(restarted);
		const auto readyText = Json{ { "type", "sessionReady" }, { "profileId", "gate" }, { "playerId", 1 }, { "resumeToken", "opaque" } }.dump();
		CommonwealthOnline::Server::OutboundFrame ready{ 11, CommonwealthOnline::Server::Delivery::ReliableOrdered, { readyText.begin(), readyText.end() } };
		std::string error;
		assert(gate.PrepareSessionReady(ready, error));
		assert(Json::parse(ready.payload).at("startingInventoryRecovery").at("state") == "intent");
		const auto notice = gate.TakeStartingGrantFrames(11);
		assert(notice.size() == 1 && Json::parse(notice[0].payload).at("state") == "intent");
		(void)gate.SessionReadyDelivered(ready, clock());
		assert(gate.BeginStartingGrant(11, "gate", "Character", before).empty());
	}
	{
		DurableStateFileStore store(path, clock, id);
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile", hash, std::nullopt, true, 5000.0);
		(void)store.InitializeInventory("profile", Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		assert(!store.AckStartingGrant("profile", "starting-inventory"));
		assert(!store.BeginStartingGrant("profile", "Other", before));
		assert(!store.BeginStartingGrant("profile", "Character", before));
		assert(store.RecordSaveAttestation("profile", before));
		assert(store.BeginStartingGrant("profile", "Character", before));
		assert(!store.BeginStartingGrant("profile", "Character", before));
	}
	{
		DurableStateFileStore restarted(path, clock, id);
		assert(restarted.PendingStartingGrant("profile"));
		assert(!restarted.AckStartingGrant("profile", "starting-inventory"));
		assert(!restarted.ResolveStartingGrant("profile", "Other", after, "inspected", false));
		assert(!restarted.ResolveStartingGrant("profile", "Character", before, "inspected", false));
		assert(!restarted.ResolveStartingGrant("profile", "Character", after, "", false));
		assert(!restarted.ResolveStartingGrant("profile", "Character", after, "inspected", true));
		assert(restarted.ResolveStartingGrant("profile", "Character", after, "inspected save and inventory", false));
	}
	{
		DurableStateFileStore restarted(path, clock, id);
		assert(!restarted.PendingStartingGrant("profile"));
		assert(!restarted.ResolveStartingGrant("profile", "Character", after, "again", false));
		CommonwealthOnline::Server::PersistenceGate gate(restarted);
		const auto readyText = Json{ { "type", "sessionReady" }, { "profileId", "profile" }, { "playerId", 1 }, { "resumeToken", "opaque" } }.dump();
		CommonwealthOnline::Server::OutboundFrame ready{ 23, CommonwealthOnline::Server::Delivery::ReliableOrdered, { readyText.begin(), readyText.end() } };
		std::string error;
		assert(gate.PrepareSessionReady(ready, error));
		const auto recovery = Json::parse(ready.payload).at("startingInventoryRecovery");
		assert(recovery.at("state") == "resolved" && recovery.at("saveFile") == after.saveFile && recovery.at("sha256") == after.sha256Hex && recovery.at("characterId") == "Character");
		gate.RebindConnectionProfile(20, "profile");
		assert(!gate.InventorySaveReady(20));
		assert(!gate.VerifyStartingGrantSave(20, before));
		assert(!gate.InventorySaveReady(20));
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		bool failedCommit = false;
		try { (void)gate.VerifyStartingGrantSave(20, after); } catch (const std::exception&) { failedCommit = true; }
		assert(failedCommit && !gate.InventorySaveReady(20));
		assert(!restarted.GetStartingGrant("profile")->at("recovery").value("consumed", false));
		std::filesystem::remove_all(backup);
		assert(gate.VerifyStartingGrantSave(20, after));
		assert(gate.InventorySaveReady(20));
		assert(gate.VerifyStartingGrantSave(20, after));
		assert(restarted.GetStartingGrant("profile")->at("recovery").at("consumed") == true);
		gate.RebindConnectionProfile(20, "profile");
		assert(!gate.InventorySaveReady(20));
		assert(!gate.VerifyStartingGrantSave(20, after));
		assert(restarted.ResolveStartingGrant("profile", "Character", after, "inspected after rebind", false));
		assert(gate.VerifyStartingGrantSave(20, after));
		{
			DurableStateFileStore afterRestart(path, clock, id);
			CommonwealthOnline::Server::PersistenceGate restartedGate(afterRestart);
			restartedGate.RebindConnectionProfile(25, "profile");
			assert(!restartedGate.VerifyStartingGrantSave(25, after));
			assert(!afterRestart.PendingStartingGrant("profile"));
		}
		gate.ConnectionClosed(20);
		gate.RebindConnectionProfile(21, "profile");
		assert(!gate.VerifyStartingGrantSave(21, after));
		gate.ConnectionClosed(21);
		const auto consumedReadyText = Json{ { "type", "sessionReady" }, { "profileId", "profile" }, { "playerId", 1 }, { "resumeToken", "opaque" } }.dump();
		CommonwealthOnline::Server::OutboundFrame consumedReady{ 24, CommonwealthOnline::Server::Delivery::ReliableOrdered, { consumedReadyText.begin(), consumedReadyText.end() } };
		assert(gate.PrepareSessionReady(consumedReady, error));
		assert(Json::parse(consumedReady.payload).at("startingInventoryRecovery").at("state") == "consumed");
		assert(restarted.ResolveStartingGrant("profile", "Character", after, "inspected unchanged save", false));
		gate.RebindConnectionProfile(20, "profile");
		assert(gate.VerifyStartingGrantSave(20, after));
		try { (void)gate.VerifyStartingGrantSave(20, AttestedSave{ "bad", "bad", 0 }); } catch (const std::exception&) {}
		assert(!gate.InventorySaveReady(20));
		assert(!gate.VerifyStartingGrantSave(20, after));
		gate.ConnectionClosed(20);
		assert(restarted.ResolveStartingGrant("profile", "Character", after, "inspected after invalid request", false));
		gate.RebindConnectionProfile(20, "profile");
		assert(gate.VerifyStartingGrantSave(20, after));
		const auto updated = F4T::InventorySnapshot::NormalizeRequest("snapshot", 0, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 4 } } }));
		assert(updated && gate.ReconcileInventory(20, *updated)->changed);
		gate.ConnectionClosed(20);
		assert(!gate.InventorySaveReady(20));
		gate.RebindConnectionProfile(21, "profile");
		assert(!gate.VerifyStartingGrantSave(21, after));
		gate.ConnectionClosed(21);
		const AttestedSave later{ "Save_Character_3", std::string(64, 'c'), 0 };
		assert(restarted.ResolveStartingGrant("profile", "Character", later, "inspected later save", false));
		assert(restarted.GetStartingGrant("profile")->at("recovery").at("inventoryRevision") == 1);
		gate.RebindConnectionProfile(22, "profile");
		assert(!gate.VerifyStartingGrantSave(22, after));
		assert(gate.VerifyStartingGrantSave(22, later));
	}
	{
		auto root = F4T::DurableState::NewRoot(id(), clock());
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(root, "ambiguous", hash, std::nullopt, true, 5000.0, clock());
		(void)F4T::DurableState::InitializeInventory(root, "ambiguous", Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }), clock());
		F4T::DurableState::RecordSaveAttestation(root, "ambiguous", before, clock());
		F4T::DurableState::RecordSaveAttestation(root, "ambiguous", after, clock());
		assert(!F4T::DurableState::BeginStartingGrant(root, "ambiguous", "Character", before));
		assert(!F4T::DurableState::BeginStartingGrant(root, "ambiguous", "Character", after));
		assert(F4T::DurableState::ResolveStartingGrant(root, "ambiguous", "Character", after, "manually reconciled inventory", false));
		assert(root.at("profiles").at("ambiguous").at("startingGrant").at("recovery").at("intent").is_null());
		F4T::DurableState::ValidateRoot(root);
	}
	{
		auto root = F4T::DurableState::NewRoot(id(), clock());
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(root, "legacy", hash, std::nullopt, true, 5000.0, clock());
		(void)F4T::DurableState::InitializeInventory(root, "legacy", Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }), clock());
		assert(!F4T::DurableState::ResolveStartingGrant(root, "legacy", "Character", after, "", false));
		auto& grant = root["profiles"]["legacy"]["startingGrant"];
		grant.erase("recovery");
		grant["granted"] = true;
		assert(F4T::DurableState::PendingStartingGrant(root, "legacy"));
		assert(!F4T::DurableState::ResolveStartingGrant(root, "legacy", "Character", after, "inspected", false));
		root["inventoryAuthorityBlocked"] = true;
		assert(!F4T::DurableState::ResolveStartingGrant(root, "legacy", "Character", after, "inspected", true));
		root["inventoryAuthorityBlocked"] = false;
		assert(F4T::DurableState::ResolveStartingGrant(root, "legacy", "Character", after, "inspected legacy save", true));
		F4T::DurableState::ValidateRoot(root);
		assert(grant.at("recovery").at("intent").is_null());
		grant["recovery"]["sha256"] = "bad";
		bool invalidRecovery = false;
		try { F4T::DurableState::ValidateRoot(root); } catch (const std::exception&) { invalidRecovery = true; }
		assert(invalidRecovery);
		grant["recovery"]["sha256"] = after.sha256Hex;
		root["profiles"]["legacy"]["resume"] = "invalid";
		bool rejected = false;
		try { F4T::DurableState::ValidateRoot(root); } catch (const std::exception&) { rejected = true; }
		assert(rejected);
	}
	std::filesystem::remove_all(directory);
}
