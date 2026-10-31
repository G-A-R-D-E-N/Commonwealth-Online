#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	Json ListRequest()
	{
		return Json{ { "type", "factionList" } };
	}

	DurableStateFileStore OpenSeedStore(const std::filesystem::path& a_path)
	{
		return DurableStateFileStore(a_path, [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
	}

	void SeedFactions(const std::filesystem::path& a_path, std::uint32_t a_count, const std::string& a_lastProfile)
	{
		Json factions = Json::object();
		for (std::uint32_t number = 1; number <= a_count; ++number) {
			const auto digits = std::to_string(1000 + number).substr(1);
			const auto profile = number == a_count && !a_lastProfile.empty() ? a_lastProfile : "m" + digits;
			factions["f-" + std::to_string(number)] = Stored("Team " + digits, "", profile, Json::array({ profile }), Json{ { profile, "Member" } });
		}
		auto store = OpenSeedStore(a_path);
		store.SaveFactions(Json{ { "nextFactionNumber", a_count + 1 }, { "factions", std::move(factions) } });
		store.WaitForCommits();
	}

	void TestDisabledProducesNoFrames()
	{
		ProtocolHost host{ "Faction List Disabled" };
		Join(host, 10, "p10", 1.0, 100.0);
		assert(Send(host, 10, ListRequest(), 2.0, 100.2).empty());
	}

	void TestListsContractFieldsWithoutLeakingMembers()
	{
		ProtocolHost host{ "Faction List Fields" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		const auto bravoMember = Join(host, 40, "p40", 1.3, 100.6);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Nomad" } }, 1.5, 100.8);
		Send(host, 30, Json{ { "type", "playerState" }, { "characterName", "Scout" } }, 1.6, 100.9);

		const auto created = Send(host, 10, Create("Alpha Team", "ALP"), 2.0, 101.0);
		AssertResult(created, 10, "create", true);
		const auto alphaId = RequirePacket(created, 10, "factionState").at("faction").at("factionId");
		AssertResult(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "Hold the line" }, { "color", "#ff00aa" }, { "motd", "Raid at dusk" } }, 2.1, 101.1), 10, "setProfile", true);
		FormFaction(host, 30, 40, bravoMember.playerId, "Bravo Team");

		const auto response = Send(host, 10, ListRequest(), 3.0, 102.0);
		const auto factions = RequirePacket(response, 10, "factionList").at("factions");
		assert(factions.size() == 2);
		const auto& bravo = factions.at(0);
		const auto& alpha = factions.at(1);
		assert(bravo.at("name") == "Bravo Team" && bravo.at("onlineCount") == 2 && bravo.at("memberCount") == 2);
		assert(bravo.at("leader") == "Stranger" && bravo.at("yours") == false && bravo.at("tag") == "");
		assert(alpha.at("id") == alphaId && alpha.at("name") == "Alpha Team" && alpha.at("tag") == "ALP");
		assert(alpha.at("onlineCount") == 1 && alpha.at("memberCount") == 1);
		assert(alpha.at("description") == "Hold the line" && alpha.at("color") == "#FF00AA");
		assert(alpha.at("leader") == "Nomad" && alpha.at("yours") == true);
		for (const auto& entry : factions) {
			assert(entry.size() == 9);
			assert(entry.contains("id") && entry.contains("name") && entry.contains("tag") && entry.contains("color"));
			assert(entry.contains("description") && !entry.contains("motd") && entry.contains("leader") && entry.contains("memberCount"));
			assert(entry.contains("onlineCount") && entry.contains("yours"));
		}
		const auto dumped = RequirePacket(response, 10, "factionList").dump();
		assert(dumped.find("\"p") == std::string::npos);
		assert(dumped.find("members") == std::string::npos && dumped.find("officers") == std::string::npos);
		assert(dumped.find("leaderPlayerId") == std::string::npos);

		const auto outsider = RequirePacket(Send(host, 20, ListRequest(), 3.1, 102.1), 20, "factionList").at("factions");
		assert(outsider.size() == 2);
		assert(outsider.at(0).at("yours") == false && outsider.at(1).at("yours") == false);
		assert(outsider.at(0).at("leader") == "Stranger" && outsider.at(1).at("leader") == "Stranger");
	}

	void TestRequestsAreRateLimitedPerSession()
	{
		ProtocolHost host{ "Faction List Rate" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		assert(HasPacket(Send(host, 10, ListRequest(), 3.0, 101.0), 10, "factionList"));
		assert(Send(host, 10, ListRequest(), 3.5, 101.5).empty());
		assert(HasPacket(Send(host, 20, ListRequest(), 3.6, 101.6), 20, "factionList"));
		assert(HasPacket(Send(host, 10, ListRequest(), 4.0, 102.0), 10, "factionList"));
		assert(HasPacket(Send(host, 10, Json{ { "type", "playerRosterRequest" } }, 4.1, 102.1), 10, "playerRoster"));
	}

	void TestNameOrderIgnoresCase()
	{
		ProtocolHost host{ "Faction List Case" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		AssertResult(Send(host, 10, Create("Beta Crew"), 2.0, 101.0), 10, "create", true);
		AssertResult(Send(host, 20, Create("alpha Crew"), 2.0, 101.1), 20, "create", true);
		const auto listed = RequirePacket(Send(host, 30, ListRequest(), 3.0, 102.0), 30, "factionList").at("factions");
		assert(listed.size() == 2);
		assert(listed.at(0).at("name") == "alpha Crew" && listed.at(1).at("name") == "Beta Crew");
	}

	void TestListIsCappedAtSixtyFourSortedByName()
	{
		const auto directory = TempDirectory("faction-list-cap");
		const auto path = directory / "durable-state.json";
		SeedFactions(path, 65, "");
		auto store = OpenSeedStore(path);
		PersistenceGate gate(store);
		ProtocolHost host{ "Faction List Cap" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto listed = RequirePacket(Send(host, 10, ListRequest(), 2.0, 100.2), 10, "factionList").at("factions");
		assert(listed.size() == 64);
		assert(listed.at(0).at("name") == "Team 001" && listed.at(0).at("memberCount") == 1 && listed.at(0).at("onlineCount") == 0);
		assert(listed.at(63).at("name") == "Team 064");
		std::filesystem::remove_all(directory);
	}

	void TestRequesterFactionIsKeptPastTheCap()
	{
		const auto directory = TempDirectory("faction-list-requester");
		const auto path = directory / "durable-state.json";
		SeedFactions(path, 65, "p10");
		auto store = OpenSeedStore(path);
		PersistenceGate gate(store);
		ProtocolHost host{ "Faction List Requester" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		for (std::uint32_t number = 1; number <= 64; ++number) {
			const auto digits = std::to_string(1000 + number).substr(1);
			Join(host, 100 + number, "m" + digits, 1.0 + number * 0.01, 100.0 + number * 0.01);
		}
		const auto listed = RequirePacket(Send(host, 10, ListRequest(), 5.0, 110.0), 10, "factionList").at("factions");
		assert(listed.size() == 64);
		assert(listed.at(0).at("name") == "Team 001" && listed.at(62).at("name") == "Team 063");
		assert(listed.at(63).at("name") == "Team 065" && listed.at(63).at("yours") == true);
		for (const auto& entry : listed) assert(entry.at("name") != "Team 064");
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestDisabledProducesNoFrames();
	TestListsContractFieldsWithoutLeakingMembers();
	TestRequestsAreRateLimitedPerSession();
	TestNameOrderIgnoresCase();
	TestListIsCappedAtSixtyFourSortedByName();
	TestRequesterFactionIsKeptPastTheCap();
	return 0;
}
