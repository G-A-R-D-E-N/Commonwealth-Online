#include "F4TSurvivalPolicyClient.h"

#include <cassert>
#include <chrono>
#include <future>

int main()
{
	{
		F4T::SurvivalPolicy::ClientState tracked;
		tracked.BeginTrackedLoad();
		const auto token = tracked.TakePendingLoad();
		assert(token != 0 && tracked.TakePendingLoad() == 0 && tracked.CompleteLoad(token));
		tracked.BeginTrackedLoad();
		tracked.BeginTrackedLoad();
		const auto overlap = tracked.TakePendingLoad();
		assert(overlap != 0 && !tracked.CompleteLoad(overlap) && tracked.LoadAmbiguous());
	}
	for (int attempt = 0; attempt < 8; ++attempt) {
		F4T::SurvivalPolicy::ClientState tracked;
		std::promise<void> start;
		const auto signal = start.get_future().share();
		auto begin = std::async(std::launch::async, [&] { signal.wait(); tracked.BeginTrackedLoad(); });
		auto take = std::async(std::launch::async, [&] { signal.wait(); return tracked.TakePendingLoad(); });
		start.set_value();
		begin.get();
		const auto first = take.get();
		const auto second = tracked.TakePendingLoad();
		assert((first == 0) != (second == 0));
		assert(tracked.CompleteLoad(first != 0 ? first : second));
	}
	{
		F4T::SurvivalPolicy::ClientState untracked;
		untracked.Welcome(1, false);
		assert(untracked.CompleteLoad(untracked.BeginLoad()));
		assert(untracked.Enforce(1, 1, true, [] { return true; }, [] {}));
		assert(untracked.ReadyRevision(1, 1));
		untracked.UntrackedLoad();
		assert(untracked.LoadAmbiguous() && !untracked.ReadyRevision(1, 1) && !untracked.Revision(1));
		assert(!untracked.Enforce(1, 1, true, [] { return true; }, [] { assert(false); }));
		assert(!untracked.CompleteLoad(untracked.BeginLoad()));
	}
	{
		F4T::SurvivalPolicy::ClientState failed;
		failed.Welcome(1, true);
		const auto token = failed.BeginLoad();
		assert(!failed.CompleteLoad(token + 1, false) && !failed.LoadAmbiguous());
		assert(!failed.CompleteLoad(token, false) && !failed.LoadAmbiguous());
		assert(!failed.Revision(1));
		assert(!failed.Enforce(1, 1, true, [] { return true; }, [] { assert(false); }));
		const auto retry = failed.BeginLoad();
		assert(failed.CompleteLoad(retry) && !failed.LoadAmbiguous());
		assert(failed.Enforce(1, 1, true, [] { return true; }, [] {}));
	}
	{
		F4T::SurvivalPolicy::ClientState reconnect;
		const auto token = reconnect.BeginLoad();
		reconnect.Welcome(1, true);
		reconnect.Welcome(2, false);
		assert(!reconnect.Revision(1) && !reconnect.Revision(2));
		assert(reconnect.CompleteLoad(token));
		assert(!reconnect.Revision(1) && reconnect.Revision(2) && reconnect.Required(2) == false);
	}
	{
		F4T::SurvivalPolicy::ClientState loading;
		loading.Welcome(1, false);
		assert(!loading.Revision(1));
		const auto initial = loading.BeginLoad();
		assert(loading.CompleteLoad(initial));
		assert(!loading.ReadyRevision(1, 1));
		assert(!loading.Enforce(1, 1, true, [] { return false; }, [] { assert(false); }));
		assert(loading.Enforce(1, 1, true, [] { return true; }, [&] {
			assert(loading.ReadyRevision(1, 1));
		}));
		const auto oldRevision = loading.Revision(1);
		const auto first = loading.BeginLoad();
		assert(!loading.Revision(1) && !loading.ReadyRevision(1, 1));
		loading.Update(1, true);
		assert(!loading.Revision(1));
		assert(!loading.Enforce(1, 1, true, [] { return true; }, [] { assert(false); }));
		assert(!loading.CompleteLoad(first + 1) && !loading.Revision(1));
		assert(loading.CompleteLoad(first));
		assert(loading.Revision(1) && loading.Revision(1) != oldRevision);
		assert(loading.Required(1) == true);
		assert(!loading.ReadyRevision(1, 1));
		assert(loading.Enforce(1, 1, true, [] { return true; }, [] {}));
		const auto ready = loading.ReadyRevision(1, 1);
		assert(ready && !loading.ReadyRevision(1, 2));
		assert(!loading.Enforce(1, 1, true, [] { return false; }, [] { assert(false); }));
		assert(!loading.ReadyRevision(1, 1) && loading.Revision(1) != ready);
		assert(loading.Enforce(1, 1, true, [] { return true; }, [] {}));
		assert(loading.ReadyRevision(1, 1) && loading.ReadyRevision(1, 1) != ready);
		assert(!loading.CompleteLoad(first));
		const auto second = loading.BeginLoad();
		const auto third = loading.BeginLoad();
		assert(second != third && !loading.CompleteLoad(second) && !loading.CompleteLoad(third));
		assert(!loading.Revision(1) && loading.LoadAmbiguous());
	}
	{
		F4T::SurvivalPolicy::ClientState reentrant;
		reentrant.Welcome(1, true);
		assert(reentrant.CompleteLoad(reentrant.BeginLoad()));
		bool ran = false;
		assert(!reentrant.Enforce(1, 1, true, [&] { reentrant.Update(1, false); return true; }, [&] { ran = true; }));
		assert(!ran);
		reentrant.Update(1, true);
		std::uint64_t loadToken = 0;
		assert(!reentrant.Enforce(1, 1, true, [] { return true; }, [&] { loadToken = reentrant.BeginLoad(); }));
		assert(loadToken != 0 && !reentrant.Revision(1));
		assert(reentrant.CompleteLoad(loadToken));
	}
	{
		F4T::SurvivalPolicy::ClientState invalidated;
		invalidated.Welcome(1, true);
		assert(invalidated.CompleteLoad(invalidated.BeginLoad()));
		assert(invalidated.Enforce(1, 1, true, [] { return true; }, [] {}));
		const auto stale = invalidated.ReadyRevision(1, 1);
		assert(stale);
		invalidated.Welcome(1, true);
		assert(!invalidated.ReadyRevision(1, 1));
		assert(invalidated.Revision(1) && invalidated.Revision(1) != stale);
		assert(invalidated.Enforce(1, 1, true, [] { return true; }, [] {}));
		assert(invalidated.ReadyRevision(1, 1) != stale);
		assert(!invalidated.Enforce(1, 1, true, [] { return true; }, [&] {
			invalidated.Update(1, false);
			assert(invalidated.Enforce(1, 1, true, [] { return true; }, [] {}));
			assert(invalidated.ReadyRevision(1, 1));
		}));
		assert(!invalidated.ReadyRevision(1, 1));
	}
	F4T::SurvivalPolicy::ClientState state;
	assert(state.CompleteLoad(state.BeginLoad()));
	assert(!state.Required(1));
	state.Welcome(1, true);
	assert(state.Required(1) == true);
	state.Update(1, false);
	assert(state.Required(1) == false);
	state.Update(1, std::nullopt);
	assert(!state.Required(1));
	state.Welcome(2, false);
	state.Update(1, true);
	state.Welcome(1, true);
	assert(!state.Required(1));
	assert(state.Required(2) == false);
	state.Welcome(3, std::nullopt);
	assert(!state.Required(3));
	assert(!state.Revision(3));
	int calls = 0;
	auto work = [] {};
	assert(!state.Enforce(3, 1, true, [&] { ++calls; return true; }, work));
	assert(calls == 0);
	state.Update(3, true);
	assert(state.Required(3) == true);
	const auto revision = state.Revision(3);
	assert(revision && !state.Revision(2));
	state.Update(3, true);
	assert(state.Revision(3) == revision);
	assert(!state.Enforce(3, 0, true, [&] { ++calls; return true; }, work));
	assert(!state.Enforce(2, 1, true, [&] { ++calls; return true; }, work));
	assert(!state.Enforce(3, 1, false, [&] { ++calls; return true; }, work));
	assert(!state.Enforce(3, 1, true, [&] { ++calls; return false; }, [] { assert(false); }));
	assert(state.Enforce(3, 1, true, [&] { ++calls; return true; }, [&] { assert(state.Revision(3) == revision); }));
	assert(state.Enforce(3, 2, true, [&] { ++calls; return true; }, work));
	state.Update(3, false);
	assert(state.Revision(3) && state.Revision(3) != revision);
	assert(!state.Enforce(3, 2, true, [] { return false; }, [] { assert(false); }));
	assert(state.Enforce(3, 2, true, [] { return true; }, work));
	assert(calls == 3);
	std::promise<void> entered;
	std::promise<void> release;
	auto releaseFuture = release.get_future().share();
	auto running = std::async(std::launch::async, [&] {
		return state.Enforce(3, 2, true, [] { return true; }, [&] { entered.set_value(); releaseFuture.wait(); });
	});
	entered.get_future().wait();
	auto contendedRead = std::async(std::launch::async, [&] { return state.ReadyRevision(3, 2); });
	assert(contendedRead.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
	const auto concurrentRevision = contendedRead.get();
	assert(concurrentRevision && concurrentRevision == state.ReadyRevision(3, 2));
	std::promise<void> updateStarted;
	auto update = std::async(std::launch::async, [&] { updateStarted.set_value(); state.Update(3, true); });
	updateStarted.get_future().wait();
	assert(update.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
	release.set_value();
	assert(running.get());
	update.get();
	assert(state.Required(3) == true);
	assert(!state.Required(0));
	std::promise<void> healthEntered;
	std::promise<void> healthRelease;
	auto healthReleaseFuture = healthRelease.get_future().share();
	auto health = std::async(std::launch::async, [&] {
		return state.Enforce(3, 2, true, [] { return true; }, [&] { healthEntered.set_value(); healthReleaseFuture.wait(); });
	});
	healthEntered.get_future().wait();
	std::promise<void> loadStarted;
	auto load = std::async(std::launch::async, [&] { loadStarted.set_value(); return state.BeginLoad(); });
	loadStarted.get_future().wait();
	assert(load.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
	healthRelease.set_value();
	assert(health.get());
	const auto token = load.get();
	assert(!state.Revision(3) && state.CompleteLoad(token));
	assert(!F4T::SurvivalPolicy::NeedsEnabled());
	F4T::SurvivalPolicy::g_needsEnabled = true;
	assert(F4T::SurvivalPolicy::NeedsEnabled());
	F4T::SurvivalPolicy::g_needsEnabled = false;
}
