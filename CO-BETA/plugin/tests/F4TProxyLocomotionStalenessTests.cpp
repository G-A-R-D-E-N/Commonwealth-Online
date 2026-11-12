#include "F4TProxyLocomotionStaleness.h"

#include <cassert>
#include <chrono>
#include <optional>

namespace
{
	using namespace std::chrono_literals;
	using F4T::ProxyLocomotionStaleness::IsLocomotionIdle;
	using F4T::ProxyLocomotionStaleness::IsLocomotionStale;
	using F4T::ProxyLocomotionStaleness::SelectMovementView;

	struct FakeRemote
	{
		bool isMoving{ false };
		bool isSprinting{ false };
		bool isSneaking{ false };
		float movementSpeed{ 0.0F };
		float animationGraphSpeed{ -1.0F };
		std::chrono::steady_clock::time_point lastReceivedLocalTime{};
	};
}

int main()
{
	const auto t0 = std::chrono::steady_clock::time_point{ std::chrono::seconds{ 100 } };

	assert(F4T::ProxyLocomotionStaleness::kStaleLocomotionAge == 500ms);

	assert(!IsLocomotionStale({}, t0 + 1h));
	assert(!IsLocomotionStale(t0 + 1s, t0));
	assert(!IsLocomotionStale(t0, t0 + 499ms));
	assert(IsLocomotionStale(t0, t0 + 500ms));
	assert(IsLocomotionStale(t0, t0 + 5s));

	assert(!IsLocomotionStale(t0, t0 + 350ms));
	assert(!IsLocomotionStale(t0, t0 + 420ms));

	FakeRemote moving{ true, true, false, 300.0F, 320.0F, t0 };

	std::optional<FakeRemote> freshStorage;
	const auto& fresh = SelectMovementView(moving, freshStorage, t0 + 499ms);
	assert(!freshStorage.has_value());
	assert(&fresh == &moving);
	assert(fresh.isMoving && fresh.isSprinting && fresh.movementSpeed == 300.0F && fresh.animationGraphSpeed == 320.0F);

	std::optional<FakeRemote> staleStorage;
	const auto& stale = SelectMovementView(moving, staleStorage, t0 + 500ms);
	assert(staleStorage.has_value());
	assert(&stale == &*staleStorage);
	assert(!stale.isMoving && !stale.isSprinting);
	assert(stale.movementSpeed == 0.0F && stale.animationGraphSpeed == -1.0F);
	assert(moving.isMoving && moving.movementSpeed == 300.0F);

	FakeRemote sneakingMover{ true, false, true, 120.0F, 130.0F, t0 };
	std::optional<FakeRemote> sneakStorage;
	const auto& sneakView = SelectMovementView(sneakingMover, sneakStorage, t0 + 2s);
	assert(!sneakView.isMoving && sneakView.isSneaking);

	FakeRemote unstamped{ true, false, false, 120.0F, 130.0F, {} };
	std::optional<FakeRemote> unstampedStorage;
	const auto& unstampedView = SelectMovementView(unstamped, unstampedStorage, t0 + 1h);
	assert(!unstampedStorage.has_value());
	assert(unstampedView.isMoving);

	FakeRemote stationary{ false, false, false, 0.0F, -1.0F, t0 };
	std::optional<FakeRemote> stationaryStorage;
	const auto& stationaryView = SelectMovementView(stationary, stationaryStorage, t0 + 10s);
	assert(!stationaryView.isMoving && stationaryView.movementSpeed == 0.0F);
	assert(!stationaryStorage.has_value());
	assert(&stationaryView == &stationary);

	FakeRemote idleZeroGraph{ false, false, false, 0.0F, 0.0F, t0 };
	std::optional<FakeRemote> idleZeroStorage;
	const auto& idleZeroView = SelectMovementView(idleZeroGraph, idleZeroStorage, t0 + 3s);
	assert(!idleZeroStorage.has_value() && &idleZeroView == &idleZeroGraph);

	assert(IsLocomotionIdle(stationary));
	assert(IsLocomotionIdle(idleZeroGraph));
	assert(!IsLocomotionIdle(moving));
	assert(!IsLocomotionIdle(sneakingMover));
	FakeRemote sprintingIdle{ false, true, false, 0.0F, -1.0F, t0 };
	assert(!IsLocomotionIdle(sprintingIdle));
	FakeRemote speedOnly{ false, false, false, 5.0F, -1.0F, t0 };
	assert(!IsLocomotionIdle(speedOnly));
	FakeRemote graphOnly{ false, false, false, 0.0F, 40.0F, t0 };
	assert(!IsLocomotionIdle(graphOnly));

	std::optional<FakeRemote> speedOnlyStorage;
	const auto& speedOnlyView = SelectMovementView(speedOnly, speedOnlyStorage, t0 + 1s);
	assert(speedOnlyStorage.has_value() && speedOnlyView.movementSpeed == 0.0F && speedOnlyView.animationGraphSpeed == -1.0F);

	return 0;
}
