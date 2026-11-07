#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "ServerFactionTestSupport.h"
#include "DurableStateFileStore.h"
#include "F4TLoadOrder.h"
#include "PersistenceGate.h"

#include <cassert>
#include <chrono>
#include <filesystem>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

void TestDeathIntent(bool a_pluginList, bool a_failSave, bool a_failRetire = false, int a_transition = 0, bool a_respawn = false, bool a_failFinalize = false)
{
	const auto directory = TempDirectory(a_failSave ? "death-intent-failure" : a_pluginList ? "death-intent" : "death-intent-unresolved");
	const auto path = directory / "state.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	{
		DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		const auto inventory = store.InitializeInventory("profile-a#0", Json::array());
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const auto joined = Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0);
		CHECK(HasPacket(joined, 10, "sessionReady"));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		if (a_pluginList) {
			F4T::LoadOrder::PluginList list;
			list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
			const auto encoded = F4T::LoadOrder::EncodePluginList(list);
			CHECK(encoded);
			(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		}
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		if (a_failSave) {
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
		}
		std::vector<CommonwealthOnline::Server::OutboundFrame> killed;
		if (a_transition == 0) {
			killed = host.AdminKillPlayer(player);
		} else {
			const bool downed = a_transition == 2;
			const auto state = Send(host, 10, Json{ { "type", "playerState" }, { "vitals", Json{
				{ "healthPercent", 0 }, { "isDowned", downed }, { "isDead", !downed } } } }, now, 1000.0);
			if (downed && a_pluginList && !a_failSave) {
				const auto pending = store.GetActiveDeathIntent(character);
				CHECK(pending && pending->at("state") == "downed" && pending->at("deadlineUnix") == 1060.0);
			}
			if (downed && a_failFinalize) {
				std::filesystem::remove(backup);
				std::filesystem::create_directories(backup / "occupied");
				const auto deferred = host.PollDownedState(now + 61.0);
				CHECK(!HasPacket(deferred, 10, "died"));
				CHECK(store.GetActiveDeathIntent(character)->at("state") == "downed");
				std::filesystem::remove_all(backup);
			}
			killed = downed ? host.PollDownedState(now + 62.0) : state;
		}
		CHECK(HasPacket(killed, 10, "died"));
		const auto intent = store.GetActiveDeathIntent(character);
		if (a_pluginList && !a_failSave) {
			CHECK(intent && intent->at("state") == "dead" && intent->at("locationSource") == "client-reported");
			CHECK(intent->at("cellKey") == "fallout4.esm:00000002");
			CHECK(intent->at("worldspaceKey") == "fallout4.esm:00000001");
			CHECK(intent->at("position") == Json::array({ 1.0, 2.0, 3.0 }));
			CHECK(host.AdminKillPlayer(player).empty());
			CHECK(store.GetActiveDeathIntent(character) == intent);
			const auto firstId = store.GetActiveDeathIntentId(character);
			CHECK(firstId);
			const auto disabledResult = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *firstId }, { "status", "disabled" } }, now + 0.05, 1000.0);
			CHECK(disabledResult.empty() && !store.GetDeathBag(*firstId));
			const auto forgedResult = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *firstId }, { "status", "created" } }, now + 0.06, 1000.0);
			CHECK(forgedResult.empty() && !store.GetDeathBag(*firstId));
			const auto untrustedVerified = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *firstId }, { "status", "countsVerified" } }, now + 0.07, 1000.0);
			CHECK(untrustedVerified.empty() && !store.GetDeathBag(*firstId));
			const auto unavailable = Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *firstId } }, now + 0.08, 1000.0);
			CHECK(unavailable.empty());
			CHECK(!store.GetDeathBag(*firstId));
			if (a_failRetire) {
				std::filesystem::remove(backup);
				std::filesystem::create_directories(backup / "occupied");
			}
			const auto reviveOrRespawn = [&](double a_time) {
				return a_respawn ? Send(host, 10, Json{ { "type", "respawnRequest" }, { "point", "sanctuary" } }, a_time, 1000.0) :
					host.AdminRevivePlayer(player, a_time);
			};
			const auto revived = reviveOrRespawn(now + 0.1);
			if (a_failRetire) {
				CHECK(revived.empty());
				CHECK(store.GetActiveDeathIntentId(character) == firstId);
				std::filesystem::remove_all(backup);
				const auto retry = reviveOrRespawn(now + 0.2);
				CHECK(HasPacket(retry, 10, a_respawn ? "respawnGrant" : "revived"));
				CHECK(!store.GetActiveDeathIntentId(character));
			} else {
				CHECK(HasPacket(revived, 10, a_respawn ? "respawnGrant" : "revived"));
				CHECK(!store.GetActiveDeathIntentId(character));
				DurableStateFileStore retiredRestart(path, [] { return 1000.0; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				CHECK(!retiredRestart.GetActiveDeathIntentId(character));
				CHECK(!retiredRestart.GetActiveDeathIntent(character));
				const auto next = store.AllocateDeathIntent(*intent);
				CHECK(next.outcome == F4T::DeathIntent::Outcome::Created && next.generation == 2 && next.deathId != *firstId);
				CHECK(!store.RetireUntransferredDeathIntent(character, *firstId));
				DurableStateFileStore reopened(path, [] { return 1000.0; }, [] { return "ffffffffffffffffffffffffffffffff"; });
				CHECK(reopened.GetActiveDeathIntentId(character) == next.deathId);
			}
		} else {
			CHECK(!intent);
		}
		CHECK(store.GetInventory("profile-a#0") == inventory);
		if (a_failSave) std::filesystem::remove_all(backup);
	}
	std::filesystem::remove_all(directory);
}

void TestRestartRecovery(bool a_expired, bool a_replaced = false, bool a_expireBeforeShutdown = false, bool a_failExpiry = false)
{
	const auto directory = TempDirectory(a_expired ? "death-restart-expired" : "death-restart-pending");
	const auto path = directory / "state.json";
	double wall = 1000.0;
	std::string character;
	{
		DurableStateFileStore store(path, [&] { return wall; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array());
		(void)host.HandleConnected(10, wall);
		host.BindSessionProfile(10, "profile-a");
		const auto mono = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const auto hello = Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } };
		CHECK(HasPacket(Send(host, 10, hello, mono, wall), 10, "sessionReady"));
		character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, mono, wall);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, mono, wall);
		(void)Send(host, 10, Json{ { "type", "playerState" }, { "vitals", Json{
			{ "healthPercent", 0 }, { "isDowned", true }, { "isDead", false } } } }, mono, wall);
		const auto pending = store.GetActiveDeathIntent(character);
		CHECK(pending && pending->at("state") == "downed" && pending->at("deadlineUnix") == 1060.0);
		wall = 1061.0;
		CHECK(store.ExpireDueDeathIntents({ character }) == 0);
		CHECK(store.GetActiveDeathIntent(character)->at("state") == "downed");
		wall = 1000.0;
		(void)host.HandleDisconnected(10, wall);
		if (a_expireBeforeShutdown) {
			wall = 1061.0;
			const auto backup = std::filesystem::path{ path.string() + ".bak" };
			if (a_failExpiry) {
				std::filesystem::remove(backup);
				std::filesystem::create_directories(backup / "occupied");
			}
			(void)host.PollDownedState(mono + 61.0);
			if (a_failExpiry) {
				CHECK(store.GetActiveDeathIntent(character)->at("state") == "downed");
				std::filesystem::remove_all(backup);
				(void)host.PollDownedState(mono + 62.0);
			}
			CHECK(store.GetActiveDeathIntent(character)->at("state") == "dead");
		}
	}
	wall = a_expired ? 1061.0 : 1030.0;
	if (a_replaced) {
		DurableStateFileStore store(path, [&] { return wall; }, [] { return "ffffffffffffffffffffffffffffffff"; });
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Replacement" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
	}
	{
		DurableStateFileStore store(path, [&] { return wall; }, [] { return "ffffffffffffffffffffffffffffffff"; });
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		const auto mono = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (a_expired && !a_replaced) {
			(void)host.PollDownedState(mono);
			CHECK(store.GetActiveDeathIntent(character)->at("state") == "dead");
		}
		(void)host.HandleConnected(11, wall);
		host.BindSessionProfile(11, "profile-a");
		const auto hello = Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } };
		const auto frames = Send(host, 11, hello, mono, wall);
		CHECK(HasPacket(frames, 11, "sessionReady"));
		CHECK(!HasPacket(frames, 11, "deathBagGrantReconciliation"));
		if (a_replaced) {
			CHECK(!HasPacket(frames, 11, "downedState") && !HasPacket(frames, 11, "playerDied"));
			CHECK(store.GetActiveDeathIntent(character));
		} else if (a_expired) {
			CHECK(HasPacket(frames, 11, "playerDied"));
		} else {
			const auto state = RequirePacket(frames, 11, "downedState");
			CHECK(state.at("downed") == true && state.at("remainingMs") == 30000u);
		}
		CHECK(store.GetInventory("profile-a#0")->at("items").empty());
	}
	std::filesystem::remove_all(directory);
}

int main()
{
	TestDeathIntent(true, false);
	TestDeathIntent(true, false, true);
	TestDeathIntent(false, false);
	TestDeathIntent(true, true);
	TestDeathIntent(true, false, false, 1);
	TestDeathIntent(true, false, false, 2);
	TestDeathIntent(true, false, true, 0, true);
	TestDeathIntent(true, false, false, 2, false, true);
	TestRestartRecovery(false);
	TestRestartRecovery(true);
	TestRestartRecovery(false, true);
	TestRestartRecovery(true, false, true);
	TestRestartRecovery(true, false, true, true);
}
