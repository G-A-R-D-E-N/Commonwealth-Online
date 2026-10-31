#include "ServerFactionTestSupport.h"

#include <iostream>
#include <sstream>

using namespace FactionTestSupport;

namespace
{
	void TestPersistenceRoundTrip()
	{
		const auto directory = TempDirectory("persist");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Persist" };
			assert(host.EnableFactions(&gate));
			const auto leader = Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Leader" } }, 1.5, 100.5);
			Send(host, 10, Create("Persisted Team", "PER"), 2.0, 101.0);
			const auto inviteId = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
			AssertResult(Send(host, 20, Respond(inviteId, true), 3.1, 102.1), 20, "inviteResponse", true);
			assert(leader.playerId != member.playerId);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Persist" };
			assert(host.EnableFactions(&gate));
			const auto returning = Join(host, 30, "p20", 1.0, 200.0);
			const auto state = RequirePacket(returning.hello, 30, "factionState").at("faction");
			assert(state.at("name") == "Persisted Team");
			assert(state.at("tag") == "PER");
			assert(state.at("members").size() == 2);
			assert(state.at("members").at(0).at("name") == "Leader");
			assert(state.at("members").at(0).at("online") == false);
			assert(state.at("members").at(1).at("online") == true);
		}
		std::filesystem::remove_all(directory);
	}

	void TestPersistenceFailureRestoresState()
	{
		const auto directory = TempDirectory("save-failure");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Save Failure" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		std::filesystem::remove_all(directory);
		AssertResult(Send(host, 10, Create("Doomed Team"), 2.0, 101.0), 10, "create", false, "persistenceUnavailable");
		AssertResult(Send(host, 10, Create("Doomed Team"), 3.0, 102.0), 10, "create", false, "persistenceUnavailable");
	}

	void TestLoadDropsCorruptAndCollidingFactions()
	{
		const auto directory = TempDirectory("load");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{
				{ "nextFactionNumber", 3 },
				{ "factions", Json{
					{ "f-1", Stored("Alpha Team", "ALP", "p1", Json::array({ "p1", "p2" }), Json{ { "p1", "One" }, { "p2", "Two" } }) },
					{ "f-2", Stored("Beta Team", "", "p3", Json::array({ "p2", "p3" }), Json::object()) },
					{ "f-3", Stored("x", "", "p4", Json::array({ "p4" }), Json::object()) },
					{ "f-4", Stored("Gamma Team", "", "p9", Json::array({ "p5" }), Json::object()) },
					{ "f-5", Stored("Delta Team", "", "p6", Json::array(), Json::object()) },
					{ "f-6", Stored("alpha team", "", "p7", Json::array({ "p7" }), Json::object()) },
					{ "f-7", Stored("Epsilon Team", "ALP", "p8", Json::array({ "p8" }), Json::object()) },
					{ "f-8", Stored("Zeta Team", "", "p10", Json::array({ "p10" }), Json::object()) },
					{ "f-9", Stored("Al-pha Team", "", "p12", Json::array({ "p12" }), Json::object()) }
				} }
			});
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Load" };
			std::ostringstream log;
			auto* previous = std::cerr.rdbuf(log.rdbuf());
			assert(host.EnableFactions(&gate));
			std::cerr.rdbuf(previous);
			assert(log.str().find("Faction f-2 dropped member p2 because it already belongs to another faction.") != std::string::npos);
			const auto alpha = Join(host, 10, "p1", 1.0, 100.0);
			assert(RequirePacket(alpha.hello, 10, "factionState").at("faction").at("name") == "Alpha Team");
			const auto keptFirst = Join(host, 20, "p2", 1.1, 100.2);
			assert(RequirePacket(keptFirst.hello, 20, "factionState").at("faction").at("name") == "Alpha Team");
			const auto beta = Join(host, 30, "p3", 1.2, 100.4);
			assert(RequirePacket(beta.hello, 30, "factionState").at("faction").at("name") == "Beta Team");
			assert(RequirePacket(beta.hello, 30, "factionState").at("faction").at("members").size() == 1);

			Join(host, 40, "p9", 1.3, 100.6);
			AssertResult(Send(host, 40, Create("Gamma Team"), 2.0, 101.0), 40, "create", true);
			Join(host, 50, "p6", 1.4, 100.8);
			AssertResult(Send(host, 50, Create("Delta Team"), 2.0, 101.0), 50, "create", true);
			Join(host, 60, "p8", 1.5, 101.0);
			AssertResult(Send(host, 60, Create("Epsilon Team"), 2.0, 101.1), 60, "create", true);
			Join(host, 70, "p11", 1.6, 101.2);
			const auto lookalike = Join(host, 80, "p12", 1.7, 101.4);
			assert(RequirePacket(lookalike.hello, 80, "factionState").at("faction").is_null());
			AssertResult(Send(host, 70, Create("Zeta Team"), 2.0, 101.3), 70, "create", false, "nameTaken");
			AssertResult(Send(host, 70, Create("Tagged Team", "ALP"), 3.0, 102.0), 70, "create", false, "tagTaken");
			const auto newer = Send(host, 70, Create("Newer Team"), 4.0, 103.0);
			AssertResult(newer, 70, "create", true);
			assert(RequirePacket(newer, 70, "factionState").at("faction").at("factionId") == "f-13");
		}
		std::filesystem::remove_all(directory);
	}

	void TestSaveFailureRollsBackAcceptLeaveAndKick()
	{
		const auto directory = TempDirectory("rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto target = Join(host, 30, "p30", 1.2, 100.4);
		Join(host, 40, "p40", 1.3, 100.6);
		const auto probe = Join(host, 50, "p50", 1.4, 100.8);
		AssertResult(Send(host, 10, Create("Rollback Team"), 2.0, 101.0), 10, "create", true);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		AssertResult(Send(host, 40, Create("Solo Team"), 2.5, 101.5), 40, "create", true);
		const auto targetInvite = InviteIdFrom(Send(host, 10, Invite(target.playerId), 4.0, 103.0), 30);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		AssertResult(Send(host, 30, Respond(targetInvite, true), 5.0, 104.0), 30, "inviteResponse", false, "persistenceUnavailable");
		AssertResult(Send(host, 30, Respond(targetInvite, true), 6.0, 105.0), 30, "inviteResponse", false, "persistenceUnavailable");
		AssertResult(Send(host, 10, Kick("p20"), 7.0, 106.0), 10, "kick", false, "persistenceUnavailable");
		AssertResult(Send(host, 20, Create("Rejoin Team"), 7.5, 106.5), 20, "create", false, "alreadyInFaction");
		AssertResult(Send(host, 10, Leave(), 9.0, 108.0), 10, "leave", false, "persistenceUnavailable");
		AssertResult(Send(host, 10, Invite(probe.playerId), 10.0, 109.0), 10, "invite", true);
		AssertResult(Send(host, 10, Create("Leader Again"), 11.0, 110.0), 10, "create", false, "alreadyInFaction");
		AssertResult(Send(host, 40, Leave(), 12.0, 111.0), 40, "leave", false, "persistenceUnavailable");
		AssertResult(Send(host, 50, Create("Solo Team"), 12.5, 111.5), 50, "create", false, "nameTaken");
		AssertResult(Send(host, 40, Create("Again Team"), 13.0, 112.0), 40, "create", false, "alreadyInFaction");
	}

	void TestLoadRejectsMalformedIdsAndClampsNextNumber()
	{
		const auto directory = TempDirectory("malformed");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const std::string longId(129, 'x');
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{
				{ "nextFactionNumber", 4000000000u },
				{ "factions", Json{
					{ "f-0", Stored("Zero Team", "", "q0", Json::array({ "q0" }), Json::object()) },
					{ "f-1234567890", Stored("Huge Team", "", "q1", Json::array({ "q1" }), Json::object()) },
					{ "g-5", Stored("Prefix Team", "", "q2", Json::array({ "q2" }), Json::object()) },
					{ "f-6", Stored("Long Team", "", longId, Json::array({ longId }), Json::object()) },
					{ "f-7", Stored("Empty Team", "", "", Json::array({ "" }), Json::object()) },
						{ "f-007", Stored("Pad Team", "", "q3", Json::array({ "q3" }), Json::object()) },
					{ "f-9", Stored("Valid Team", "", "v9", Json::array({ "v9" }), Json::object()) }
				} }
			});
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Malformed" };
			assert(host.EnableFactions(&gate));
			const auto valid = Join(host, 10, "v9", 1.0, 100.0);
			assert(RequirePacket(valid.hello, 10, "factionState").at("faction").at("factionId") == "f-9");
			Join(host, 20, "q0", 1.1, 100.2);
			const auto created = Send(host, 20, Create("Fresh Team"), 2.0, 101.0);
			AssertResult(created, 20, "create", true);
			assert(RequirePacket(created, 20, "factionState").at("faction").at("factionId") == "f-999999999");
			const auto q1 = Join(host, 30, "q1", 1.2, 100.4);
			assert(RequirePacket(q1.hello, 30, "factionState").at("faction").is_null());
			const auto q2 = Join(host, 31, "q2", 1.3, 100.6);
			assert(RequirePacket(q2.hello, 31, "factionState").at("faction").is_null());
			const auto q3 = Join(host, 32, "q3", 1.4, 100.8);
			assert(RequirePacket(q3.hello, 32, "factionState").at("faction").is_null());
			host.HandleConnected(33, 100.9);
			assert(RequirePacket(Send(host, 33, Json{ { "type", "hello" }, { "protocolVersion", 2 } }, 1.5, 101.0), 33, "factionState").at("faction").is_null());
			Join(host, 34, "n1", 1.6, 101.1);
			AssertResult(Send(host, 34, Create("Long Team"), 2.0, 101.2), 34, "create", true);
			Join(host, 35, "n2", 1.7, 101.3);
			AssertResult(Send(host, 35, Create("Empty Team"), 2.0, 101.4), 35, "create", true);
		}
		std::filesystem::remove_all(directory);
	}

	void TestFactionCapacity()
	{
		const auto directory = TempDirectory("capacity");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		Json factions = Json::object();
		for (std::uint32_t index = 1; index <= 512; ++index) {
			const auto leader = "m" + std::to_string(index);
			factions["f-" + std::to_string(index)] = Stored("Team " + std::to_string(100 + index), "", leader, Json::array({ leader }), Json::object());
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{ { "nextFactionNumber", 513 }, { "factions", factions } });
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Capacity" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		AssertResult(Send(host, 10, Create("Overflow Team"), 2.0, 101.0), 10, "create", false, "factionCapacity");
		std::filesystem::remove_all(directory);
	}

	void TestCreateIdWrapsToLowestFreeId()
	{
		const auto directory = TempDirectory("wrap");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{
				{ "nextFactionNumber", 999999999u },
				{ "factions", Json{ { "f-1", Stored("Low Team", "", "w1", Json::array({ "w1" }), Json::object()) } } }
			});
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Wrap" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "w2", 1.0, 100.0);
		const auto top = Send(host, 10, Create("Top Team"), 2.0, 101.0);
		AssertResult(top, 10, "create", true);
		assert(RequirePacket(top, 10, "factionState").at("faction").at("factionId") == "f-999999999");
		Join(host, 20, "w3", 1.1, 100.2);
		const auto wrapped = Send(host, 20, Create("Next Team"), 2.0, 101.0);
		AssertResult(wrapped, 20, "create", true);
		assert(RequirePacket(wrapped, 20, "factionState").at("faction").at("factionId") == "f-2");
		std::filesystem::remove_all(directory);
	}

	void TestDisbandPersistsAcrossReload()
	{
		const auto directory = TempDirectory("disband");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Disband Persist" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			AssertResult(Send(host, 10, Create("Gone Team"), 2.0, 101.0), 10, "create", true);
			const auto inviteId = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
			AssertResult(Send(host, 20, Respond(inviteId, true), 3.1, 102.1), 20, "inviteResponse", true);
			AssertResult(Send(host, 10, Disband(), 4.0, 103.0), 10, "disband", true);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Disband Reload" };
			assert(host.EnableFactions(&gate));
			assert(RequirePacket(Join(host, 10, "p10", 1.0, 200.0).hello, 10, "factionState").at("faction").is_null());
			assert(RequirePacket(Join(host, 20, "p20", 1.1, 200.2).hello, 20, "factionState").at("faction").is_null());
			AssertResult(Send(host, 10, Create("Gone Team"), 2.0, 201.0), 10, "create", true);
		}
		std::filesystem::remove_all(directory);
	}

	void TestDisbandRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("disband-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Disband Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto target = Join(host, 30, "p30", 1.2, 100.4);
		AssertResult(Send(host, 10, Create("Keep Team"), 2.0, 101.0), 10, "create", true);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		const auto targetInvite = InviteIdFrom(Send(host, 10, Invite(target.playerId), 4.0, 103.0), 30);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		AssertResult(Send(host, 10, Disband(), 5.0, 104.0), 10, "disband", false, "persistenceUnavailable");
		AssertResult(Send(host, 30, Respond(targetInvite, true), 6.0, 105.0), 30, "inviteResponse", false, "persistenceUnavailable");
		AssertResult(Send(host, 20, Leave(), 7.0, 106.0), 20, "leave", false, "persistenceUnavailable");
		AssertResult(Send(host, 10, Create("Other Team"), 8.0, 107.0), 10, "create", false, "alreadyInFaction");
	}

	void TestRejectedFactionsSurviveTheNextSave()
	{
		const auto directory = TempDirectory("quarantine");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto leaderOutside = Stored("Leaderless Team", "", "p2", Json::array({ "p1" }), Json::object());
		const auto badId = Stored("Zero Team", "", "p3", Json::array({ "p3" }), Json::object());
		const auto assertQuarantined = [&](const Json& a_saved) {
			assert(a_saved.contains("quarantinedFactions"));
			assert(a_saved.at("quarantinedFactions").size() == 2);
			assert(a_saved.at("quarantinedFactions").at("f-2").dump() == leaderOutside.dump());
			assert(a_saved.at("quarantinedFactions").at("f-0").dump() == badId.dump());
		};
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json{
				{ "nextFactionNumber", 3 },
				{ "factions", Json{
					{ "f-0", badId },
					{ "f-1", Stored("Alpha Team", "", "p1", Json::array({ "p1" }), Json{ { "p1", "One" } }) },
					{ "f-2", leaderOutside }
				} }
			});
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Quarantine" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p5", 1.0, 100.0);
			AssertResult(Send(host, 10, Create("Fresh Team"), 2.0, 101.0), 10, "create", true);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			assertQuarantined(store.GetFactions());
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Quarantine Reload" };
			assert(host.EnableFactions(&gate));
			assert(RequirePacket(Join(host, 10, "p1", 1.0, 200.0).hello, 10, "factionState").at("faction").at("name") == "Alpha Team");
			Join(host, 20, "p6", 1.1, 200.2);
			AssertResult(Send(host, 20, Create("Second Team"), 2.0, 201.0), 20, "create", true);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			assertQuarantined(store.GetFactions());
		}
		std::filesystem::remove_all(directory);
	}

	void TestQuarantineKeepsClashesAndMalformedStash()
	{
		const auto directory = TempDirectory("quarantine-clash");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		const auto clash = Stored("Alpha Team", "", "p2", Json::array({ "p2" }), Json::object());
		const auto run = [&](const Json& a_stash) {
			{
				DurableStateFileStore store(path, clock, instanceId);
				store.SaveFactions(Json{
					{ "nextFactionNumber", 3 },
					{ "quarantinedFactions", a_stash },
					{ "factions", Json{
						{ "f-1", Stored("Alpha Team", "", "p1", Json::array({ "p1" }), Json::object()) },
						{ "f-2", clash }
					} }
				});
				store.WaitForCommits();
			}
			{
				DurableStateFileStore store(path, clock, instanceId);
				PersistenceGate gate(store);
				ProtocolHost host{ "Factions Quarantine Clash" };
				assert(host.EnableFactions(&gate));
				Join(host, 10, "p5", 1.0, 100.0);
				AssertResult(Send(host, 10, Create("Fresh Team"), 2.0, 101.0), 10, "create", true);
				store.WaitForCommits();
			}
			DurableStateFileStore store(path, clock, instanceId);
			return store.GetFactions().at("quarantinedFactions");
		};
		const auto earlier = Json{ { "f-2", "kept from an earlier load" } };
		const auto kept = run(earlier);
		assert(kept.size() == 2 && kept.at("f-2") == earlier.at("f-2") && kept.at("f-2#2").dump() == clash.dump());
		const auto malformed = run(Json::array({ "not", "an", "object" }));
		assert(malformed.size() == 2 && malformed.at("quarantinedFactions") == Json::array({ "not", "an", "object" }));
		assert(malformed.at("f-2").dump() == clash.dump());
		std::filesystem::remove_all(directory);
	}

	void TestSetRankRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("setrank-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions SetRank Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		const auto outsider = Join(host, 30, "p30", 1.2, 100.4);
		AssertResult(Send(host, 10, Create("Rank Team"), 2.0, 101.0), 10, "create", true);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		AssertResult(Send(host, 10, SetRank("p20", "officer"), 4.0, 103.0), 10, "setRank", false, "persistenceUnavailable");
		AssertResult(Send(host, 20, Invite(outsider.playerId), 5.0, 104.0), 20, "invite", false, "notLeader");
	}

	void TestProfileRoundTrip()
	{
		const auto directory = TempDirectory("profile");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Profile Persist" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			FormFaction(host, 10, 20, member.playerId, "Profile Persist");
			AssertResult(Send(host, 10, Json{ { "type", "factionSetProfile" }, { "description", "Persisted words" }, { "color", "#0a0b0c" }, { "motd", "Persisted motd" } }, 3.0, 102.0), 10, "setProfile", true);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			assert(store.GetFactions().at("factions").at("f-1").at("description") == "Persisted words");
			assert(store.GetFactions().at("factions").at("f-1").at("motd") == "Persisted motd");
			assert(store.GetFactions().at("factions").at("f-1").at("color") == "#0A0B0C");
			ProtocolHost host{ "Factions Profile Persist" };
			assert(host.EnableFactions(&gate));
			const auto returning = Join(host, 20, "p20", 1.0, 200.0);
			const auto state = RequirePacket(returning.hello, 20, "factionState").at("faction");
			assert(state.at("description") == "Persisted words" && state.at("motd") == "Persisted motd" && state.at("color") == "#0A0B0C");
		}
		std::filesystem::remove_all(directory);
	}

	void TestProfileLoadIsLenient()
	{
		const auto directory = TempDirectory("profile-load");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			Json badProfile = Stored("Profile Team", "", "p3", Json::array({ "p3" }), Json::object());
			badProfile["description"] = 5;
			badProfile["motd"] = 5;
			badProfile["color"] = "red";
			Json mixedProfile = Stored("Mixed Team", "", "p4", Json::array({ "p4" }), Json::object());
			mixedProfile["description"] = "Kept words";
			mixedProfile["motd"] = "Kept motd";
			mixedProfile["color"] = "red";
			Json longProfile = Stored("Long Team", "", "p5", Json::array({ "p5" }), Json::object());
			longProfile["description"] = std::string(201, 'a');
			longProfile["motd"] = std::string(1001, 'a');
			longProfile["color"] = "#3ECF4A";
			Json controlProfile = Stored("Control Team", "", "p6", Json::array({ "p6" }), Json::object());
			controlProfile["description"] = "Bad\x01" "Text";
			controlProfile["color"] = "#3ECF4A";
			store.SaveFactions(Json{
				{ "nextFactionNumber", 5 },
				{ "factions", Json{ { "f-1", badProfile }, { "f-2", mixedProfile }, { "f-3", longProfile }, { "f-4", controlProfile } } }
			});
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Profile Load" };
			assert(host.EnableFactions(&gate));
			const auto bad = Join(host, 10, "p3", 1.0, 100.0);
			const auto badFaction = RequirePacket(bad.hello, 10, "factionState").at("faction");
			assert(badFaction.at("name") == "Profile Team" && badFaction.at("description") == "" && badFaction.at("motd") == "" && badFaction.at("color") == "");
			const auto mixed = Join(host, 20, "p4", 1.1, 100.2);
			const auto mixedFaction = RequirePacket(mixed.hello, 20, "factionState").at("faction");
			assert(mixedFaction.at("description") == "Kept words" && mixedFaction.at("motd") == "Kept motd" && mixedFaction.at("color") == "");
			const auto longJoin = Join(host, 30, "p5", 1.2, 100.4);
			const auto longFaction = RequirePacket(longJoin.hello, 30, "factionState").at("faction");
			assert(longFaction.at("name") == "Long Team" && longFaction.at("description") == "" && longFaction.at("motd") == "" && longFaction.at("color") == "#3ECF4A");
			const auto control = Join(host, 40, "p6", 1.3, 100.6);
			const auto controlFaction = RequirePacket(control.hello, 40, "factionState").at("faction");
			assert(controlFaction.at("name") == "Control Team" && controlFaction.at("description") == "" && controlFaction.at("motd") == "" && controlFaction.at("color") == "#3ECF4A");
		}
		std::filesystem::remove_all(directory);
	}

	void TestSetProfileRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("setprofile-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions SetProfile Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		AssertResult(Send(host, 10, Create("Profile Rollback"), 2.0, 101.0), 10, "create", true);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		AssertResult(Send(host, 10, SetProfile("Kept", "#112233"), 3.5, 102.5), 10, "setProfile", true);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		AssertResult(Send(host, 10, SetProfile("Lost", "#445566"), 4.0, 103.0), 10, "setProfile", false, "persistenceUnavailable");
		host.HandleDisconnected(20, 103.5);
		const auto rejoin = Join(host, 21, "p20", 5.0, 104.0);
		const auto state = RequirePacket(rejoin.hello, 21, "factionState").at("faction");
		assert(state.at("description") == "Kept" && state.at("color") == "#112233");
	}

	void TestOfficersRoundTrip()
	{
		const auto directory = TempDirectory("officers");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Officer Persist" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto officer = Join(host, 20, "p20", 1.1, 100.2);
			const auto member = Join(host, 30, "p30", 1.2, 100.4);
			Send(host, 10, Create("Officer Persist"), 2.0, 101.0);
			Send(host, 10, Invite(officer.playerId), 3.0, 102.0);
			AssertResult(Send(host, 20, Respond("i-1", true), 4.0, 103.0), 20, "inviteResponse", true);
			Send(host, 10, Invite(member.playerId), 5.0, 104.0);
			AssertResult(Send(host, 30, Respond("i-2", true), 6.0, 105.0), 30, "inviteResponse", true);
			AssertResult(Send(host, 10, SetRank("p20", "officer"), 7.0, 106.0), 10, "setRank", true);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			assert(store.GetFactions().at("factions").at("f-1").at("officers") == Json::array({ "p20" }));
			ProtocolHost host{ "Factions Officer Persist" };
			assert(host.EnableFactions(&gate));
			const auto returning = Join(host, 10, "p10", 1.0, 200.0);
			const auto members = RequirePacket(returning.hello, 10, "factionState").at("faction").at("members");
			assert(members.at(1).at("officer") == true);
			assert(members.at(2).at("officer") == false);
		}
		std::filesystem::remove_all(directory);
	}

	void TestLoadFiltersOfficersToMembers()
	{
		const auto directory = TempDirectory("officer-load");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			Json officerTeam = Stored("Officer Team", "", "p3", Json::array({ "p3", "p4" }), Json::object());
			officerTeam["officers"] = Json::array({ "p4", "p9", "p3", 5 });
			Json badOfficers = Stored("Bad Officers", "", "p5", Json::array({ "p5", "p6" }), Json::object());
			badOfficers["officers"] = "p6";
			store.SaveFactions(Json{
				{ "nextFactionNumber", 4 },
				{ "factions", Json{
					{ "f-1", Stored("Legacy Team", "", "p1", Json::array({ "p1", "p2" }), Json::object()) },
					{ "f-2", officerTeam },
					{ "f-3", badOfficers }
				} }
			});
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Officer Load" };
			assert(host.EnableFactions(&gate));
			const auto legacy = Join(host, 10, "p1", 1.0, 100.0);
			assert(RequirePacket(legacy.hello, 10, "factionState").at("faction").at("members").at(1).at("officer") == false);
			const auto officer = Join(host, 20, "p4", 1.1, 100.2);
			assert(RequirePacket(officer.hello, 20, "factionState").at("faction").at("members").at(1).at("officer") == true);
			const auto kept = Join(host, 40, "p6", 1.15, 100.3);
			const auto keptFaction = RequirePacket(kept.hello, 40, "factionState").at("faction");
			assert(keptFaction.at("name") == "Bad Officers" && keptFaction.at("members").at(1).at("officer") == false);
			Join(host, 30, "p3", 1.2, 100.4);
			AssertResult(Send(host, 30, SetRank("p4", "officer"), 2.0, 101.0), 30, "setRank", true);
			store.WaitForCommits();
			assert(store.GetFactions().at("factions").at("f-2").at("officers") == Json::array({ "p4" }));
		}
		std::filesystem::remove_all(directory);
	}

	void TestAdminDisbandPersistsAcrossReload()
	{
		const auto directory = TempDirectory("admin-disband");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Admin Disband Persist" };
			assert(host.EnableFactions(&gate));
			Join(host, 10, "p10", 1.0, 100.0);
			const auto member = Join(host, 20, "p20", 1.1, 100.2);
			const auto invitee = Join(host, 30, "p30", 1.2, 100.4);
			AssertResult(Send(host, 10, Create("Admin Gone Team"), 2.0, 101.0), 10, "create", true);
			const auto inviteId = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
			AssertResult(Send(host, 20, Respond(inviteId, true), 3.1, 102.1), 20, "inviteResponse", true);
			Send(host, 10, Invite(invitee.playerId), 3.2, 102.2);
			std::string failure;
			assert(host.AdminDisbandFaction("f-99", 3.5, failure).empty() && failure == "factionNotFound");
			assert(host.AdminListFactions().at("factions").at(0).at("memberCount") == 2);
			failure.clear();
			const auto frames = host.AdminDisbandFaction("f-1", 4.0, failure);
			assert(failure.empty());
			assert(RequirePacket(frames, 10, "factionState").at("faction").is_null());
			assert(RequirePacket(frames, 20, "factionState").at("faction").is_null());
			assert(RequirePacket(frames, 30, "factionState").at("pendingInvites").empty());
			assert(host.AdminListFactions().at("factions").empty());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Factions Admin Disband Reload" };
			assert(host.EnableFactions(&gate));
			assert(host.AdminListFactions().at("factions").empty());
			assert(RequirePacket(Join(host, 10, "p10", 1.0, 200.0).hello, 10, "factionState").at("faction").is_null());
			AssertResult(Send(host, 10, Create("Admin Gone Team"), 2.0, 201.0), 10, "create", true);
		}
		std::filesystem::remove_all(directory);
	}

	void TestAdminDisbandRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("admin-disband-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Admin Disband Rollback" };
		assert(host.EnableFactions(&gate));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		AssertResult(Send(host, 10, Create("Keep Team"), 2.0, 101.0), 10, "create", true);
		const auto memberInvite = InviteIdFrom(Send(host, 10, Invite(member.playerId), 3.0, 102.0), 20);
		AssertResult(Send(host, 20, Respond(memberInvite, true), 3.1, 102.1), 20, "inviteResponse", true);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		std::string failure;
		assert(host.AdminDisbandFaction("f-1", 5.0, failure).empty());
		assert(failure == "persistenceUnavailable");
		const auto listed = host.AdminListFactions().at("factions");
		assert(listed.size() == 1);
		assert(listed.at(0).at("members").size() == 2);
		AssertResult(Send(host, 20, Leave(), 6.0, 106.0), 20, "leave", false, "persistenceUnavailable");
		AssertResult(Send(host, 10, Create("Other Team"), 7.0, 107.0), 10, "create", false, "alreadyInFaction");
	}

	void TestLoadQuarantinesNonObjectRoot()
	{
		const auto directory = TempDirectory("root");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveFactions(Json::array({ "not", "an", "object" }));
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Factions Root" };
		assert(host.EnableFactions(&gate));
		assert(host.AdminListFactions().at("factions").empty());
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestPersistenceRoundTrip();
	TestPersistenceFailureRestoresState();
	TestSaveFailureRollsBackAcceptLeaveAndKick();
	TestLoadDropsCorruptAndCollidingFactions();
	TestLoadRejectsMalformedIdsAndClampsNextNumber();
	TestFactionCapacity();
	TestCreateIdWrapsToLowestFreeId();
	TestDisbandPersistsAcrossReload();
	TestDisbandRollsBackOnSaveFailure();
	TestLoadQuarantinesNonObjectRoot();
	TestRejectedFactionsSurviveTheNextSave();
	TestQuarantineKeepsClashesAndMalformedStash();
	TestSetRankRollsBackOnSaveFailure();
	TestOfficersRoundTrip();
	TestLoadFiltersOfficersToMembers();
	TestProfileRoundTrip();
	TestProfileLoadIsLenient();
	TestSetProfileRollsBackOnSaveFailure();
	TestAdminDisbandPersistsAcrossReload();
	TestAdminDisbandRollsBackOnSaveFailure();
	return 0;
}
