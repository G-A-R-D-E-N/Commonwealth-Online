#include "ServerFactionTestSupport.h"
#include "AdminCommandDispatcher.h"
#include "F4TInventorySnapshot.h"
#include "F4TSaveProfileLogic.h"
#include "SaveRecoveryAdmin.h"

#include <cassert>
#include <fstream>
#include <functional>
#include <memory>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::AdminCommandDispatcher;
using CommonwealthOnline::Server::AdminCommandHooks;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

namespace
{
	constexpr const char* kAccount = "co-override-acct";
	constexpr const char* kSave = "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2";
	constexpr const char* kUnmatchedSave = "Save8_BBBB2222_Nomad_Commonwealth_20260814000001_1_2";

	struct Fixture
	{
		std::filesystem::path directory;
		std::unique_ptr<DurableStateFileStore> store;
		std::unique_ptr<PersistenceGate> gate;
		std::unique_ptr<ProtocolHost> host;
	};

	Json Catalog()
	{
		return Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
	}

	Fixture OpenFixture(const std::filesystem::path& a_directory)
	{
		Fixture fixture;
		fixture.directory = a_directory;
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		fixture.store = std::make_unique<DurableStateFileStore>(a_directory / "durable-state.json", clock, instanceId);
		fixture.gate = std::make_unique<PersistenceGate>(*fixture.store, Catalog());
		fixture.host = std::make_unique<ProtocolHost>("First Join Admin Override");
		assert(fixture.host->EnableCharacterSlots(fixture.gate.get(), 3));
		F4T::Identity::TokenHash hash{};
		hash[0] = 1;
		fixture.store->SaveResumeCredential(kAccount, hash, std::nullopt, true, 2000.0);
		return fixture;
	}

	std::vector<OutboundFrame> Reconnect(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, kAccount);
		return a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), a_monotonicSeconds, a_unixTimeSeconds);
	}

	void ExpectOk(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_op)
	{
		const auto result = RequirePacket(a_frames, a_connectionId, "characterResult");
		assert(result.at("op") == std::string{ a_op });
		assert(result.at("ok") == true);
	}

	std::filesystem::path SeedStartedCharacter(std::string_view a_tag)
	{
		const auto directory = TempDirectory(a_tag);
		const auto key = std::string{ kAccount } + "#1";
		{
			auto fixture = OpenFixture(directory);
			(void)RequirePacket(Reconnect(*fixture.host, 10, 1.0, 101.0), 10, "sessionReady");
			ExpectOk(Send(*fixture.host, 10, Json{ { "type", "characterCreate" }, { "slot", 1 }, { "name", "Hero" } }, 2.0, 102.0), 10, "create");
			ExpectOk(Send(*fixture.host, 10, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 3.0, 103.0), 10, "select");
			fixture.host->HandleDisconnected(10, 103.5);
			(void)RequirePacket(Reconnect(*fixture.host, 11, 4.0, 104.0), 11, "sessionReady");
			assert(fixture.store->InitializeInventory(key, Catalog()).at("initialized") == true);
			fixture.host->HandleDisconnected(11, 105.0);
			fixture.store->WaitForCommits();
		}
		const auto path = directory / "durable-state.json";
		Json root;
		{
			std::ifstream input(path);
			root = Json::parse(input);
		}
		root["profiles"][key].erase("startingGrant");
		for (std::uint32_t revision = 0; revision < 3; ++revision) {
			const auto items = revision % 2 == 0 ? Json::array() : Catalog();
			const auto request = F4T::InventorySnapshot::NormalizeRequest("revision-" + std::to_string(revision), revision, items);
			assert(request && F4T::InventorySnapshot::Reconcile(root, key, *request, {}).changed);
		}
		std::ofstream output(path, std::ios::trunc);
		output << root.dump(2);
		return directory;
	}

	AdminCommandDispatcher MakeDispatcher(AdminCommandHooks& a_hooks, ProtocolHost* a_host, std::function<bool(std::string_view)> a_disconnected = [](std::string_view) { return true; })
	{
		CommonwealthOnline::Server::InstallFirstJoinAbandonHook(
			a_hooks,
			[](std::string_view) { return true; },
			std::move(a_disconnected),
			[a_host](std::string_view a_account, std::uint32_t a_slot, bool a_withoutSave) {
				return a_host->AbandonSlotForAdmin(a_account, a_slot, 6.0, a_withoutSave).has_value();
			});
		return AdminCommandDispatcher(a_hooks);
	}

	Json Command(std::string_view a_confirm)
	{
		return Json{ { "cmd", "first-join-abandon" }, { "profileId", std::string{ kAccount } + "#1" }, { "confirm", std::string{ a_confirm } } };
	}

	void WriteRecoveryGrant(const std::filesystem::path& a_directory, std::string_view a_characterId, std::string_view a_baselineSave)
	{
		const auto key = std::string{ kAccount } + "#1";
		const auto path = a_directory / "durable-state.json";
		Json root;
		{
			std::ifstream input(path);
			root = Json::parse(input);
		}
		root["profiles"][key]["startingGrant"] = Json{ { "granted", false }, { "items", root["profiles"][key]["inventory"]["items"] },
			{ "recovery", Json{ { "state", "intent" }, { "characterId", std::string{ a_characterId } }, { "baselineSave", std::string{ a_baselineSave } }, { "baselineSha256", std::string(64, 'a') } } } };
		std::ofstream output(path, std::ios::trunc);
		output << root.dump(2);
	}

	void TestWithoutSaveDiscardsStartedCharacter()
	{
		const auto directory = SeedStartedCharacter("override-discard");
		auto fixture = OpenFixture(directory);
		const auto key = std::string{ kAccount } + "#1";
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("discard-unbegun-first-join")).value("ok", true) == false);
		assert(fixture.store->GetInventory(key).has_value());
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", false) == true);
		assert(!fixture.store->GetInventory(key).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestWithoutSaveRefusesSlotWithRecordedSave()
	{
		const auto directory = SeedStartedCharacter("override-saved");
		const auto key = std::string{ kAccount } + "#1";
		WriteRecoveryGrant(directory, "AAAA1111", kSave);
		auto fixture = OpenFixture(directory);
		assert(fixture.store->GetStartingGrant(key).has_value());
		assert(fixture.store->RecordSaveAttestation(kAccount, F4T::SaveProfileLogic::AttestedSave{ kSave, std::string(64, 'a'), 0 }));
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", true) == false);
		assert(fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestWithoutSaveDiscardsGrantWithoutMatchingSave()
	{
		const auto directory = SeedStartedCharacter("override-unmatched");
		const auto key = std::string{ kAccount } + "#1";
		WriteRecoveryGrant(directory, "BBBB2222", kUnmatchedSave);
		auto fixture = OpenFixture(directory);
		assert(fixture.store->GetStartingGrant(key).has_value());
		assert(fixture.store->RecordSaveAttestation(kAccount, F4T::SaveProfileLogic::AttestedSave{ kSave, std::string(64, 'a'), 0 }));
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", false) == true);
		assert(!fixture.store->GetInventory(key).has_value());
		assert(!fixture.store->GetPlayerState(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestWithoutSaveRefusesActiveSelection()
	{
		const auto directory = SeedStartedCharacter("override-active");
		const auto key = std::string{ kAccount } + "#1";
		auto fixture = OpenFixture(directory);
		(void)RequirePacket(Reconnect(*fixture.host, 12, 7.0, 107.0), 12, "sessionReady");
		ExpectOk(Send(*fixture.host, 12, Json{ { "type", "characterSelect" }, { "slot", 1 } }, 8.0, 108.0), 12, "select");
		fixture.host->HandleDisconnected(12, 108.5);
		const auto ready = RequirePacket(Reconnect(*fixture.host, 13, 9.0, 109.0), 13, "sessionReady");
		assert(ready.at("persistenceProfile") == key);
		assert(!fixture.host->AbandonSlotForAdmin(kAccount, 1, 6.0, true).has_value());
		assert(fixture.store->GetPlayerState(key).has_value());
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", true) == false);
		assert(fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestWithoutSaveRefusesConnectedProfile()
	{
		const auto directory = SeedStartedCharacter("override-connected");
		const auto key = std::string{ kAccount } + "#1";
		auto fixture = OpenFixture(directory);
		(void)RequirePacket(Reconnect(*fixture.host, 12, 7.0, 107.0), 12, "sessionReady");
		bool online = true;
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get(), [&online](std::string_view) { return !online; });
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", true) == false);
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetPlayerState(key).has_value());
		fixture.host->HandleDisconnected(12, 108.0);
		online = false;
		assert(dispatcher.Dispatch(Command("discard-first-join-without-save")).value("ok", false) == true);
		assert(!fixture.store->GetInventory(key).has_value());
		std::filesystem::remove_all(directory);
	}

	void TestUnknownConfirmIsRefused()
	{
		const auto directory = SeedStartedCharacter("override-confirm");
		auto fixture = OpenFixture(directory);
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("yes")).value("ok", true) == false);
		assert(dispatcher.Dispatch(Json{ { "cmd", "first-join-abandon" }, { "profileId", std::string{ kAccount } + "#1" } }).value("ok", true) == false);
		assert(fixture.store->GetInventory(std::string{ kAccount } + "#1").has_value());
		std::filesystem::remove_all(directory);
	}

	void TestOldConfirmRefusesStartedSlot()
	{
		const auto directory = SeedStartedCharacter("override-old-confirm");
		const auto key = std::string{ kAccount } + "#1";
		auto fixture = OpenFixture(directory);
		AdminCommandHooks hooks;
		const auto dispatcher = MakeDispatcher(hooks, fixture.host.get());
		assert(dispatcher.Dispatch(Command("discard-unbegun-first-join")).value("ok", true) == false);
		assert(fixture.store->GetInventory(key).has_value());
		assert(fixture.store->GetPlayerState(key).has_value());
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestWithoutSaveDiscardsStartedCharacter();
	TestWithoutSaveRefusesSlotWithRecordedSave();
	TestWithoutSaveDiscardsGrantWithoutMatchingSave();
	TestWithoutSaveRefusesActiveSelection();
	TestWithoutSaveRefusesConnectedProfile();
	TestUnknownConfirmIsRefused();
	TestOldConfirmRefusesStartedSlot();
	return 0;
}
