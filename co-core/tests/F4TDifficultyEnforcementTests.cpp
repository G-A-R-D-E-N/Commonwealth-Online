#include "F4TDifficultyEnforcement.h"
#include "F4TDifficultyReport.h"

#include <cassert>

namespace DE = F4T::DifficultyEnforcement;
namespace DR = F4T::DifficultyReport;

namespace
{
	DE::SessionState Joined(double a_joinedAt)
	{
		DE::SessionState state;
		state.joinedAt = a_joinedAt;
		return state;
	}

	void TestValidRange()
	{
		assert(!DE::IsValidDifficulty(-1));
		assert(DE::IsValidDifficulty(0));
		assert(DE::IsValidDifficulty(4));
		assert(DE::IsValidDifficulty(6));
		assert(!DE::IsValidDifficulty(7));
	}

	void TestDisabledWithoutSurvivalMode()
	{
		DE::Settings off{ false, true };
		auto silent = Joined(0.0);
		assert(DE::Evaluate(silent, off, 500.0) == DE::Status::Disabled);
		auto wrong = Joined(0.0);
		wrong.reportedDifficulty = 0;
		assert(DE::Evaluate(wrong, off, 500.0) == DE::Status::Disabled);
	}

	void TestOldClientsWhenReportNotRequired()
	{
		DE::Settings lenient{ true, false };
		auto silent = Joined(0.0);
		assert(DE::Evaluate(silent, lenient, 500.0) == DE::Status::Disabled);
		auto wrong = Joined(0.0);
		wrong.reportedDifficulty = 2;
		assert(DE::Evaluate(wrong, lenient, 500.0) == DE::Status::Mismatch);
	}

	void TestSilenceAfterJoin()
	{
		DE::Settings strict{ true, true };
		const auto state = Joined(100.0);
		assert(DE::Evaluate(state, strict, 100.0) == DE::Status::Grace);
		assert(DE::Evaluate(state, strict, 190.0) == DE::Status::Grace);
		assert(DE::Evaluate(state, strict, 340.0) == DE::Status::Grace);
		assert(DE::Evaluate(state, strict, 340.5) == DE::Status::Silent);
	}

	void TestReportedValues()
	{
		DE::Settings strict{ true, true };
		auto state = Joined(0.0);
		state.reportedDifficulty = 4;
		assert(DE::Evaluate(state, strict, 1.0) == DE::Status::Ok);
		assert(DE::Evaluate(state, strict, 240.0) == DE::Status::Ok);
		state.reportedDifficulty = 0;
		assert(DE::Evaluate(state, strict, 59.9) == DE::Status::Grace);
		assert(DE::Evaluate(state, strict, 60.0) == DE::Status::Mismatch);
	}

	void TestFiveAndSixAreMismatch()
	{
		DE::Settings strict{ true, true };
		for (const std::int32_t value : { 5, 6 }) {
			auto state = Joined(0.0);
			state.reportedDifficulty = value;
			assert(DE::Evaluate(state, strict, 59.9) == DE::Status::Grace);
			assert(DE::Evaluate(state, strict, 240.0) == DE::Status::Mismatch);
		}
	}

	void TestCustomRequiredDifficulty()
	{
		DE::Settings normal{ true, true, 2 };
		auto state = Joined(0.0);
		state.reportedDifficulty = 2;
		state.reportedAt = 5000.0;
		assert(DE::Evaluate(state, normal, 5000.0) == DE::Status::Ok);
		state.reportedDifficulty = 4;
		assert(DE::Evaluate(state, normal, 59.9) == DE::Status::Grace);
		assert(DE::Evaluate(state, normal, 60.0) == DE::Status::Mismatch);
	}

	void TestReportGoesSilentWhenStale()
	{
		DE::Settings strict{ true, true };
		auto state = Joined(0.0);
		state.reportedDifficulty = 4;
		state.reportedAt = 10.0;
		assert(DE::Evaluate(state, strict, 100.0) == DE::Status::Ok);
		assert(DE::Evaluate(state, strict, 250.0) == DE::Status::Ok);
		assert(DE::Evaluate(state, strict, 251.0) == DE::Status::Silent);
		state.reportedAt = 250.0;
		assert(DE::Evaluate(state, strict, 251.0) == DE::Status::Ok);
	}

	void TestStaleMismatchReportGoesSilent()
	{
		DE::Settings strict{ true, true };
		auto state = Joined(0.0);
		state.reportedDifficulty = 2;
		state.reportedAt = 10.0;
		assert(DE::Evaluate(state, strict, 100.0) == DE::Status::Mismatch);
		assert(DE::Evaluate(state, strict, 251.0) == DE::Status::Silent);
	}

	void TestLenientNeverSilent()
	{
		DE::Settings lenient{ true, false };
		auto state = Joined(0.0);
		state.reportedDifficulty = 4;
		state.reportedAt = 10.0;
		assert(DE::Evaluate(state, lenient, 5000.0) == DE::Status::Ok);
		state.reportedDifficulty = 2;
		assert(DE::Evaluate(state, lenient, 5000.0) == DE::Status::Mismatch);
	}

	void TestWarnThenKickAfterThirtySeconds()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Mismatch, 100.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Mismatch, 129.9) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Mismatch, 130.0) == DE::Action::Kick);
		assert(DE::Step(state, DE::Status::Mismatch, 200.0) == DE::Action::None);
	}

	void TestReportTriggeredWarnDoesNotKickInSameEvaluation()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Mismatch, 170.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Mismatch, 170.1) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Silent, 170.2) == DE::Action::None);
	}

	void TestWarnOnlyOncePerViolation()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Mismatch, 0.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Mismatch, 29.9) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Mismatch, 30.0) == DE::Action::Kick);
	}

	void TestRegressionAfterFixIsWarnedAgain()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Mismatch, 0.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Ok, 1.0) == DE::Action::None);
		assert(!state.warnedAt);
		assert(DE::Step(state, DE::Status::Mismatch, 2.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Mismatch, 31.9) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Mismatch, 32.0) == DE::Action::Kick);
	}

	void TestSilentSweepEscalates()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Silent, 0.0) == DE::Action::Warn);
		assert(DE::Step(state, DE::Status::Silent, 10.0) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Silent, 30.0) == DE::Action::Kick);
	}

	void TestNonViolatingStatusesDoNotCount()
	{
		auto state = Joined(0.0);
		assert(DE::Step(state, DE::Status::Grace, 0.0) == DE::Action::None);
		assert(DE::Step(state, DE::Status::Disabled, 0.0) == DE::Action::None);
		assert(!state.warnedAt);
	}

	void TestResetTrackingClearsWarningAndKick()
	{
		auto state = Joined(0.0);
		state.reportedDifficulty = 2;
		state.warnedAt = 10.0;
		state.kicked = true;
		DE::ResetTracking(state, 500.0);
		assert(state.joinedAt == 500.0);
		assert(state.reportedAt == 500.0);
		assert(!state.warnedAt && !state.kicked);
		assert(state.reportedDifficulty && *state.reportedDifficulty == 2);
		assert(DE::Evaluate(state, DE::Settings{ true, true }, 510.0) == DE::Status::Grace);
		assert(DE::Step(state, DE::Status::Mismatch, 510.0) == DE::Action::Warn);
		assert(DE::Evaluate(state, DE::Settings{ true, true }, 741.0) == DE::Status::Silent);
	}

	void TestReportCadence()
	{
		DR::Cadence cadence;
		assert(DR::Due(cadence, 4, 0.0));
		cadence.lastSent = 4;
		cadence.lastSentAt = 0.0;
		assert(!DR::Due(cadence, 4, 29.9));
		assert(DR::Due(cadence, 4, 30.0));
		assert(DR::Due(cadence, 0, 1.0));
	}

	void TestLocalDifficultyCache()
	{
		DR::SetLocalDifficulty(-1);
		assert(!DR::LocalDifficulty());
		DR::SetLocalDifficulty(4);
		assert(DR::LocalDifficulty() && *DR::LocalDifficulty() == 4);
		DR::ClearLocalDifficulty();
		assert(!DR::LocalDifficulty());
		DR::SetLocalDifficulty(9);
		assert(!DR::LocalDifficulty());
		DR::SetServerAcceptsReports(false);
		assert(!DR::ServerAcceptsReports());
		DR::SetServerAcceptsReports(true);
		assert(DR::ServerAcceptsReports());
	}
}

int main()
{
	TestValidRange();
	TestDisabledWithoutSurvivalMode();
	TestOldClientsWhenReportNotRequired();
	TestSilenceAfterJoin();
	TestReportedValues();
	TestFiveAndSixAreMismatch();
	TestCustomRequiredDifficulty();
	TestReportGoesSilentWhenStale();
	TestStaleMismatchReportGoesSilent();
	TestLenientNeverSilent();
	TestWarnThenKickAfterThirtySeconds();
	TestReportTriggeredWarnDoesNotKickInSameEvaluation();
	TestWarnOnlyOncePerViolation();
	TestRegressionAfterFixIsWarnedAgain();
	TestSilentSweepEscalates();
	TestNonViolatingStatusesDoNotCount();
	TestResetTrackingClearsWarningAndKick();
	TestReportCadence();
	TestLocalDifficultyCache();
	return 0;
}
