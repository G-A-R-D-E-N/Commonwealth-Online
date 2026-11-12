#include "F4TQuestAssistOrigin.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using F4T::QuestAssistOrigin::Clear;
	using F4T::QuestAssistOrigin::HitContext;
	using F4T::QuestAssistOrigin::kHitWindowMilliseconds;
	using F4T::QuestAssistOrigin::kMaximumCandidates;
	using F4T::QuestAssistOrigin::kMaximumPendingDeaths;
	using F4T::QuestAssistOrigin::kMaximumTrackedHits;
	using F4T::QuestAssistOrigin::MatchActorObjectives;
	using F4T::QuestAssistOrigin::ObjectiveTarget;
	using F4T::QuestAssistOrigin::ObjectiveTargetsContain;
	using F4T::QuestAssistOrigin::QueueDeath;
	using F4T::QuestAssistOrigin::RecordSentHit;
	using F4T::QuestAssistOrigin::SetEnabled;
	using F4T::QuestAssistOrigin::TakeDeathWithHit;

	const std::string kQuest = "00ABCD:commonwealthonline.esp";
	const std::string kOther = "001234:fallout4.esm";
	constexpr std::uint32_t kTarget = 0x00ABCDEFu;
	constexpr std::uint32_t kOtherTarget = 0x00000111u;
	constexpr std::uint32_t kFirstQueuedTarget = 0x00000100u;

	HitContext Hit(std::uint32_t a_targetFormId, std::uint64_t a_now)
	{
		return HitContext{ 11, 5, a_targetFormId, a_now };
	}

	ObjectiveTarget Objective(const std::string& a_quest, std::uint32_t a_objectiveId, std::vector<std::uint32_t> a_actors)
	{
		return ObjectiveTarget{ a_quest, a_objectiveId, std::move(a_actors) };
	}

	std::vector<std::string> KeysOf(std::uint32_t a_actorFormId)
	{
		if (a_actorFormId == 111) {
			return { "00AAAA:fallout4.esm" };
		}
		if (a_actorFormId == 222) {
			return { "00BBBB:fallout4.esm", "00CCCC:fallout4.esm" };
		}
		return {};
	}

	void AssistTargetMustBeOnTheCreditedObjective()
	{
		const std::vector<ObjectiveTarget> objectives{
			Objective(kQuest, 10, { 111 }),
			Objective(kQuest, 20, { 222 }),
		};
		assert(ObjectiveTargetsContain(objectives, 10, "00AAAA:fallout4.esm", KeysOf));
		assert(ObjectiveTargetsContain(objectives, 20, "00CCCC:fallout4.esm", KeysOf));
		assert(!ObjectiveTargetsContain(objectives, 20, "00AAAA:fallout4.esm", KeysOf));
	}

	void AWrongOrEmptyTargetIsRejected()
	{
		const std::vector<ObjectiveTarget> objectives{ Objective(kQuest, 10, { 111 }) };
		assert(!ObjectiveTargetsContain(objectives, 10, "", KeysOf));
		assert(!ObjectiveTargetsContain(objectives, 10, "00BBBB:fallout4.esm", KeysOf));
		assert(!ObjectiveTargetsContain(objectives, 10, "00AAAA:other.esp", KeysOf));
	}

	void AnUndisplayedObjectiveHasNoTargets()
	{
		assert(!ObjectiveTargetsContain({}, 10, "00AAAA:fallout4.esm", KeysOf));
	}

	void MatchesTheObjectiveWhoseTargetIsTheDeadActor()
	{
		const std::vector<ObjectiveTarget> objectives{
			Objective(kQuest, 10, { 111, 222 }),
			Objective(kQuest, 20, { 333 }),
			Objective(kOther, 5, { 222 }),
		};
		const auto match = MatchActorObjectives(objectives, 222, Hit(222, 5000), 5000);
		assert(match.has_value());
		assert(match->candidates.size() == 2);
		assert(match->candidates[0].quest == kQuest);
		assert(match->candidates[0].objectiveId == 10);
		assert(match->candidates[1].quest == kOther);
		assert(match->hit.attackSequence == 11);
		assert(match->hit.sequence == 5);
	}

	void RejectsAnActorWithNoObjectiveTarget()
	{
		const std::vector<ObjectiveTarget> objectives{ Objective(kQuest, 10, { 111 }) };
		assert(!MatchActorObjectives(objectives, 999, Hit(999, 5000), 5000).has_value());
		assert(!MatchActorObjectives({}, 111, Hit(111, 5000), 5000).has_value());
	}

	void RejectsAHitForAnotherActor()
	{
		const std::vector<ObjectiveTarget> objectives{ Objective(kQuest, 10, { 111 }) };
		assert(!MatchActorObjectives(objectives, 111, Hit(222, 5000), 5000).has_value());
		assert(MatchActorObjectives(objectives, 111, Hit(111, 5000), 5000).has_value());
	}

	void RejectsAHitWithoutAnAcceptedContext()
	{
		const std::vector<ObjectiveTarget> objectives{ Objective(kQuest, 10, { 111 }) };
		const std::vector<HitContext> unaccepted{
			HitContext{ 0, 5, 111, 5000 },
			HitContext{ 11, 0, 111, 5000 },
			HitContext{ 11, 5, 0, 5000 },
		};
		for (const auto& hit : unaccepted) {
			assert(!MatchActorObjectives(objectives, 111, hit, 5000).has_value());
		}
		assert(!MatchActorObjectives(objectives, 0, Hit(111, 5000), 5000).has_value());
	}

	void RejectsStaleAndFutureHits()
	{
		const std::vector<ObjectiveTarget> objectives{ Objective(kQuest, 10, { 111 }) };
		const auto hit = Hit(111, 5000);
		assert(MatchActorObjectives(objectives, 111, hit, 5000 + kHitWindowMilliseconds).has_value());
		assert(!MatchActorObjectives(objectives, 111, hit, 5000 + kHitWindowMilliseconds + 1).has_value());
		assert(!MatchActorObjectives(objectives, 111, hit, 4999).has_value());
	}

	void CapsAndDeduplicatesCandidates()
	{
		const std::vector<ObjectiveTarget> objectives{
			Objective(kQuest, 0, { 111 }),
			Objective(kQuest, 0, { 111 }),
			Objective(kQuest, 1, { 111 }),
			Objective(kQuest, 2, { 111 }),
			Objective(kQuest, 3, { 111 }),
			Objective(kQuest, 4, { 111 }),
		};
		const auto match = MatchActorObjectives(objectives, 111, Hit(111, 5000), 5000);
		assert(match.has_value());
		assert(match->candidates.size() == kMaximumCandidates);
		for (std::size_t index = 0; index < kMaximumCandidates; ++index) {
			assert(match->candidates[index].objectiveId == index);
		}
	}

	void SkipsObjectivesWithNoQuest()
	{
		const std::vector<ObjectiveTarget> objectives{
			Objective("", 10, { 111 }),
			Objective(kQuest, 20, { 111 }),
		};
		const auto match = MatchActorObjectives(objectives, 111, Hit(111, 5000), 5000);
		assert(match.has_value());
		assert(match->candidates.size() == 1);
		assert(match->candidates[0].objectiveId == 20);
	}

	void ADeathWaitsForItsHitAndBacksOneClaim()
	{
		Clear();
		QueueDeath(kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		RecordSentHit(1, 11, kTarget, 1100);
		const auto matched = TakeDeathWithHit(1100);
		assert(matched.has_value());
		assert(matched->deadFormId == kTarget);
		assert(matched->hit.sequence == 1);
		assert(matched->hit.attackSequence == 11);
		assert(matched->hit.targetFormId == kTarget);
		QueueDeath(kTarget, 1100);
		assert(!TakeDeathWithHit(1100).has_value());
		Clear();
	}

	void AMostRecentHitWins()
	{
		Clear();
		RecordSentHit(1, 11, kTarget, 1000);
		RecordSentHit(2, 22, kTarget, 1100);
		QueueDeath(kTarget, 1200);
		const auto matched = TakeDeathWithHit(1200);
		assert(matched.has_value());
		assert(matched->hit.sequence == 2);
		assert(matched->hit.attackSequence == 22);
		Clear();
	}

	void AHitOnlyBacksItsOwnTarget()
	{
		Clear();
		RecordSentHit(1, 11, kTarget, 1000);
		QueueDeath(kOtherTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		QueueDeath(kTarget, 1000);
		const auto matched = TakeDeathWithHit(1000);
		assert(matched.has_value());
		assert(matched->deadFormId == kTarget);
		Clear();
	}

	void AStaleHitDoesNotBackAFreshDeath()
	{
		Clear();
		RecordSentHit(1, 11, kTarget, 1000);
		QueueDeath(kTarget, 1000 + kHitWindowMilliseconds + 1);
		assert(!TakeDeathWithHit(1000 + kHitWindowMilliseconds + 1).has_value());
		Clear();
	}

	void ADeathExpiresWithItsWindow()
	{
		Clear();
		QueueDeath(kTarget, 1000);
		assert(!TakeDeathWithHit(1000 + kHitWindowMilliseconds + 1).has_value());
		RecordSentHit(2, 22, kTarget, 1000 + kHitWindowMilliseconds + 1);
		assert(!TakeDeathWithHit(1000 + kHitWindowMilliseconds + 1).has_value());
		Clear();
	}

	void InvalidHitsAreNotRecorded()
	{
		Clear();
		RecordSentHit(0, 11, kTarget, 1000);
		RecordSentHit(1, 0, kTarget, 1000);
		RecordSentHit(1, 11, 0, 1000);
		QueueDeath(kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		Clear();
	}

	void TheDeathQueueIsBounded()
	{
		Clear();
		for (std::uint32_t index = 0; index <= kMaximumPendingDeaths; ++index) {
			QueueDeath(kFirstQueuedTarget + index, 1000);
		}
		const auto newestTarget = kFirstQueuedTarget + static_cast<std::uint32_t>(kMaximumPendingDeaths);
		RecordSentHit(1, 11, kFirstQueuedTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		RecordSentHit(2, 22, newestTarget, 1000);
		const auto matched = TakeDeathWithHit(1000);
		assert(matched.has_value());
		assert(matched->deadFormId == newestTarget);
		Clear();
	}

	void TheOldestHitsAreDroppedWhenTheLedgerIsFull()
	{
		Clear();
		for (std::uint32_t index = 0; index < kMaximumTrackedHits; ++index) {
			RecordSentHit(index + 1, index + 1, kTarget, 1000 + index);
		}
		RecordSentHit(99, 99, kTarget, 2000);
		std::vector<std::uint32_t> attackSequences;
		for (std::size_t taken = 0; taken <= kMaximumTrackedHits; ++taken) {
			QueueDeath(kTarget, 2000);
			const auto matched = TakeDeathWithHit(2000);
			if (!matched.has_value()) {
				break;
			}
			attackSequences.push_back(matched->hit.attackSequence);
		}
		assert(attackSequences.size() == kMaximumTrackedHits);
		assert(attackSequences.front() == 99);
		for (std::size_t index = 1; index < attackSequences.size(); ++index) {
			assert(attackSequences[index] == kMaximumTrackedHits + 1 - index);
		}
		Clear();
	}

	void ClearEmptiesTheLedgerAndTheDeathQueue()
	{
		Clear();
		RecordSentHit(1, 11, kTarget, 1000);
		QueueDeath(kTarget, 1000);
		Clear();
		RecordSentHit(2, 22, kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		QueueDeath(kTarget, 1000);
		const auto matched = TakeDeathWithHit(1000);
		assert(matched.has_value());
		assert(matched->hit.sequence == 2);
		QueueDeath(kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		Clear();
	}

	void RecordsNothingWhileOriginationIsDisabled()
	{
		Clear();
		SetEnabled(false);
		RecordSentHit(1, 11, kTarget, 1000);
		SetEnabled(true);
		QueueDeath(kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		Clear();
		SetEnabled(false);
		QueueDeath(kTarget, 1000);
		SetEnabled(true);
		RecordSentHit(2, 22, kTarget, 1000);
		assert(!TakeDeathWithHit(1000).has_value());
		Clear();
	}
}

int main()
{
	SetEnabled(true);
	MatchesTheObjectiveWhoseTargetIsTheDeadActor();
	RejectsAnActorWithNoObjectiveTarget();
	RejectsAHitForAnotherActor();
	RejectsAHitWithoutAnAcceptedContext();
	RejectsStaleAndFutureHits();
	CapsAndDeduplicatesCandidates();
	SkipsObjectivesWithNoQuest();
	ADeathWaitsForItsHitAndBacksOneClaim();
	AMostRecentHitWins();
	AHitOnlyBacksItsOwnTarget();
	AStaleHitDoesNotBackAFreshDeath();
	ADeathExpiresWithItsWindow();
	InvalidHitsAreNotRecorded();
	TheDeathQueueIsBounded();
	TheOldestHitsAreDroppedWhenTheLedgerIsFull();
	ClearEmptiesTheLedgerAndTheDeathQueue();
	RecordsNothingWhileOriginationIsDisabled();
	AssistTargetMustBeOnTheCreditedObjective();
	AWrongOrEmptyTargetIsRejected();
	AnUndisplayedObjectiveHasNoTargets();
	return 0;
}
