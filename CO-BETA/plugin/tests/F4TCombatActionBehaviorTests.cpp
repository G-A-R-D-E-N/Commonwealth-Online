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
	const auto find = [&](std::uint32_t a_type, const std::vector<RecentAttack>& a_recent) {
		return F4T::CombatActionContract::FindUniqueAttackSequence(a_type, hitAt, a_recent);
	};

	const std::vector<RecentAttack> single{ { 7, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	const auto one = find(3, single);
	assert(one.sequence == 7);
	assert(one.candidates == 1);

	const std::vector<RecentAttack> overlapping{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 300 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	const auto two = find(3, overlapping);
	assert(two.sequence == 0);
	assert(two.candidates == 2);

	const std::vector<RecentAttack> outsideWindow{ { 7, 3, hitAt - std::chrono::milliseconds{ 2100 } } };
	const auto none = find(3, outsideWindow);
	assert(none.sequence == 0);
	assert(none.candidates == 0);

	const std::vector<RecentAttack> mixedWindow{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 2100 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 1900 } } };
	const auto windowed = find(3, mixedWindow);
	assert(windowed.sequence == 8);
	assert(windowed.candidates == 1);

	const std::vector<RecentAttack> otherType{
		{ 7, 1, hitAt - std::chrono::milliseconds{ 100 } },
		{ 8, 3, hitAt - std::chrono::milliseconds{ 200 } } };
	const auto typed = find(3, otherType);
	assert(typed.sequence == 8);
	assert(typed.candidates == 1);

	const std::vector<RecentAttack> withinSlack{
		{ 7, 3, hitAt + F4T::CombatActionContract::kFutureSlack - std::chrono::milliseconds{ 1 } } };
	assert(find(3, withinSlack).sequence == 7);

	const std::vector<RecentAttack> beyondSlack{
		{ 7, 3, hitAt + F4T::CombatActionContract::kFutureSlack + std::chrono::milliseconds{ 1 } } };
	assert(find(3, beyondSlack).sequence == 0);
	assert(find(3, beyondSlack).candidates == 0);

	const std::vector<RecentAttack> beforeDecision{ { 7, 3, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(!F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + std::chrono::milliseconds{ 50 }));
	assert(find(3, beforeDecision).sequence == 7);
	const std::vector<RecentAttack> lateArrival{
		{ 7, 3, hitAt - std::chrono::milliseconds{ 100 } },
		{ 8, 3, hitAt + std::chrono::milliseconds{ 100 } } };
	assert(F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + std::chrono::milliseconds{ 200 }));
	const auto ambiguousAfterDecision = find(3, lateArrival);
	assert(ambiguousAfterDecision.sequence == 0);
	assert(ambiguousAfterDecision.candidates == 2);
	assert(!F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + F4T::CombatActionContract::kFutureSlack));
	assert(F4T::CombatActionContract::IsHitDecidable(hitAt, hitAt + F4T::CombatActionContract::kFutureSlack + std::chrono::milliseconds{ 1 }));

	using Launch = F4T::CombatActionContract::ProjectileLaunch;
	const auto findExact = [&](std::uint32_t a_handle, std::uint32_t a_type, const std::vector<Launch>& a_launches) {
		return F4T::CombatActionContract::FindExactAttackSequence(a_handle, a_type, hitAt, a_launches);
	};

	const std::vector<Launch> oneLaunch{ { 21, 3, 0x1234, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(findExact(0x1234, 3, oneLaunch) == 21);
	assert(findExact(0x5678, 3, oneLaunch) == 0);
	assert(findExact(0x1234, 1, oneLaunch) == 0);

	const std::vector<Launch> zeroHandle{ { 21, 3, 0, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(findExact(0, 3, zeroHandle) == 0);

	const std::vector<Launch> duplicateHandle{
		{ 21, 3, 0x1234, hitAt - std::chrono::milliseconds{ 300 } },
		{ 22, 3, 0x1234, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(findExact(0x1234, 3, duplicateHandle) == 0);

	const std::vector<Launch> staleDuplicate{
		{ 21, 3, 0x1234, hitAt - std::chrono::milliseconds{ 2100 } },
		{ 22, 3, 0x1234, hitAt - std::chrono::milliseconds{ 100 } } };
	assert(findExact(0x1234, 3, staleDuplicate) == 22);

	const std::vector<Launch> staleLaunch{ { 21, 3, 0x1234, hitAt - std::chrono::milliseconds{ 2100 } } };
	assert(findExact(0x1234, 3, staleLaunch) == 0);

	return 0;
}
