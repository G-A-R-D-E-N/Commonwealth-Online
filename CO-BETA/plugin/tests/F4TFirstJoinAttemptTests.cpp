#include "F4TFirstJoinAttempt.h"

#include <cassert>
#include <chrono>

namespace FirstJoin = F4T::FirstJoinAttempt;

int main()
{
	const FirstJoin::Scope scope{ 1, 2, 3, 4 };
	const FirstJoin::Token token{ FirstJoin::NextId(), scope, "previous.fos", 10 };
	const auto id = token.id;
	assert(FirstJoin::CallbackAccepted(token, scope, false, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, FirstJoin::Scope{ 9, 2, 3, 4 }, false, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, FirstJoin::Scope{ 1, 9, 3, 4 }, false, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, FirstJoin::Scope{ 1, 2, 9, 4 }, false, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, FirstJoin::Scope{ 1, 2, 3, 9 }, false, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, scope, true, false, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, scope, false, true, "new.fos", "character"));
	assert(!FirstJoin::CallbackAccepted(token, scope, false, false, "", "character"));
	assert(!FirstJoin::CallbackAccepted(token, scope, false, false, "new.fos", ""));
	assert(!FirstJoin::CallbackAccepted(token, scope, false, false, "previous.fos", "character"));
	assert(FirstJoin::EntryAccepted(token, FirstJoin::Entry{ id, true, 1 }));
	assert(!FirstJoin::EntryAccepted(token, FirstJoin::Entry{ id, false, 1 }));
	assert(!FirstJoin::EntryAccepted(token, FirstJoin::Entry{ id, true, 2 }));
	assert(!FirstJoin::EntryAccepted(token, FirstJoin::Entry{ id + 1, true, 1 }));
	assert(FirstJoin::EntryPending(token, FirstJoin::Entry{ id, false, 0 }));
	assert(!FirstJoin::EntryPending(token, FirstJoin::Entry{ id, true, 1 }));
	assert(!FirstJoin::EntryPending(token, FirstJoin::Entry{ id + 1, false, 0 }));
	const auto first = FirstJoin::NextId();
	const auto second = FirstJoin::NextId();
	assert(second > first);
	const auto deadline = std::chrono::steady_clock::now();
	assert(FirstJoin::SettleExpired(deadline, deadline));
	assert(!FirstJoin::SettleExpired(deadline + std::chrono::seconds{ 30 }, deadline));
	assert(FirstJoin::ShouldRecordOnboardingResume(true, true, true, "Save40_AAAA1111_Nomad_Vault111Cryo_5_20260814001212_1_2"));
	assert(!FirstJoin::ShouldRecordOnboardingResume(false, true, true, "x"));
	assert(!FirstJoin::ShouldRecordOnboardingResume(true, false, true, "x"));
	assert(!FirstJoin::ShouldRecordOnboardingResume(true, true, false, "x"));
	assert(!FirstJoin::ShouldRecordOnboardingResume(true, true, true, ""));
	assert(FirstJoin::ShouldResumeOnboarding(true, false, true));
	assert(!FirstJoin::ShouldResumeOnboarding(false, false, true));
	assert(!FirstJoin::ShouldResumeOnboarding(true, true, true));
	assert(!FirstJoin::ShouldResumeOnboarding(true, false, false));
	return 0;
}
