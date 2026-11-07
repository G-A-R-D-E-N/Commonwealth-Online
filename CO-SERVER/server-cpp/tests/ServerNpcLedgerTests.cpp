#include "ServerNpcLedger.h"

#include "ServerSessionTypes.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace CommonwealthOnline::Server;
using NpcLedger::Ledger;
using NpcLedger::Scope;
using SessionTypes::kNpcKillCreditCooldownSeconds;
using SessionTypes::kNpcLedgerMaximumEntries;
using SessionTypes::kNpcQuarantineSeconds;
using SessionTypes::kNpcRespawnCooldownSeconds;

namespace
{
	const Scope kScope{ "CAFE", "F00D" };

	Ledger::NpcObservation Npc(std::uint32_t a_npcId, const char* a_sourceFormId, double a_x, bool a_dead = false)
	{
		return Ledger::NpcObservation{ a_npcId, a_sourceFormId, a_x, 0.0, 0.0, a_dead };
	}

	void Observe(Ledger& a_ledger, std::uint32_t a_epoch, std::vector<Ledger::NpcObservation> a_npcs, double a_monotonicSeconds)
	{
		a_ledger.ObserveSnapshot(kScope, a_epoch, a_npcs, a_monotonicSeconds);
	}

	void TestDeadThenAliveWithinCooldownIsQuarantined()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0, true) }, 100.0);
		assert(!ledger.Quarantined(kScope, "01000ABC", 100.0));

		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0) }, 200.0);
		assert(ledger.Quarantined(kScope, "01000ABC", 200.0));
		assert(ledger.Quarantined(kScope, "01000ABC", 100.0 + kNpcRespawnCooldownSeconds - 1.0));
		assert(!ledger.Quarantined(kScope, "01000ABC", 100.0 + kNpcRespawnCooldownSeconds));

		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0) }, 100.0 + kNpcRespawnCooldownSeconds + 1.0);
		assert(!ledger.Quarantined(kScope, "01000ABC", 100.0 + kNpcRespawnCooldownSeconds + 1.0));
	}

	void TestAliveToDeadIsNotQuarantined()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0) }, 100.0);
		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0, true) }, 101.0);
		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0, true) }, 102.0);
		assert(!ledger.Quarantined(kScope, "01000ABC", 102.0));
	}

	void TestTeleportIsQuarantined()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 0.0) }, 10.0);
		Observe(ledger, 1, { Npc(42, "01000ABC", 100000.0) }, 10.1);
		assert(ledger.Quarantined(kScope, "01000ABC", 10.1));
		assert(ledger.Quarantined(kScope, "01000ABC", 10.1 + kNpcQuarantineSeconds - 1.0));
		assert(!ledger.Quarantined(kScope, "01000ABC", 10.1 + kNpcQuarantineSeconds + 0.5));
	}

	void TestSlowMoveIsNotQuarantined()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 0.0) }, 10.0);
		Observe(ledger, 1, { Npc(42, "01000ABC", 1000.0) }, 11.0);
		assert(!ledger.Quarantined(kScope, "01000ABC", 11.0));
	}

	void TestSourceFormIdChangeIsQuarantined()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 20.0) }, 10.0);
		Observe(ledger, 1, { Npc(42, "02000ABC", 20.0) }, 10.5);
		assert(ledger.Quarantined(kScope, "02000ABC", 10.5));
	}

	void TestNewEpochResetsContinuity()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(42, "01000ABC", 0.0) }, 10.0);
		Observe(ledger, 2, { Npc(42, "01000ABC", 100000.0) }, 10.1);
		assert(!ledger.Quarantined(kScope, "01000ABC", 10.1));
	}

	void TestTombstoneSurvivesForgetScope()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(43, "03000ABC", 20.0, true) }, 20.0);
		ledger.ForgetScope(kScope);
		Observe(ledger, 2, { Npc(43, "03000ABC", 20.0) }, 25.0);
		assert(ledger.Quarantined(kScope, "03000ABC", 25.0));
	}

	void TestGapResetsContinuity()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(44, "04000ABC", 0.0) }, 10.0);
		Observe(ledger, 1, { Npc(44, "04000ABC", 100000.0) }, 16.0);
		assert(!ledger.Quarantined(kScope, "04000ABC", 16.0));
	}

	void TestKillClaimIsPerProfileAndRef()
	{
		Ledger ledger;
		assert(!ledger.KillClaimed("p10", kScope, "01000ABC", 10.0));
		ledger.RecordKill("p10", kScope, "01000ABC", 10.0);
		assert(ledger.KillClaimed("p10", kScope, "01000ABC", 10.0));
		assert(!ledger.KillClaimed("p20", kScope, "01000ABC", 10.0));
		assert(!ledger.KillClaimed("p10", kScope, "02000ABC", 10.0));
		assert(ledger.KillClaimed("p10", kScope, "01000ABC", 10.0 + kNpcKillCreditCooldownSeconds - 1.0));
		assert(!ledger.KillClaimed("p10", kScope, "01000ABC", 10.0 + kNpcKillCreditCooldownSeconds));
	}

	void TestCapEvictsOldestEntries()
	{
		Ledger ledger;
		for (std::size_t index = 0; index < kNpcLedgerMaximumEntries + 100; ++index) {
			ledger.RecordKill("p" + std::to_string(index), kScope, "01000ABC", 10.0 + 0.001 * static_cast<double>(index));
		}
		assert(!ledger.KillClaimed("p0", kScope, "01000ABC", 11.0));
		assert(ledger.KillClaimed("p100", kScope, "01000ABC", 11.0));
		assert(ledger.KillClaimed("p" + std::to_string(kNpcLedgerMaximumEntries + 99), kScope, "01000ABC", 11.0));
	}

	void TestRuntimeNpcTombstonesAreKeyedByNpcId()
	{
		Ledger ledger;
		Observe(ledger, 1, { Npc(7, "00000000", 20.0), Npc(8, "00000000", 40.0) }, 100.0);
		Observe(ledger, 1, { Npc(7, "00000000", 20.0, true), Npc(8, "00000000", 40.0) }, 101.0);
		Observe(ledger, 1, { Npc(7, "00000000", 20.0), Npc(8, "00000000", 40.0) }, 102.0);
		assert(ledger.Quarantined(kScope, NpcLedger::LedgerSourceKey(1, 7, "00000000"), 102.0));
		assert(!ledger.Quarantined(kScope, NpcLedger::LedgerSourceKey(1, 8, "00000000"), 102.0));
		assert(!ledger.Quarantined(kScope, NpcLedger::LedgerSourceKey(2, 7, "00000000"), 102.0));
		assert(!ledger.Quarantined(kScope, "00000000", 102.0));
	}

	void TestRuntimeKillClaimsAreKeyedByNpcId()
	{
		Ledger ledger;
		ledger.RecordKill("p10", kScope, NpcLedger::LedgerSourceKey(1, 7, "00000000"), 10.0);
		assert(ledger.KillClaimed("p10", kScope, NpcLedger::LedgerSourceKey(1, 7, "00000000"), 10.0));
		assert(!ledger.KillClaimed("p10", kScope, NpcLedger::LedgerSourceKey(1, 8, "00000000"), 10.0));
		assert(!ledger.KillClaimed("p10", kScope, NpcLedger::LedgerSourceKey(2, 7, "00000000"), 10.0));
		assert(!ledger.KillClaimed("p10", kScope, "00000000", 10.0));
	}
}

int main()
{
	TestDeadThenAliveWithinCooldownIsQuarantined();
	TestAliveToDeadIsNotQuarantined();
	TestTeleportIsQuarantined();
	TestSlowMoveIsNotQuarantined();
	TestSourceFormIdChangeIsQuarantined();
	TestNewEpochResetsContinuity();
	TestTombstoneSurvivesForgetScope();
	TestGapResetsContinuity();
	TestKillClaimIsPerProfileAndRef();
	TestCapEvictsOldestEntries();
	TestRuntimeNpcTombstonesAreKeyedByNpcId();
	TestRuntimeKillClaimsAreKeyedByNpcId();
	return 0;
}
