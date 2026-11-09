#include "F4TCombatActionContract.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

struct RecentAttack
{
	std::uint32_t sequence{};
	std::uint32_t type{};
	std::chrono::steady_clock::time_point capturedAt{};
};

int main()
{
	using Action = F4T::CombatActionContract::Action;
	using Clock = std::chrono::steady_clock;

	const auto captureAt = Clock::time_point{} + std::chrono::milliseconds{ 1 };
	const auto hitAt = captureAt + std::chrono::milliseconds{ 50 };
	const auto sendCompleteAt = captureAt + std::chrono::milliseconds{ 250 };

	Action action{};
	action.sequence = 17;
	action.type = 1;
	action.captureTime = captureAt;
	std::vector<Action> actions{ action };
	const auto actionSpan = std::span<const Action>{ actions };

	const auto nonSolo = F4T::CombatActionContract::SelectInitialActions(false, actionSpan);
	assert(nonSolo.size() == 1);
	assert(nonSolo.front().sequence == 17);
	assert(F4T::CombatActionContract::SelectInitialActions(true, actionSpan).empty());

	std::vector<Action> committed;
	auto record = [&](const Action& a_action) {
		committed.push_back(a_action);
	};
	F4T::CombatActionContract::CommitSentActions(false, nonSolo, record);
	assert(committed.empty());
	F4T::CombatActionContract::CommitSentActions(true, nonSolo, record);
	assert(committed.size() == 1);
	assert(committed.front().captureTime == captureAt);

	assert(sendCompleteAt > hitAt + F4T::CombatActionContract::kFutureSlack);
	assert(F4T::CombatActionContract::CanBind(
		committed.front().type,
		committed.front().captureTime,
		1,
		hitAt));
	assert(!F4T::CombatActionContract::CanBind(
		committed.front().type,
		sendCompleteAt,
		1,
		hitAt));

	const auto retryWindow = F4T::CombatActionContract::kBindingWindow;
	assert(F4T::CombatActionContract::ShouldRetryReplay(false, false, captureAt, hitAt));
	assert(F4T::CombatActionContract::ShouldRetryReplay(false, false, captureAt, captureAt + retryWindow));
	assert(!F4T::CombatActionContract::ShouldRetryReplay(true, false, captureAt, hitAt));
	assert(!F4T::CombatActionContract::ShouldRetryReplay(false, true, captureAt, hitAt));
	assert(!F4T::CombatActionContract::ShouldRetryReplay(false, false, captureAt, captureAt + retryWindow + std::chrono::milliseconds{ 1 }));
	static_assert(F4T::CombatActionContract::IsPermanentReplayFailure(true, true));
	static_assert(F4T::CombatActionContract::IsPermanentReplayFailure(true, false));
	static_assert(!F4T::CombatActionContract::IsPermanentReplayFailure(false, true));
	static_assert(F4T::CombatActionContract::IsPermanentReplayFailure(false, false));
	assert(F4T::CombatActionContract::ShouldRetryReplay(
		false,
		F4T::CombatActionContract::IsPermanentReplayFailure(false, true),
		captureAt,
		hitAt));
	assert(!F4T::CombatActionContract::ShouldRetryReplay(
		false,
		F4T::CombatActionContract::IsPermanentReplayFailure(false, false),
		captureAt,
		hitAt));
	const auto nearest = [&](Clock::time_point a_at, const std::vector<RecentAttack>& a_recent) {
		return F4T::CombatActionContract::FindNearestAttackSequence(3, a_at, a_recent);
	};

	const std::vector<RecentAttack> single{ { 7, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(nearest(hitAt, single) == 7);

	const std::vector<RecentAttack> overlapping{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 300 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(nearest(hitAt, overlapping) == 8);

	const std::vector<RecentAttack> outsideWindow{ { 7, 3, hitAt - std::chrono::milliseconds{ 2100 } } };
	assert(nearest(hitAt, outsideWindow) == 0);

	const std::vector<RecentAttack> mixedWindow{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 2100 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 1900 } } };
	assert(nearest(hitAt, mixedWindow) == 8);

	const std::vector<RecentAttack> otherType{
		{ 7, 1, hitAt - std::chrono::milliseconds{ 100 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 200 } } };
	assert(nearest(hitAt, otherType) == 8);

	const std::vector<RecentAttack> withinSlack{
		{ 7, 3, hitAt + F4T::CombatActionContract::kFutureSlack - std::chrono::milliseconds{ 1 } } };
	assert(nearest(hitAt, withinSlack) == 7);

	const std::vector<RecentAttack> beyondSlack{
		{ 7, 3, hitAt + F4T::CombatActionContract::kFutureSlack + std::chrono::milliseconds{ 1 } } };
	assert(nearest(hitAt, beyondSlack) == 0);

	const std::vector<RecentAttack> beforeDecision{ { 7, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(!F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + std::chrono::milliseconds{ 50 }));
	assert(nearest(hitAt, beforeDecision) == 7);
	const std::vector<RecentAttack> lateArrival{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 100 } },
		{ 8, 3, hitAt + std::chrono::milliseconds{ 100 } } };
	assert(F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + std::chrono::milliseconds{ 200 }));
	assert(nearest(hitAt, lateArrival) == 0);
	assert(!F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + F4T::CombatActionContract::kFutureSlack));
	assert(F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + F4T::CombatActionContract::kFutureSlack + std::chrono::milliseconds{ 1 }));
	const std::vector<RecentAttack> futureNearest{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 100 } },
		{ 8, 3, hitAt + std::chrono::milliseconds{ 50 } } };
	assert(nearest(hitAt, futureNearest) == 8);
	const std::vector<RecentAttack> rapidFire{
		{ 7, 3, hitAt },
		{ 8, 3, hitAt + std::chrono::milliseconds{ 100 } } };
	assert(nearest(hitAt + std::chrono::milliseconds{ 10 }, rapidFire) == 7);
	assert(nearest(hitAt + std::chrono::milliseconds{ 110 }, rapidFire) == 8);

	const std::vector<RecentAttack> meleeOverlap{
		{ 7, 1, hitAt - std::chrono::milliseconds{ 300 } },
		{ 8, 1, hitAt - std::chrono::milliseconds{ 100 } },
		{ 9, 3, hitAt - std::chrono::milliseconds{ 20 } } };
	assert(F4T::CombatActionContract::FindNearestAttackSequence(1, hitAt, meleeOverlap) == 8);
	assert(F4T::CombatActionContract::FindNearestAttackSequence(2, hitAt, meleeOverlap) == 0);
	const std::vector<RecentAttack> unarmedOverlap{
		{ 10, 2, hitAt - std::chrono::milliseconds{ 400 } },
		{ 11, 2, hitAt - std::chrono::milliseconds{ 150 } } };
	assert(F4T::CombatActionContract::FindNearestAttackSequence(2, hitAt, unarmedOverlap) == 11);
	assert(F4T::CombatActionContract::FindNearestAttackSequence(1, hitAt, unarmedOverlap) == 0);

	return 0;
}
