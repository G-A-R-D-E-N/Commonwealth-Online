#include "ServerFactionTestSupport.h"

#include "ServerFactions.h"

#include <cassert>
#include <filesystem>
#include <string>

namespace
{
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::FactionService;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::SocialGraph;
	using FactionTestSupport::Json;
	using FactionTestSupport::TempDirectory;

	void TestEnableDisableLifecycleWithoutPersistence()
	{
		ProtocolStats stats;
		SocialGraph social{ stats };
		FactionService factions(stats, social);
		assert(!factions.Enabled());
		assert(!factions.SaveNow());
		assert(!factions.Disable());

		assert(factions.Enable(nullptr));
		assert(factions.Enabled());
		assert(!factions.Enable(nullptr));
		assert(factions.SaveNow());

		assert(factions.Disable());
		assert(!factions.Enabled());
		assert(!factions.Disable());
		assert(!factions.SaveNow());
	}

	void TestEnableLoadsStoredCounterAndSaveNowWritesItBack()
	{
		const auto directory = TempDirectory("store-counter");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{ { "nextFactionNumber", 7 }, { "factions", Json::object() } });
			PersistenceGate gate(store);
			ProtocolStats stats;
			SocialGraph social{ stats };
			FactionService factions(stats, social);
			assert(factions.Enable(&gate));
			assert(factions.SaveNow());

			const auto saved = gate.ReadStoredFactions();
			assert(saved.at("nextFactionNumber") == 7);
			assert(saved.at("factions").empty());
			assert(!saved.contains("quarantinedFactions"));
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}

	void TestQuarantinedEntriesSurviveSave()
	{
		const auto directory = TempDirectory("store-quarantine");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{ { "nextFactionNumber", 2 }, { "factions", "not-an-object" } });
			PersistenceGate gate(store);
			ProtocolStats stats;
			SocialGraph social{ stats };
			FactionService factions(stats, social);
			assert(factions.Enable(&gate));
			assert(factions.SaveNow());

			const auto saved = gate.ReadStoredFactions();
			assert(saved.at("factions").empty());
			assert(saved.at("quarantinedFactions").at("factions") == "not-an-object");
			assert(saved.at("nextFactionNumber") == 2);
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestEnableDisableLifecycleWithoutPersistence();
	TestEnableLoadsStoredCounterAndSaveNowWritesItBack();
	TestQuarantinedEntriesSurviveSave();
	return 0;
}
