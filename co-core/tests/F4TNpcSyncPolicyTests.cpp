#include "F4TNpcSyncPolicy.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

int main()
{
	using namespace F4T::NpcSyncPolicy;
	using namespace std::chrono_literals;

	assert(RouteAuthority(false, false, 1, 0) == HitRoute::kRetry);
	assert(RouteAuthority(false, true, 1, 1) == HitRoute::kRetry);
	assert(RouteAuthority(true, true, 2, 1) == HitRoute::kRetry);
	assert(RouteAuthority(true, false, 2, 1) == HitRoute::kRetry);
	assert(RouteAuthority(true, true, 1, 2) == HitRoute::kDrop);
	assert(RouteAuthority(true, false, 3, 3) == HitRoute::kRetry);
	assert(RouteAuthority(true, true, 2, 3) == HitRoute::kDrop);
	assert(RouteAuthority(true, true, 3, 3) == HitRoute::kApply);

	assert(KeepPendingHit(HitRoute::kRetry, 0ms, 5000ms));
	assert(KeepPendingHit(HitRoute::kRetry, 5000ms, 5000ms));
	assert(!KeepPendingHit(HitRoute::kRetry, 5001ms, 5000ms));
	assert(!KeepPendingHit(HitRoute::kDrop, 0ms, 5000ms));
	assert(!KeepPendingHit(HitRoute::kApply, 0ms, 5000ms));

	assert(DamageAboveFloor(50.0F, 10.0F, 0.0F) == 10.0F);
	assert(DamageAboveFloor(5.0F, 10.0F, 1.0F) == 4.0F);
	assert(DamageAboveFloor(1.0F, 10.0F, 1.0F) == 0.0F);
	assert(DamageAboveFloor(0.5F, 10.0F, 1.0F) == 0.0F);
	assert(DamageAboveFloor(50.0F, -3.0F, 0.0F) == 0.0F);
	assert(DamageAboveFloor(50.0F, 0.0F, 0.0F) == 0.0F);
	assert(DamageAboveFloor(50.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F) == 0.0F);
	assert(DamageAboveFloor(50.0F, std::numeric_limits<float>::infinity(), 0.0F) == 0.0F);
	assert(DamageAboveFloor(std::numeric_limits<float>::quiet_NaN(), 10.0F, 0.0F) == 0.0F);

	assert(DeathRetryDelay(0) == 250ms);
	assert(DeathRetryDelay(1) == 500ms);
	assert(DeathRetryDelay(2) == 1000ms);
	assert(DeathRetryDelay(3) == 2000ms);
	assert(DeathRetryDelay(4) == 4000ms);
	assert(DeathRetryDelay(5) == 4000ms);
	assert(DeathRetryDelay(255) == 4000ms);

	assert(IsDeathRetryDue(250ms, 0));
	assert(!IsDeathRetryDue(249ms, 0));
	assert(!IsDeathRetryDue(499ms, 1));
	assert(IsDeathRetryDue(500ms, 1));
	assert(!IsDeathRetryDue(3999ms, 4));
	assert(IsDeathRetryDue(4000ms, 4));
	assert(IsDeathRetryDue(60000ms, 200));

	assert(AcceptRemoteNpcState(3, std::nullopt, true));
	assert(AcceptRemoteNpcState(3, 5U, true));
	assert(AcceptRemoteNpcState(5, 5U, true));
	assert(AcceptRemoteNpcState(7, 5U, false));
	assert(!AcceptRemoteNpcState(7, 5U, true));

	std::uint8_t attempts = 0;
	std::chrono::milliseconds elapsed{ 0 };
	std::uint32_t dueCount = 0;
	for (std::uint32_t tick = 0; tick < 2000; ++tick) {
		elapsed += 250ms;
		if (IsDeathRetryDue(elapsed, attempts)) {
			++dueCount;
			elapsed = 0ms;
			if (attempts < kMaximumDeathBackoffStep) {
				++attempts;
			}
		}
	}
	assert(attempts == kMaximumDeathBackoffStep);
	assert(dueCount > 5);
	assert(elapsed < 4000ms);

	const std::optional<std::chrono::milliseconds> kNotMissing{};
	const std::optional<std::uint32_t> kNotLoaded{};
	assert(ResolveHostNpcIdRecord(7U, kNotMissing, std::optional<std::uint32_t>{ 7U }, 0ms, 5s).step == HostNpcIdStep::kKeep);
	assert(ResolveHostNpcIdRecord(7U, kNotMissing, std::optional<std::uint32_t>{ 0U }, 0ms, 5s).step == HostNpcIdStep::kKeep);
	assert(ResolveHostNpcIdRecord(7U, kNotMissing, std::optional<std::uint32_t>{ 9U }, 0ms, 5s).step == HostNpcIdStep::kReissue);
	const auto firstMiss = ResolveHostNpcIdRecord(7U, kNotMissing, kNotLoaded, 1000ms, 5s);
	assert(firstMiss.step == HostNpcIdStep::kKeep && firstMiss.missingSince == 1000ms);
	assert(ResolveHostNpcIdRecord(7U, firstMiss.missingSince, kNotLoaded, 5999ms, 5s).step == HostNpcIdStep::kKeep);
	assert(ResolveHostNpcIdRecord(7U, firstMiss.missingSince, kNotLoaded, 6000ms, 5s).step == HostNpcIdStep::kForget);
	const auto reloaded = ResolveHostNpcIdRecord(7U, firstMiss.missingSince, std::optional<std::uint32_t>{ 7U }, 3000ms, 5s);
	assert(reloaded.step == HostNpcIdStep::kKeep && !reloaded.missingSince);
	const auto secondMiss = ResolveHostNpcIdRecord(7U, reloaded.missingSince, kNotLoaded, 7999ms, 5s);
	assert(secondMiss.missingSince == 7999ms);
	assert(ResolveHostNpcIdRecord(7U, secondMiss.missingSince, kNotLoaded, 12998ms, 5s).step == HostNpcIdStep::kKeep);
	assert(ResolveHostNpcIdRecord(7U, secondMiss.missingSince, kNotLoaded, 12999ms, 5s).step == HostNpcIdStep::kForget);
	const auto reissued = ResolveHostNpcIdRecord(7U, std::optional<std::chrono::milliseconds>{ 1000ms }, std::optional<std::uint32_t>{ 9U }, 2000ms, 5s);
	assert(reissued.step == HostNpcIdStep::kReissue && !reissued.missingSince);

	const auto candidate = [](std::uint32_t a_actorFormId, float a_distance, bool a_dead = false) {
		return RuntimeClaimCandidate{ a_actorFormId, a_distance, a_dead };
	};
	std::map<int, std::uint32_t> claims;
	const std::vector<RuntimeClaimCandidate> single{ candidate(10U, 100.0F) };
	assert(ResolveRuntimeClaim(claims, 1, single, 1024.0F) == 10U);
	assert(!ResolveRuntimeClaim(claims, 2, single, 1024.0F));
	assert(ResolveRuntimeClaim(claims, 1, single, 1024.0F) == 10U);
	claims.erase(1);
	assert(ResolveRuntimeClaim(claims, 2, single, 1024.0F) == 10U);
	assert(!ResolveRuntimeClaim(claims, 1, single, 1024.0F));

	std::map<int, std::uint32_t> kept;
	assert(ResolveRuntimeClaim(kept, 1, { candidate(10U, 500.0F) }, 1024.0F) == 10U);
	assert(ResolveRuntimeClaim(kept, 1, { candidate(10U, 500.0F), candidate(11U, 10.0F) }, 1024.0F) == 10U);
	assert(ResolveRuntimeClaim(kept, 1, { candidate(11U, 10.0F) }, 1024.0F) == 11U);
	assert(kept.at(1) == 11U);

	std::map<int, std::uint32_t> far;
	assert(!ResolveRuntimeClaim(far, 1, { candidate(10U, 1024.0F) }, 1024.0F));
	assert(far.empty());

	std::map<int, std::uint32_t> pick;
	assert(ResolveRuntimeClaim(pick, 1, { candidate(10U, 5.0F, true), candidate(11U, 900.0F), candidate(12U, 300.0F) }, 1024.0F) == 12U);
	assert(ResolveRuntimeClaim(pick, 2, { candidate(12U, 300.0F), candidate(11U, 900.0F) }, 1024.0F) == 11U);
	assert(ResolveRuntimeClaim(pick, 2, { candidate(10U, 5.0F, true) }, 1024.0F) == 10U);

	std::map<int, std::uint32_t> beyond{ { 1, 10U } };
	assert(!ResolveRuntimeClaim(beyond, 1, { candidate(10U, 1500.0F) }, 1024.0F));
	assert(beyond.empty());

	std::map<int, std::uint32_t> taken{ { 1, 5U }, { 2, 5U } };
	assert(ResolveRuntimeClaim(taken, 2, { candidate(5U, 10.0F), candidate(6U, 20.0F) }, 1024.0F) == 6U);
	assert(taken.at(2) == 6U);

	std::map<int, std::uint32_t> deadFirst;
	assert(ResolveRuntimeClaim(deadFirst, 1, { candidate(5U, 10.0F), RuntimeClaimCandidate{ 6U, 500.0F, true } }, 1024.0F, true) == 6U);
	std::map<int, std::uint32_t> deadOnlyLiving;
	assert(ResolveRuntimeClaim(deadOnlyLiving, 1, { candidate(5U, 10.0F) }, 1024.0F, true) == 5U);
	std::map<int, std::uint32_t> livingFirst;
	assert(ResolveRuntimeClaim(livingFirst, 1, { RuntimeClaimCandidate{ 6U, 10.0F, true }, candidate(5U, 500.0F) }, 1024.0F, false) == 5U);

	const RuntimeCopy hostA{ 7U, 0.0F, 0.0F, 0.0F, 2U, false };
	const RuntimeCopy hostB{ 7U, 500.0F, 0.0F, 0.0F, 3U, false };
	assert(IsRuntimeDuplicate(hostA, hostB, kRuntimeClaimDistance));
	assert(!IsRuntimeDuplicate(hostA, RuntimeCopy{ 7U, 1024.0F, 0.0F, 0.0F, 3U, false }, kRuntimeClaimDistance));
	assert(!IsRuntimeDuplicate(hostA, RuntimeCopy{ 8U, 10.0F, 0.0F, 0.0F, 3U, false }, kRuntimeClaimDistance));
	assert(!IsRuntimeDuplicate(hostA, RuntimeCopy{ 7U, 10.0F, 0.0F, 0.0F, 2U, false }, kRuntimeClaimDistance));
	assert(!IsRuntimeDuplicate(hostA, RuntimeCopy{ 7U, std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 3U, false }, kRuntimeClaimDistance));
	using Losers = std::vector<std::size_t>;
	const RuntimeCopy h1r1{ 7U, 0.0F, 0.0F, 0.0F, 1U, false };
	const RuntimeCopy h1r2{ 7U, 100.0F, 0.0F, 0.0F, 1U, false };
	const RuntimeCopy h1r3{ 7U, 200.0F, 0.0F, 0.0F, 1U, false };
	const RuntimeCopy h2r1{ 7U, 10.0F, 0.0F, 0.0F, 2U, true };
	const RuntimeCopy h2r2{ 7U, 110.0F, 0.0F, 0.0F, 2U, true };
	assert((FindRuntimeDuplicateLosers({ h1r1, h1r2, h1r3, h2r1, h2r2 }, kRuntimeClaimDistance) == Losers{ 0, 1 }));
	assert((FindRuntimeDuplicateLosers({ h2r2, h1r3, h2r1, h1r2, h1r1 }, kRuntimeClaimDistance) == Losers{ 3, 4 }));
	const RuntimeCopy chainA{ 7U, 0.0F, 0.0F, 0.0F, 1U, false };
	const RuntimeCopy chainB{ 7U, 900.0F, 0.0F, 0.0F, 2U, false };
	const RuntimeCopy chainC{ 7U, 1800.0F, 0.0F, 0.0F, 3U, false };
	assert((FindRuntimeDuplicateLosers({ chainA, chainB, chainC }, kRuntimeClaimDistance) == Losers{ 1 }));
	assert((FindRuntimeDuplicateLosers({ chainC, chainB, chainA }, kRuntimeClaimDistance) == Losers{ 1 }));
	assert((FindRuntimeDuplicateLosers({ chainB, chainA, chainC }, kRuntimeClaimDistance) == Losers{ 0 }));
	assert(FindRuntimeDuplicateLosers({}, kRuntimeClaimDistance).empty());
	assert(FindRuntimeDuplicateLosers({ chainA }, kRuntimeClaimDistance).empty());
	assert(PreferRuntimeCopy(hostA, hostB) && !PreferRuntimeCopy(hostB, hostA));
	const RuntimeCopy localHigh{ 7U, 0.0F, 0.0F, 0.0F, 9U, true };
	assert(PreferRuntimeCopy(localHigh, hostA) && !PreferRuntimeCopy(hostA, localHigh));

	assert(HostRank(false, true, 0ms) == 0 && HostRank(true, false, 0ms) == 1 && HostRank(false, false, 0ms) == 2);
	assert(HostRank(true, false, kFreshDeathWindow - 1ms) == 1 && HostRank(true, false, kFreshDeathWindow) == 3);
	assert(HostRank(true, true, kFreshDeathWindow) == 3);

	assert(kPuppetRedrawInterval == 1000ms && kMaximumPuppetRedraws == 5);
	const auto first = DecidePuppetRedraw(true, 0ms, 0);
	assert(first.redraw && first.firstRedraw);
	assert(!DecidePuppetRedraw(false, 999ms, 1).redraw);
	const auto second = DecidePuppetRedraw(false, 1000ms, 1);
	assert(second.redraw && !second.firstRedraw);
	assert(DecidePuppetRedraw(false, 1000ms, kMaximumPuppetRedraws - 1).redraw);
	assert(!DecidePuppetRedraw(false, 60000ms, kMaximumPuppetRedraws).redraw);
	assert(!DecidePuppetRedraw(true, 0ms, kMaximumPuppetRedraws).redraw);

	assert(!IsRedrawEpisodeSettled(true, 60000ms));
	assert(!IsRedrawEpisodeSettled(false, 4999ms));
	assert(IsRedrawEpisodeSettled(false, 5000ms));

	std::chrono::milliseconds lastRedraw{ 0 };
	std::chrono::milliseconds lastNeeded{ 0 };
	std::chrono::milliseconds resetAt{ -1 };
	bool never = true;
	bool neverNeeded = true;
	std::uint8_t count = 0;
	std::uint32_t flipRedraws = 0;
	std::chrono::milliseconds previous{ -1 };
	for (std::chrono::milliseconds now{ 0 }; now < 30000ms; now += 100ms) {
		const bool needed = (now.count() / 100) % 2 == 0 && now < 20000ms;
		if (!needed) {
			if (IsRedrawEpisodeSettled(neverNeeded, now - lastNeeded)) {
				never = true;
				neverNeeded = true;
				count = 0;
				resetAt = now;
			}
			continue;
		}
		lastNeeded = now;
		neverNeeded = false;
		if (DecidePuppetRedraw(never, now - lastRedraw, count).redraw) {
			assert(previous.count() < 0 || now - previous >= kPuppetRedrawInterval);
			previous = now;
			lastRedraw = now;
			never = false;
			++count;
			++flipRedraws;
		}
	}
	assert(flipRedraws == kMaximumPuppetRedraws);
	assert(never && count == 0 && resetAt == 24800ms);

	assert(IsOrphanAdoptable(false, false, false));
	assert(!IsOrphanAdoptable(false, true, false));
	assert(!IsOrphanAdoptable(false, false, true));
	assert(IsOrphanAdoptable(true, true, true));

	assert(IsRuntimeAdoptScopeCompatible("A", "A", "", ""));
	assert(IsRuntimeAdoptScopeCompatible("A", "B", "W", "W"));
	assert(!IsRuntimeAdoptScopeCompatible("A", "B", "", ""));
	assert(!IsRuntimeAdoptScopeCompatible("A", "B", "W1", "W2"));

	const auto nearest = AssignNearestOrphans({ { 0, 0, 300.0F }, { 0, 1, 100.0F }, { 1, 1, 50.0F }, { 1, 0, 400.0F } });
	assert(nearest.size() == 2 && nearest[0].wanted == 1 && nearest[0].orphan == 1 && nearest[1].wanted == 0 && nearest[1].orphan == 0);
	const auto ties = AssignNearestOrphans({ { 1, 0, 10.0F }, { 0, 1, 10.0F }, { 1, 1, 10.0F }, { 0, 0, 10.0F } });
	assert(ties.size() == 2 && ties[0].wanted == 0 && ties[0].orphan == 0 && ties[1].wanted == 1 && ties[1].orphan == 1);
	const auto finite = AssignNearestOrphans({ { 0, 0, std::numeric_limits<float>::quiet_NaN() }, { 0, 1, std::numeric_limits<float>::infinity() }, { 1, 0, 5.0F } });
	assert(finite.size() == 1 && finite[0].wanted == 1);
	assert(AssignNearestOrphans({}).empty());

	struct FakeSlot
	{
		bool deathApplied{ true };
		std::uint8_t deathAttempts{ 3 };
		std::chrono::steady_clock::time_point lastDeathAttempt{ std::chrono::steady_clock::now() };
		std::chrono::steady_clock::time_point lastRedraw{ std::chrono::steady_clock::now() };
		std::chrono::steady_clock::time_point lastNeeded{ std::chrono::steady_clock::now() };
		std::uint8_t redrawCount{ 4 };
	};
	FakeSlot dead;
	ResetAdoptedSlot(dead, true);
	assert(dead.deathApplied && dead.deathAttempts == 3 && dead.redrawCount == 4);
	FakeSlot alive;
	ResetAdoptedSlot(alive, false);
	assert(!alive.deathApplied && alive.deathAttempts == 0 && alive.redrawCount == 0);
	assert(alive.lastDeathAttempt == std::chrono::steady_clock::time_point{} && alive.lastRedraw == std::chrono::steady_clock::time_point{} && alive.lastNeeded == std::chrono::steady_clock::time_point{});
	return 0;
}
