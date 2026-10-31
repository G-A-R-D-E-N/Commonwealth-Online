#include "F4TSessionSaveGuardPolicy.h"

#include <cstdint>
#include <cstdlib>

int main()
{
	using F4T::SessionSaveGuard::DecidePlayerSave;
	using F4T::SessionSaveGuard::AcceptsSessionExitRequest;
	using F4T::SessionSaveGuard::Decision;
	using F4T::SessionSaveGuard::NextStablePollCount;
	using F4T::SessionSaveGuard::QueueReady;
	using F4T::SessionSaveGuard::kMaxQueueAttempts;
	using F4T::SessionSaveGuard::kStablePollsBeforeQueue;

	if (DecidePlayerSave(false, false) != Decision::kAllow) {
		return EXIT_FAILURE;
	}
	if (DecidePlayerSave(false, true) != Decision::kAllow) {
		return EXIT_FAILURE;
	}
	if (DecidePlayerSave(true, true) != Decision::kAllow) {
		return EXIT_FAILURE;
	}
	if (DecidePlayerSave(true, false) != Decision::kBlock) {
		return EXIT_FAILURE;
	}
	if (!AcceptsSessionExitRequest(false)) {
		return EXIT_FAILURE;
	}
	if (AcceptsSessionExitRequest(true)) {
		return EXIT_FAILURE;
	}

	std::uint32_t polls = 0;
	polls = NextStablePollCount(polls, true);
	polls = NextStablePollCount(polls, true);
	if (polls != 2 || QueueReady(polls, 0)) {
		return EXIT_FAILURE;
	}
	polls = NextStablePollCount(polls, true);
	if (polls != kStablePollsBeforeQueue || !QueueReady(polls, 0)) {
		return EXIT_FAILURE;
	}
	polls = NextStablePollCount(polls, false);
	if (polls != 0 || QueueReady(polls, 0)) {
		return EXIT_FAILURE;
	}
	polls = NextStablePollCount(polls, true);
	polls = NextStablePollCount(polls, false);
	polls = NextStablePollCount(polls, true);
	if (polls != 1 || QueueReady(polls, 0)) {
		return EXIT_FAILURE;
	}
	if (!QueueReady(kStablePollsBeforeQueue, kMaxQueueAttempts - 1)) {
		return EXIT_FAILURE;
	}
	if (QueueReady(kStablePollsBeforeQueue, kMaxQueueAttempts)) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
