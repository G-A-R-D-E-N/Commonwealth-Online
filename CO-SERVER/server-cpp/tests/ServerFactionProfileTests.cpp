#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	void TestLookalikeNamesAreTaken()
	{
		ProtocolHost host{ "Factions Lookalike" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		AssertResult(Send(host, 10, Create("Black Hand"), 2.0, 101.0), 10, "create", true);
		AssertResult(Send(host, 20, Create("BlackHand"), 2.0, 101.1), 20, "create", false, "nameTaken");
		AssertResult(Send(host, 30, Create("Black-Hand"), 2.0, 101.2), 30, "create", false, "nameTaken");
	}

	void TestPunctuationOnlyNamesRejected()
	{
		ProtocolHost host{ "Factions Punctuation" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		AssertResult(Send(host, 10, Create("---"), 2.0, 101.0), 10, "create", false, "invalidName");
		AssertResult(Send(host, 20, Create("'''"), 2.0, 101.1), 20, "create", false, "invalidName");
	}

	void TestLeaderSetsProfileForEveryMember()
	{
		ProtocolHost host{ "Factions Profile Set" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Profile Team");

		const auto frames = Send(host, 10, SetProfile("  Hold the line  ", "#ff00aa"), 3.0, 102.0);
		AssertResult(frames, 10, "setProfile", true);
		const auto leaderState = RequirePacket(frames, 10, "factionState").at("faction");
		assert(leaderState.at("description") == "Hold the line" && leaderState.at("color") == "#FF00AA");
		const auto memberState = RequirePacket(frames, 20, "factionState").at("faction");
		assert(memberState.at("description") == "Hold the line" && memberState.at("color") == "#FF00AA");
	}

	void TestNonLeaderIsRefused()
	{
		ProtocolHost host{ "Factions Profile Member" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Member Profile Team");

		AssertResult(Send(host, 20, SetProfile("Nope", "#123456"), 3.0, 102.0), 20, "setProfile", false, "notLeader");
	}

	void TestOutsiderIsRefused()
	{
		ProtocolHost host{ "Factions Profile Outsider" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);

		AssertResult(Send(host, 20, SetProfile("Nope", ""), 3.0, 102.0), 20, "setProfile", false, "notLeader");
	}

	void TestInvalidProfileProducesNoFrames()
	{
		ProtocolHost host{ "Factions Profile Invalid" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Invalid Profile Team");

		assert(Send(host, 10, SetProfile(std::string(201, 'a'), ""), 3.0, 102.0).empty());
		assert(Send(host, 10, SetProfile("bad\x01", ""), 3.1, 102.1).empty());
		assert(Send(host, 10, SetProfile("", "#12345"), 3.2, 102.2).empty());
		assert(Send(host, 10, SetProfile("", "#GGGGGG"), 3.3, 102.3).empty());
		assert(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "" } }, 3.4, 102.4).empty());
		assert(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", 5 }, { "color", "" } }, 3.5, 102.5).empty());
	}

	void TestProfileClearsWithEmptyStrings()
	{
		ProtocolHost host{ "Factions Profile Clear" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Clear Profile Team");
		AssertResult(Send(host, 10, SetProfile("Set first", "#00FF00"), 3.0, 102.0), 10, "setProfile", true);

		const auto frames = Send(host, 10, SetProfile("", ""), 4.0, 103.0);
		AssertResult(frames, 10, "setProfile", true);
		const auto state = RequirePacket(frames, 20, "factionState").at("faction");
		assert(state.at("description") == "" && state.at("color") == "");
	}

	void TestBlockedDescriptionIsRefused()
	{
		ProtocolHost host{ "Factions Profile Blocked" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Blocked Profile Team");

		const auto frames = Send(host, 10, SetProfile("Shit happens", "#FF0000"), 3.0, 102.0);
		AssertResult(frames, 10, "setProfile", false, "invalidDescription");
		assert(!HasPacket(frames, 20, "factionState"));
		const auto symbolFrames = Send(host, 10, SetProfile("$hit happens", ""), 3.5, 102.5);
		AssertResult(symbolFrames, 10, "setProfile", false, "invalidDescription");
		assert(!HasPacket(symbolFrames, 20, "factionState"));
		AssertResult(Send(host, 10, SetProfile("Fine words", ""), 4.0, 103.0), 10, "setProfile", true);
	}

	void TestMotdIsSetKeptWhenAbsentAndCleared()
	{
		ProtocolHost host{ "Factions Motd Set" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Motd Team");

		const auto frames = Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "Hold" }, { "color", "" }, { "motd", "  Raid at dusk  " } }, 3.0, 102.0);
		AssertResult(frames, 10, "setProfile", true);
		assert(RequirePacket(frames, 20, "factionState").at("faction").at("motd") == "Raid at dusk");
		const auto kept = Send(host, 10, SetProfile("Hold", ""), 4.0, 103.0);
		AssertResult(kept, 10, "setProfile", true);
		assert(RequirePacket(kept, 20, "factionState").at("faction").at("motd") == "Raid at dusk");
		const auto cleared = Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "Hold" }, { "color", "" }, { "motd", "" } }, 5.0, 104.0);
		AssertResult(cleared, 10, "setProfile", true);
		assert(RequirePacket(cleared, 20, "factionState").at("faction").at("motd") == "");
	}

	void TestMotdFromNonLeaderIsRefused()
	{
		ProtocolHost host{ "Factions Motd Member" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Motd Member Team");

		AssertResult(Send(host, 20, Json{ { "type", "factionSetProfile" }, { "description", "" }, { "color", "" }, { "motd", "Nope" } }, 3.0, 102.0), 20, "setProfile", false, "notLeader");
	}

	void TestBlockedAndMalformedMotdAreRefused()
	{
		ProtocolHost host{ "Factions Motd Blocked" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Blocked Motd Team");

		const auto frames = Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "" }, { "color", "" }, { "motd", "Shit happens" } }, 3.0, 102.0);
		AssertResult(frames, 10, "setProfile", false, "invalidMotd");
		assert(!HasPacket(frames, 20, "factionState"));
		assert(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "" }, { "color", "" }, { "motd", std::string(1001, 'a') } }, 4.0, 103.0).empty());
		AssertResult(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "" }, { "color", "" }, { "motd", "Fine words" } }, 5.0, 104.0), 10, "setProfile", true);
	}
}

int main()
{
	TestLookalikeNamesAreTaken();
	TestPunctuationOnlyNamesRejected();
	TestLeaderSetsProfileForEveryMember();
	TestNonLeaderIsRefused();
	TestOutsiderIsRefused();
	TestInvalidProfileProducesNoFrames();
	TestProfileClearsWithEmptyStrings();
	TestBlockedDescriptionIsRefused();
	TestMotdIsSetKeptWhenAbsentAndCleared();
	TestMotdFromNonLeaderIsRefused();
	TestBlockedAndMalformedMotdAreRefused();
	return 0;
}
