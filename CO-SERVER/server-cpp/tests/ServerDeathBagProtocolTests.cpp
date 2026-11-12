#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "ServerFactionTestSupport.h"
#include "DurableStateFileStore.h"
#include "F4TLoadOrder.h"
#include "PersistenceGate.h"
#include "StartingGrantFixture.h"
#include "F4TDeathBagWire.h"
#include "F4TInventorySnapshot.h"

#include <cassert>
#include <chrono>
#include <filesystem>

using namespace FactionTestSupport;
using CommonwealthOnline::Server::DurableStateFileStore;
using CommonwealthOnline::Server::PersistenceGate;
using CommonwealthOnline::Server::ProtocolHost;

void CheckUnplacedReports(const std::filesystem::path& a_directory, bool a_placed)
{
	DurableStateFileStore store(a_directory / "state.json", [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
	F4T::Identity::TokenHash hash{};
	store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
	PersistenceGate gate(store);
	ProtocolHost host;
	CHECK(host.EnableInventoryTransactions(gate));
	CHECK(host.EnableCharacterSlots(&gate, 3));
	CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
	(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
		Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } }, Json{ { "plugin", "test.esm" }, { "localFormId", "00000003" }, { "count", 1u } } }));
	const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
	const auto welcome = host.HandleConnected(10, 1000.0);
	const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
	host.BindSessionProfile(10, "profile-a");
	const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	const auto joined = Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
		{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0);
	CHECK(HasPacket(joined, 10, "sessionReady"));
	CHECK(gate.VerifyStartingGrantSave(10, grantSave));
	const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
	F4T::LoadOrder::PluginList list;
	list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
	const auto encoded = F4T::LoadOrder::EncodePluginList(list);
	CHECK(encoded);
	(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
	(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
		{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
	CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
	const auto deathId = store.GetActiveDeathIntentId(character);
	CHECK(deathId);
	const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
	const Json retained = Json::array({ Json{ { "itemKey", "test.esm:00000003" }, { "count", 1u } } });
	const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", retained } }, now + 20.0, 1000.0);
	CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
	const auto owner = store.GetDeathBag(*deathId)->at("ownerProfileId").get<std::string>();
	if (a_placed) {
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		CHECK(store.ReportDeathBagUnplaced(*deathId, owner, character, "unplace-1") == F4T::DeathBag::UnplacedOutcome::Refused);
		CHECK(store.GetDeathBag(*deathId)->at("placed") == true);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		return;
	}
	CHECK(store.ReportDeathBagUnplaced(*deathId, "profile-b#0", character, "unplace-1") == F4T::DeathBag::UnplacedOutcome::Refused);
	CHECK(store.GetDeathBag(*deathId).has_value());
	const auto refunded = Send(host, 10, Json{ { "type", "deathBagUnplaced" }, { "deathId", *deathId }, { "operationId", "unplace-1" } }, now + 30.0, 1000.0);
	CHECK(RequirePacket(refunded, 10, "deathBagUnplacedResult").at("status") == "refunded");
	CHECK(!store.GetDeathBag(*deathId));
	CHECK(store.GetInventory("profile-a#0")->at("items").at("test.esm:00000002").at("count") == 2u);
	const auto absent = Send(host, 10, Json{ { "type", "deathBagUnplaced" }, { "deathId", *deathId }, { "operationId", "unplace-1" } }, now + 31.0, 1000.0);
	CHECK(RequirePacket(absent, 10, "deathBagUnplacedResult").at("status") == "absent");
	CHECK(store.GetInventory("profile-a#0")->at("items").at("test.esm:00000002").at("count") == 2u);
}

int main()
{
	const auto directory = TempDirectory("death-bag-protocol");
	const auto path = directory / "state.json";
	{
		DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } }, Json{ { "plugin", "test.esm" }, { "localFormId", "00000003" }, { "count", 1u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const auto joined = Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0);
		CHECK(HasPacket(joined, 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const Json retained = Json::array({ Json{ { "itemKey", "test.esm:00000003" }, { "count", 1u } } });
		const auto refused = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "disabled" }, { "stacks", stacks }, { "retained", retained } }, now + 10.0, 1000.0);
		CHECK(refused.empty() && !store.GetDeathBag(*deathId));
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", retained } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(store.GetDeathBag(*deathId));
		CHECK(!store.GetDeathBag(*deathId)->at("items").contains("test.esm:00000003"));
		CHECK(store.GetInventory("profile-a#0")->at("items").at("test.esm:00000003").at("count") == 1u);
		CHECK(store.GetInventory("profile-a#0")->at("items").at("fallout4.esm:00021B3B").at("count") == 1u);
		const auto repeated = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", retained } }, now + 25.0, 1000.0);
		CHECK(RequirePacket(repeated, 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(store.GetDeathBag(*deathId)->at("items").at("test.esm:00000002").at("count") == 2u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		const auto unplaced = Send(host, 10, Json{ { "type", "deathBagDiscover" } }, now + 30.0, 1000.0);
		CHECK(RequirePacket(unplaced, 10, "deathBagDiscovery").at("bags").empty());
		const auto placedAck = Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 40.0, 1000.0);
		CHECK(RequirePacket(placedAck, 10, "deathBagPlacedResult").at("status") == "placed");
		const auto repeatAck = Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 45.0, 1000.0);
		CHECK(RequirePacket(repeatAck, 10, "deathBagPlacedResult").at("status") == "placed");
		auto limited = Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 45.0, 1000.0);
		for (int attempt = 0; attempt < 8 && !limited.empty(); ++attempt) {
			limited = Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 45.0, 1000.0);
		}
		CHECK(limited.empty());
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagDiscover" } }, now + 45.0, 1000.0), 10, "deathBagDiscovery").at("bags").size() == 1);
		const auto placed = Send(host, 10, Json{ { "type", "deathBagDiscover" } }, now + 50.0, 1000.0);
		const auto bags = RequirePacket(placed, 10, "deathBagDiscovery").at("bags");
		CHECK(bags.size() == 1 && bags.at(0).at("deathId") == *deathId && bags.at(0).at("canTake") == true && bags.at(0).at("stacks") == stacks);
		const auto revision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		const auto withdrawn = Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", revision }, { "operationId", "op" }, { "receiptProof", true } }, now + 60.0, 1000.0);
		const auto grant = RequirePacket(withdrawn, 10, "deathBagHealthGrant");
		CHECK(grant.at("count") == 2u && grant.at("metadata").is_null() && grant.at("operationId") == "op" && grant.at("fresh") == true);
		const auto replay = Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", revision }, { "operationId", "op" }, { "receiptProof", true } }, now + 65.0, 1000.0);
		CHECK(RequirePacket(replay, 10, "deathBagHealthGrant").at("fresh") == false);
		const auto recovered = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 67.0, 1000.0),
			10, "deathBagGrantReconciliation").at("grants");
		CHECK(recovered.size() == 1 && recovered.at(0).at("fresh") == true && !recovered.at(0).contains("freshProof"));
		const auto pending = store.GetDeathBagRecipientGrants("profile-a#0", character);
		CHECK(pending && pending->size() == 1 && pending->at(0).at("state") == "pending");
		const auto receipt = Send(host, 10, Json{ { "type", "deathBagGrantAttempted" }, { "deathId", *deathId }, { "operationId", "op" } }, now + 70.0, 1000.0);
		CHECK(RequirePacket(receipt, 10, "deathBagGrantReceipt").at("status") == "attempted");
		const auto acknowledged = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 75.0, 1000.0),
			10, "deathBagGrantReconciliation").at("grants");
		CHECK(acknowledged.size() == 1 && acknowledged.at(0).at("state") == "attempted" && acknowledged.at(0).at("fresh") == true &&
			!acknowledged.at(0).contains("freshProof"));
		const auto delivered = store.GetDeathBagRecipientGrants("profile-a#0", character);
		CHECK(delivered && delivered->size() == 1 && delivered->at(0).at("state") == "attempted");
		CHECK(store.GetInventory("profile-a#0")->at("items").at("test.esm:00000002").at("count") == 2u);
		(void)host.PollDownedState(now + 80.0);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(store.RetireUntransferredDeathIntent(character, *deathId));
		(void)host.PollDownedState(now + 300.0);
		CHECK(!store.GetDeathBag(*deathId));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "void.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		storeClock = 1000.0;
		(void)host.HandleDisconnected(10, 1000.0);
		storeClock = 1119.0;
		(void)host.PollDownedState(now + 90.0);
		CHECK(store.GetDeathBag(*deathId));
		storeClock = 1120.0;
		(void)host.PollDownedState(now + 100.0);
		CHECK(store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		storeClock = 87400.0;
		(void)host.PollDownedState(now + 200.0);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(store.ListArchivedDeathBags(0).at("total") == 1u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "quit.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		storeClock = 1000.0;
		(void)host.HandleDisconnected(10, 1000.0, true);
		storeClock = 1119.0;
		(void)host.PollDownedState(now + 200.0);
		CHECK(store.GetDeathBag(*deathId));
		storeClock = 1120.0;
		(void)host.PollDownedState(now + 300.0);
		CHECK(store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		storeClock = 87400.0;
		(void)host.PollDownedState(now + 400.0);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(store.ListArchivedDeathBags(0).at("total") == 1u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "reconnect.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		storeClock = 1000.0;
		(void)host.HandleDisconnected(10, 1000.0);
		CHECK(store.GetDeathBag(*deathId)->at("departure") == "crash");
		(void)host.HandleConnected(11, 1000.0);
		host.BindSessionProfile(11, "profile-a");
		CHECK(HasPacket(Send(host, 11, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now + 30.0, 1000.0), 11, "sessionReady"));
		CHECK(store.GetDeathBag(*deathId)->at("departure") == "none");
		storeClock = 1119.0;
		(void)host.PollDownedState(now + 100.0);
		CHECK(store.GetDeathBag(*deathId));
		storeClock = 1120.0;
		(void)host.PollDownedState(now + 110.0);
		CHECK(store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		storeClock = 87400.0;
		(void)host.PollDownedState(now + 200.0);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(store.ListArchivedDeathBags(0).at("total") == 1u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "supersede.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		(void)host.HandleConnected(11, 1000.0);
		host.BindSessionProfile(11, "profile-a");
		const auto takeover = Send(host, 11, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now + 30.0, 1000.0);
		CHECK(HasPacket(takeover, 10, "sessionEnded"));
		CHECK(HasPacket(takeover, 11, "sessionReady"));
		CHECK(store.GetDeathBag(*deathId)->at("departure") == "none");
		storeClock = 1119.0;
		(void)host.PollDownedState(now + 100.0);
		CHECK(store.GetDeathBag(*deathId));
		storeClock = 1120.0;
		(void)host.PollDownedState(now + 110.0);
		CHECK(store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		storeClock = 87400.0;
		(void)host.PollDownedState(now + 200.0);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(store.ListArchivedDeathBags(0).at("total") == 1u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	std::string restartDeathId;
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "restart.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		restartDeathId = *deathId;
		CHECK(store.GetDeathBag(restartDeathId)->at("departure") == "none");
	}
	{
		double storeClock = 1500.0;
		DurableStateFileStore store(directory / "restart.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		CHECK(store.GetDeathBag(restartDeathId));
		CHECK(store.GetDeathBag(restartDeathId)->at("departure") == "crash");
		CHECK(store.GetDeathBag(restartDeathId)->at("departedAt") == 1500.0);
		storeClock = 1619.0;
		CHECK(store.ExpirePendingDeathBags().empty());
		storeClock = 1620.0;
		CHECK(store.ExpirePendingDeathBags().empty());
		CHECK(store.GetDeathBag(restartDeathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		storeClock = 87400.0;
		CHECK(store.ExpirePendingDeathBags().empty());
		CHECK(!store.GetDeathBag(restartDeathId));
		CHECK(store.ListArchivedDeathBags(0).at("total") == 1u);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "place-failed.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(store.GetDeathBag(*deathId)->at("placed") == false && store.GetDeathBag(*deathId)->at("departure") == "none");
		const auto restoredWithStacks = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", true }, { "remaining", stacks } }, now + 26.0, 1000.0);
		CHECK(!HasPacket(restoredWithStacks, 10, "deathBagPlaceFailedAck"));
		const Json overCount = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 3u }, { "metadata", nullptr } } });
		const auto overClaim = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", overCount } }, now + 27.0, 1000.0);
		CHECK(!HasPacket(overClaim, 10, "deathBagPlaceFailedAck"));
		const auto missingRemaining = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", true } }, now + 28.0, 1000.0);
		CHECK(!HasPacket(missingRemaining, 10, "deathBagPlaceFailedAck"));
		CHECK(store.GetDeathBag(*deathId)->at("items").at("test.esm:00000002").at("count") == 2u);
		const auto failed = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", true }, { "remaining", Json::array() } }, now + 30.0, 1000.0);
		const auto voided = RequirePacket(failed, 10, "deathBagVoided");
		CHECK(voided.at("deathId") == *deathId && voided.at("stacks") == stacks);
		CHECK(RequirePacket(failed, 10, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		const auto repeatFailed = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", true }, { "remaining", Json::array() } }, now + 31.0, 1000.0);
		CHECK(!HasPacket(repeatFailed, 10, "deathBagVoided"));
		CHECK(RequirePacket(repeatFailed, 10, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		(void)host.PollDownedState(now + 40.0);
		CHECK(!store.GetDeathBag(*deathId));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "place-held.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		const auto deposit = Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0);
		CHECK(RequirePacket(deposit, 10, "deathBagDepositResult").at("status") == "committed");
		const auto held = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", stacks } }, now + 25.0, 1000.0);
		CHECK(!HasPacket(held, 10, "deathBagVoided"));
		CHECK(RequirePacket(held, 10, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		const auto heldAgain = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", stacks } }, now + 26.0, 1000.0);
		CHECK(RequirePacket(heldAgain, 10, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		const Json overClaim = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 3u }, { "metadata", nullptr } } });
		const auto heldOver = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", overClaim } }, now + 27.0, 1000.0);
		CHECK(!HasPacket(heldOver, 10, "deathBagPlaceFailedAck"));
		CHECK(store.RecoverArchivedDeathBag(*deathId));
		const auto grants = store.GetDeathBagRecipientGrants("profile-a#0", character, std::string{});
		CHECK(grants && grants->size() == 1 && grants->at(0).at("count") == 2u);
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "foreign.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		store.SaveResumeCredential("profile-b", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		CHECK(store.SetCharacterSlotRecord("profile-b", 0, Json{ { "name", "Looter" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		(void)store.InitializeInventory("profile-b#0", Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "00000003" }, { "count", 1u } } }));
		const auto ownerSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto looterSave = ReconcileStartingGrantFixture(store, "profile-b#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, ownerSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0), 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		(void)host.HandleConnected(11, 1000.0);
		host.BindSessionProfile(11, "profile-b");
		CHECK(HasPacket(Send(host, 11, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now + 40.0, 1000.0), 11, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(11, looterSave));
		const auto looterRevision = store.GetInventory("profile-b#0")->at("revision").get<std::uint32_t>();
		const Json looterWithdraw{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", looterRevision }, { "operationId", "loot" } };
		CHECK(RequirePacket(Send(host, 11, Json{ { "type", "deathBagDiscover" } }, now + 50.0, 1000.0), 11, "deathBagDiscovery").at("bags").empty());
		CHECK(!HasPacket(Send(host, 11, looterWithdraw, now + 60.0, 1000.0), 11, "deathBagHealthGrant"));
		CHECK(store.GetDeathBag(*deathId)->at("items").at("test.esm:00000002").at("count") == 2u);
		host.SetDeathBagLootByOthers(true);
		const auto open = RequirePacket(Send(host, 11, Json{ { "type", "deathBagDiscover" } }, now + 70.0, 1000.0), 11, "deathBagDiscovery").at("bags");
		CHECK(open.size() == 1 && open.at(0).at("stacks") == stacks);
		const auto looted = RequirePacket(Send(host, 11, looterWithdraw, now + 80.0, 1000.0), 11, "deathBagHealthGrant");
		CHECK(looted.at("count") == 2u && looted.at("operationId") == "loot" && !looted.contains("fresh"));
		CHECK(store.GetInventory("profile-b#0")->at("items").at("test.esm:00000002").at("count") == 2u);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(RequirePacket(Send(host, 11, looterWithdraw, now + 90.0, 1000.0), 11, "deathBagHealthGrant").at("count") == 2u);
		CHECK(store.GetInventory("profile-b#0")->at("items").at("test.esm:00000002").at("count") == 2u);
		const auto ownerRevision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		CHECK(!HasPacket(Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 1u }, { "expectedRevision", ownerRevision }, { "operationId", "owner" } }, now + 100.0, 1000.0), 10, "deathBagHealthGrant"));
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "place-permanent.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		store.SaveResumeCredential("profile-b", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		CHECK(store.SetCharacterSlotRecord("profile-b", 0, Json{ { "name", "Looter" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		(void)store.InitializeInventory("profile-b#0", Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "00000003" }, { "count", 1u } } }));
		const auto ownerSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto looterSave = ReconcileStartingGrantFixture(store, "profile-b#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, ownerSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0), 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		(void)host.HandleConnected(11, 1000.0);
		host.BindSessionProfile(11, "profile-b");
		CHECK(HasPacket(Send(host, 11, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now + 40.0, 1000.0), 11, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(11, looterSave));
		const auto foreign = Send(host, 11, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", stacks } }, now + 50.0, 1000.0);
		CHECK(!HasPacket(foreign, 11, "deathBagVoided"));
		CHECK(RequirePacket(foreign, 11, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		const auto placedRestored = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", true }, { "remaining", Json::array() } }, now + 60.0, 1000.0);
		CHECK(!HasPacket(placedRestored, 10, "deathBagVoided"));
		CHECK(RequirePacket(placedRestored, 10, "deathBagPlaceFailedAck").at("deathId") == *deathId);
		const Json overCount = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 3u }, { "metadata", nullptr } } });
		const auto placedOver = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", *deathId }, { "restored", false }, { "remaining", overCount } }, now + 70.0, 1000.0);
		CHECK(!HasPacket(placedOver, 10, "deathBagPlaceFailedAck"));
		CHECK(store.GetDeathBag(*deathId)->at("placed") == true && store.GetDeathBag(*deathId)->at("items").at("test.esm:00000002").at("count") == 2u);
		const auto neverDeposited = Send(host, 10, Json{ { "type", "deathBagPlaceFailed" }, { "deathId", "fedcba9876543210fedcba9876543210" }, { "restored", false }, { "remaining", stacks } }, now + 80.0, 1000.0);
		CHECK(RequirePacket(neverDeposited, 10, "deathBagPlaceFailedAck").at("deathId") == "fedcba9876543210fedcba9876543210");
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "expiry-withdraw.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0), 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		const auto revision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		storeClock = 2800.0;
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", revision }, { "operationId", "late" } }, now + 40.0, 1000.0), 10, "deathBagHealthGrant").at("count") == 2u);
		CHECK(!store.GetDeathBag(*deathId));
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 50.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		CHECK(!store.CreateEmptyDeathBag(*deathId));
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "replay-grant.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		const Json hello{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } };
		CHECK(HasPacket(Send(host, 10, hello, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0), 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		const auto revision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		const auto originalGrant = RequirePacket(Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", revision }, { "operationId", "replay" }, { "receiptProof", true } }, now + 40.0, 1000.0), 10, "deathBagHealthGrant");
		CHECK(originalGrant.at("count") == 2u && originalGrant.at("fresh") == true);
		(void)host.HandleDisconnected(10, 1000.0);
		(void)host.HandleConnected(11, 1000.0);
		host.BindSessionProfile(11, "profile-a");
		const auto resumed = Send(host, 11, hello, now + 50.0, 1000.0);
		CHECK(HasPacket(resumed, 11, "sessionReady"));
		const auto offered = RequirePacket(resumed, 11, "deathBagGrantReconciliation").at("grants");
		CHECK(offered.size() == 1 && offered.at(0).at("operationId") == "replay" && offered.at(0).at("state") == "pending" &&
			offered.at(0).size() == 7 && !offered.at(0).contains("fresh") && !offered.at(0).contains("freshProof"));
		const auto legacyPage = RequirePacket(Send(host, 11, Json{ { "type", "deathBagGrantReconcile" } }, now + 52.0, 1000.0),
			11, "deathBagGrantReconciliation").at("grants");
		CHECK(legacyPage.size() == 1 && legacyPage.at(0).size() == 7 && !legacyPage.at(0).contains("fresh"));
		const auto resumedPage = RequirePacket(Send(host, 11, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 55.0, 1000.0),
			11, "deathBagGrantReconciliation").at("grants");
		CHECK(resumedPage.size() == 1 && resumedPage.at(0).at("fresh") == true && !resumedPage.at(0).contains("freshProof"));
		const auto decoded = F4T::DeathBagWire::DecodeGrantRecord(resumedPage.at(0));
		CHECK(decoded && decoded->deathId == *deathId && decoded->operationId == "replay" && decoded->count == 2u && decoded->metadata.is_null() && decoded->fresh == true);
		CHECK(RequirePacket(Send(host, 11, Json{ { "type", "deathBagGrantAttempted" }, { "deathId", *deathId }, { "operationId", "replay" } }, now + 60.0, 1000.0), 11, "deathBagGrantReceipt").at("status") == "attempted");
		(void)host.HandleDisconnected(11, 1000.0);
		(void)host.HandleConnected(12, 1000.0);
		host.BindSessionProfile(12, "profile-a");
		const auto cleared = Send(host, 12, hello, now + 70.0, 1000.0);
		CHECK(HasPacket(cleared, 12, "sessionReady"));
		const auto reoffered = RequirePacket(cleared, 12, "deathBagGrantReconciliation").at("grants");
		CHECK(reoffered.size() == 1 && reoffered.at(0).at("operationId") == "replay" && reoffered.at(0).at("state") == "attempted" &&
			reoffered.at(0).size() == 7 && !reoffered.at(0).contains("fresh") && !reoffered.at(0).contains("freshProof"));
		const auto attemptedPage = RequirePacket(Send(host, 12, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 75.0, 1000.0),
			12, "deathBagGrantReconciliation").at("grants");
		CHECK(attemptedPage.size() == 1 && attemptedPage.at(0).at("fresh") == true && !attemptedPage.at(0).contains("freshProof"));
		CHECK(RequirePacket(Send(host, 12, Json{ { "type", "deathBagGrantAttempted" }, { "deathId", *deathId }, { "operationId", "replay" } }, now + 80.0, 1000.0), 12, "deathBagGrantReceipt").at("status") == "attempted");
		const auto stillOffered = store.GetDeathBagRecipientGrants("profile-a#0", "quest-11111111111111111111111111111111");
		CHECK(stillOffered && stillOffered->size() == 1 && stillOffered->at(0).at("state") == "attempted" && stillOffered->at(0).at("freshProof") == true);
	}
	{
		double storeClock = 1000.0;
		DurableStateFileStore store(directory / "grant-reconcile.json", [&storeClock] { return storeClock; }, [] { return "0123456789abcdef0123456789abcdef"; });
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
		PersistenceGate gate(store);
		ProtocolHost host;
		CHECK(host.EnableInventoryTransactions(gate));
		CHECK(host.EnableCharacterSlots(&gate, 3));
		CHECK(store.SetCharacterSlotRecord("profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-11111111111111111111111111111111" } }));
		(void)store.InitializeInventory("profile-a#0", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } }));
		const auto grantSave = ReconcileStartingGrantFixture(store, "profile-a#0");
		const auto welcome = host.HandleConnected(10, 1000.0);
		const auto player = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		host.BindSessionProfile(10, "profile-a");
		const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		CHECK(HasPacket(Send(host, 10, Json{ { "type", "hello" }, { "protocolVersion", 2u },
			{ "capabilities", Json::array({ "inventory-stable-v1", "component-snapshot-v1", "component-snapshot-channels-v1" }) } }, now, 1000.0), 10, "sessionReady"));
		CHECK(gate.VerifyStartingGrantSave(10, grantSave));
		const auto character = store.GetCharacterSlotRecords("profile-a").at(0).at("questProfile").get<std::string>();
		F4T::LoadOrder::PluginList list;
		list.full.push_back({ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		CHECK(encoded);
		(void)Send(host, 10, Json{ { "type", "pluginList" }, { "data", *encoded } }, now, 1000.0);
		(void)Send(host, 10, Json{ { "type", "transform" }, { "cellId", "00000002" }, { "worldspaceId", "00000001" },
			{ "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", "spawn" } }, now, 1000.0);
		CHECK(HasPacket(host.AdminKillPlayer(player), 10, "died"));
		const auto deathId = store.GetActiveDeathIntentId(character);
		CHECK(deathId);
		const Json stacks = Json::array({ Json{ { "itemKey", "test.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagSnapshot" }, { "deathId", *deathId }, { "status", "countsVerified" }, { "stacks", stacks }, { "retained", Json::array() } }, now + 20.0, 1000.0), 10, "deathBagDepositResult").at("status") == "committed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagPlaced" }, { "deathId", *deathId } }, now + 30.0, 1000.0), 10, "deathBagPlacedResult").at("status") == "placed");
		const auto revision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagWithdraw" }, { "deathId", *deathId }, { "itemKey", "test.esm:00000002" },
			{ "count", 2u }, { "expectedRevision", revision }, { "operationId", "unacked" } }, now + 40.0, 1000.0), 10, "deathBagHealthGrant").at("count") == 2u);
		const auto owed = store.GetDeathBagRecipientGrants("profile-a#0", character);
		CHECK(owed && owed->size() == 1 && owed->at(0).at("operationId") == "unacked");
		const auto spendRevision = store.GetInventory("profile-a#0")->at("revision").get<std::uint32_t>();
		const auto spend = F4T::InventorySnapshot::NormalizeRequest("spend", spendRevision,
			Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } } }));
		CHECK(spend);
		CHECK(store.ReconcileInventory("profile-a#0", *spend).changed);
		CHECK(!store.GetInventory("profile-a#0")->at("items").contains("test.esm:00000002"));
		CHECK(store.GetDeathBagRecipientGrants("profile-a#0", character)->size() == 1);
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantAttempted" }, { "deathId", *deathId }, { "operationId", "unacked" } }, now + 50.0, 1000.0), 10, "deathBagGrantReceipt").at("status") == "attempted");
		const auto kept = store.GetDeathBagRecipientGrants("profile-a#0", character);
		CHECK(kept && kept->size() == 1 && kept->at(0).at("state") == "attempted");
		for (std::uint32_t i = 0; i < 64; ++i) {
			const auto operation = "page" + std::to_string(i);
			CHECK(store.QueueDeathBagRefundGrant("profile-a#0", character, *deathId, operation, "test.esm:00000002", 1, nullptr));
		}
		CHECK(!store.QueueDeathBagRefundGrant("profile-a#0", character, *deathId, "page64", "test.esm:00000002", 1, nullptr));
		CHECK(!store.MarkDeathBagGrantAttempted("profile-a#0", character, *deathId, "page0"));
		const auto claimable = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 55.0, 1000.0), 10, "deathBagGrantReconciliation");
		CHECK(claimable.at("grants").size() == 64 && claimable.at("grants").at(0).at("claimRequired") == true);
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantClaim" }, { "deathId", *deathId }, { "operationId", "page0" } }, now + 56.0, 1000.0),
			10, "deathBagGrantReceipt").at("status") == "claimed");
		CHECK(RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantClaim" }, { "deathId", *deathId }, { "operationId", "page0" } }, now + 57.0, 1000.0),
			10, "deathBagGrantReceipt").at("status") == "claimed");
		CHECK(store.MarkDeathBagGrantAttempted("profile-a#0", character, *deathId, "page0"));
		CHECK(store.QueueDeathBagRefundGrant("profile-a#0", character, *deathId, "page64", "test.esm:00000002", 1, nullptr));
		const auto afterAttemptLegacy = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" } }, now + 59.0, 1000.0),
			10, "deathBagGrantReconciliation");
		CHECK(afterAttemptLegacy.at("grants").size() == 1 && afterAttemptLegacy.at("grants").at(0).at("operationId") == "unacked");
		const auto afterAttemptCursor = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 59.5, 1000.0),
			10, "deathBagGrantReconciliation");
		CHECK(afterAttemptCursor.at("grants").at(0).at("operationId") == "page0" &&
			!afterAttemptCursor.at("grants").at(0).contains("claimManaged"));
		const auto legacy = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" } }, now + 58.0, 1000.0), 10, "deathBagGrantReconciliation");
		CHECK(legacy.at("grants").size() == 1 && legacy.at("grants").at(0).at("operationId") == "unacked");
		const auto firstPage = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", "" } }, now + 60.0, 1000.0), 10, "deathBagGrantReconciliation");
		CHECK(firstPage.at("grants").size() == 64 && firstPage.at("nextCursor").is_string());
		const auto secondPage = RequirePacket(Send(host, 10, Json{ { "type", "deathBagGrantReconcile" }, { "cursor", firstPage.at("nextCursor") } }, now + 70.0, 1000.0), 10, "deathBagGrantReconciliation");
		CHECK(secondPage.at("grants").size() == 2 && secondPage.at("nextCursor").is_null());
	}
	std::filesystem::remove_all(directory);
	const auto refundDirectory = TempDirectory("death-bag-unplaced-refund");
	CheckUnplacedReports(refundDirectory, false);
	std::filesystem::remove_all(refundDirectory);
	const auto placedDirectory = TempDirectory("death-bag-unplaced-placed");
	CheckUnplacedReports(placedDirectory, true);
	std::filesystem::remove_all(placedDirectory);
}
