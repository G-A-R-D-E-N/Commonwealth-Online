#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	void TestLoadQuarantinesBadFactionsValue()
	{
		const auto run = [](const Json& a_stored, const Json& a_bad, const std::string& a_key) {
			const auto directory = TempDirectory("resilience-factions");
			const auto path = directory / "durable-state.json";
			const auto clock = [] { return 1000.0; };
			const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
			{
				DurableStateFileStore store(path, clock, instanceId);
				store.SaveFactions(a_stored);
				store.WaitForCommits();
			}
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Resilience" };
			assert(host.EnableFactions(&gate));
			assert(host.AdminListFactions().at("enabled") == true);
			assert(host.AdminListFactions().at("factions").empty());
			Join(host, 10, "p10", 1.0, 100.0);
			AssertResult(Send(host, 10, Create("Recovered Team"), 2.0, 101.0), 10, "create", true);
			store.WaitForCommits();
			assert(store.GetFactions().at("quarantinedFactions").at(a_key) == a_bad);
			DurableStateFileStore reloaded(path, clock, instanceId);
			PersistenceGate reloadedGate(reloaded);
			ProtocolHost restarted{ "Factions Resilience Restart" };
			assert(restarted.EnableFactions(&reloadedGate));
			assert(restarted.AdminListFactions().at("factions").size() == 1);
			assert(reloaded.GetFactions().at("quarantinedFactions").at(a_key) == a_bad);
			std::filesystem::remove_all(directory);
		};
		run(Json{ { "factions", nullptr } }, nullptr, "factions");
		run(Json{ { "factions", Json::array() } }, Json::array(), "factions");
		run(Json::array({ "not", "an", "object" }), Json::array({ "not", "an", "object" }), "factions-root");
	}

	void TestLongProfileIdSurvivesReload()
	{
		const auto directory = TempDirectory("resilience-long-id");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const std::string longId(128, 'a');
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Long Id" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, longId, 1.0, 100.0);
			AssertResult(Send(host, 10, Create("Long Team"), 2.0, 101.0), 10, "create", true);
			store.WaitForCommits();
		}
		DurableStateFileStore reloaded(path, clock, instanceId);
		PersistenceGate reloadedGate(reloaded);
		ProtocolHost restarted{ "Factions Long Id Restart" };
		assert(restarted.EnableFactions(&reloadedGate));
		assert(restarted.AdminListFactions().at("factions").size() == 1);
		assert(restarted.AdminListFactions().at("factions").at(0).at("leaderProfileId") == longId);
		assert(!reloaded.GetFactions().contains("quarantinedFactions"));
		std::filesystem::remove_all(directory);
	}

	void TestQuarantinedIdIsNotReused()
	{
		const auto directory = TempDirectory("resilience-ids");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{ { "nextFactionNumber", 1 }, { "factions", Json{ { "f-7", Json{ { "name", "Broken" } } } } } });
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Resilience Ids" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		AssertResult(Send(host, 10, Create("Fresh Team"), 2.0, 101.0), 10, "create", true);
		assert(host.AdminListFactions().at("factions").at(0).at("factionId") == "f-8");
		std::filesystem::remove_all(directory);
	}

	void TestWrappedCounterSkipsQuarantinedId()
	{
		const auto directory = TempDirectory("resilience-wrap");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{ { "nextFactionNumber", 999999999 }, { "factions", Json{ { "f-1", Json{ { "name", "Broken" } } } } } });
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Resilience Wrap" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 11, "p11", 1.0, 100.0);
		AssertResult(Send(host, 10, Create("First Team"), 2.0, 101.0), 10, "create", true);
		AssertResult(Send(host, 11, Create("Second Team"), 3.0, 102.0), 11, "create", true);
		const auto listed = host.AdminListFactions();
		for (const auto& faction : listed.at("factions")) assert(faction.at("factionId") != "f-1");
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestLoadQuarantinesBadFactionsValue();
	TestLongProfileIdSurvivesReload();
	TestQuarantinedIdIsNotReused();
	TestWrappedCounterSkipsQuarantinedId();
	return 0;
}
