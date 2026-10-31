#include "F4TShotReplay.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace
{
	using Json = nlohmann::json;

	bool NearlyEqual(double a_left, double a_right)
	{
		return std::abs(a_left - a_right) < 1e-12;
	}

	Json Aim(double a_x, double a_y, double a_z)
	{
		return Json{ { "direction", Json{ { "x", a_x }, { "y", a_y }, { "z", a_z } } } };
	}
}

int main()
{
	using namespace F4T::ShotReplay;

	const auto unit = NormalizeDirection(Direction{ 3.0, 0.0, 4.0 });
	assert(unit);
	assert(NearlyEqual(unit->x, 0.6));
	assert(NearlyEqual(unit->z, 0.8));
	assert(!NormalizeDirection(Direction{}));
	assert(!NormalizeDirection(Direction{ 1e-9, 0.0, 0.0 }));
	assert(!NormalizeDirection(Direction{ std::numeric_limits<double>::infinity(), 0.0, 0.0 }));
	assert(!NormalizeDirection(Direction{ std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }));

	const auto forward = ParseProjectileAim(Aim(0.0, 1.0, 0.0));
	assert(forward && NearlyEqual(forward->y, 1.0));
	assert(!ParseProjectileAim(Aim(0.0, 2.0, 0.0)));
	assert(!ParseProjectileAim(Aim(0.0, 0.5, 0.0)));
	assert(!ParseProjectileAim(Aim(std::numeric_limits<double>::quiet_NaN(), 1.0, 0.0)));
	assert(!ParseProjectileAim(Json{ { "direction", Json{ { "x", 0.0 }, { "y", 1.0 } } } }));
	assert(!ParseProjectileAim(Json{ { "direction", Json{ { "x", "0" }, { "y", 1.0 }, { "z", 0.0 } } } }));
	assert(!ParseProjectileAim(Json{ { "direction", 1.0 } }));
	assert(!ParseProjectileAim(Json::array()));

	assert(IsReplayableAnimEvent("attackStartAuto"));
	assert(IsReplayableAnimEvent("reloadStart"));
	assert(IsReplayableAnimEvent("rifleSighted"));
	assert(IsReplayableAnimEvent("Weap_Draw"));
	assert(IsReplayableAnimEvent("powerAttack"));
	assert(!IsReplayableAnimEvent("attackStart"));
	assert(!IsReplayableAnimEvent("AttackStart"));
	assert(!IsReplayableAnimEvent("meleeattackStart"));
	assert(!IsReplayableAnimEvent("Death"));
	assert(!IsReplayableAnimEvent(""));
	assert(!IsReplayableAnimEvent("attack-start"));
	assert(!IsReplayableAnimEvent("attack start"));
	assert(IsReplayableAnimEvent("attack" + std::string(58, 'x')));
	assert(!IsReplayableAnimEvent("attack" + std::string(59, 'x')));

	assert(IsRetryableAnimEvent("attackStartAuto"));
	assert(IsRetryableAnimEvent("ReloadStart"));
	assert(IsRetryableAnimEvent("rifleSightedStart"));
	assert(!IsRetryableAnimEvent("rifleSighted"));
	assert(!IsRetryableAnimEvent("reload"));
	assert(!IsRetryableAnimEvent("Weap_Draw"));

	const auto event = ParsePlayerAnimEvent(Json{ { "event", "reloadStart" } });
	assert(event && *event == "reloadStart");
	assert(!ParsePlayerAnimEvent(Json{ { "event", "attackStart" } }));
	assert(!ParsePlayerAnimEvent(Json{ { "event", 5 } }));
	assert(!ParsePlayerAnimEvent(Json{ { "other", "reloadStart" } }));

	assert(ParseRelayedPlayerId(Json{ { "playerId", 7 } }) == 7U);
	assert(ParseRelayedPlayerId(Json{ { "playerId", 4294967295ULL } }) == 4294967295U);
	assert(!ParseRelayedPlayerId(Json{ { "playerId", 0 } }));
	assert(!ParseRelayedPlayerId(Json{ { "playerId", -1 } }));
	assert(!ParseRelayedPlayerId(Json{ { "playerId", 4294967296ULL } }));
	assert(!ParseRelayedPlayerId(Json{ { "playerId", "7" } }));

	const double kHalfPi = std::numbers::pi / 2.0;
	const auto level = DirectionFromLaunchAngles(0.0, 0.0);
	assert(NearlyEqual(level.x, 0.0) && NearlyEqual(level.y, 1.0) && NearlyEqual(level.z, 0.0));
	const auto east = DirectionFromLaunchAngles(kHalfPi, 0.0);
	assert(NearlyEqual(east.x, 1.0) && NearlyEqual(east.y, 0.0) && NearlyEqual(east.z, 0.0));
	const auto down = DirectionFromLaunchAngles(0.0, 0.5);
	assert(down.z < 0.0);
	assert(NearlyEqual(down.x, 0.0) && NearlyEqual(down.y, std::cos(0.5)));
	const auto up = DirectionFromLaunchAngles(0.0, -0.5);
	assert(up.z > 0.0);
	for (const auto angles : { std::pair{ 1.0, 0.3 }, std::pair{ -2.0, -0.7 }, std::pair{ 0.0, kHalfPi } }) {
		const auto dir = DirectionFromLaunchAngles(angles.first, angles.second);
		const auto length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		assert(NearlyEqual(length, 1.0));
	}

	{
		RemoteAimQueue queue;
		const auto start = std::chrono::steady_clock::now();
		queue.Push(7U, Direction{ 0.0, 1.0, 0.0 }, start);
		queue.Push(7U, Direction{ 1.0, 0.0, 0.0 }, start + std::chrono::milliseconds{ 10 });
		queue.Push(8U, Direction{ 0.0, 0.0, 1.0 }, start);
		const auto first = queue.Take(7U, start + std::chrono::milliseconds{ 20 });
		assert(first && NearlyEqual(first->y, 1.0));
		const auto second = queue.Take(7U, start + std::chrono::milliseconds{ 20 });
		assert(second && NearlyEqual(second->x, 1.0));
		assert(!queue.Take(7U, start + std::chrono::milliseconds{ 20 }));
		const auto other = queue.Take(8U, start + std::chrono::milliseconds{ 20 });
		assert(other && NearlyEqual(other->z, 1.0));
		assert(!queue.Take(9U, start + std::chrono::milliseconds{ 20 }));
	}
	{
		RemoteAimQueue queue;
		const auto start = std::chrono::steady_clock::now();
		queue.Push(1U, Direction{ 0.0, 1.0, 0.0 }, start);
		assert(!queue.Take(1U, start + kRemoteAimExpiry));
		queue.Push(1U, Direction{ 0.0, 1.0, 0.0 }, start);
		assert(queue.Take(1U, start + kRemoteAimExpiry - std::chrono::milliseconds{ 1 }));
	}
	{
		RemoteAimQueue queue;
		const auto start = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i <= kRemoteAimPerPlayerCap; ++i) {
			queue.Push(3U, Direction{ static_cast<double>(i), 1.0, 0.0 }, start + std::chrono::milliseconds{ static_cast<std::chrono::milliseconds::rep>(i) });
		}
		const auto evicted = queue.Take(3U, start + std::chrono::milliseconds{ 100 });
		assert(evicted && NearlyEqual(evicted->x, 1.0));
		for (std::size_t i = 0; i < kRemoteAimPerPlayerCap - 1; ++i) {
			assert(queue.Take(3U, start + std::chrono::milliseconds{ 100 }));
		}
		assert(!queue.Take(3U, start + std::chrono::milliseconds{ 100 }));
	}
	{
		RemoteAimQueue queue;
		const auto start = std::chrono::steady_clock::now();
		for (std::size_t i = 1; i <= kRemoteAimPlayerCap + 1; ++i) {
			queue.Push(static_cast<std::uint32_t>(i), Direction{ 0.0, 1.0, 0.0 }, start);
		}
		assert(!queue.Take(static_cast<std::uint32_t>(kRemoteAimPlayerCap + 1), start));
		assert(queue.Take(1U, start));
	}
	{
		RemoteAimQueue queue;
		const auto start = std::chrono::steady_clock::now();
		for (std::size_t i = 1; i <= kRemoteAimPlayerCap; ++i) {
			queue.Push(static_cast<std::uint32_t>(i), Direction{ 0.0, 1.0, 0.0 }, start);
		}
		const auto late = start + std::chrono::milliseconds{ 600 };
		queue.Push(static_cast<std::uint32_t>(kRemoteAimPlayerCap + 1), Direction{ 0.0, 1.0, 0.0 }, late);
		assert(queue.Take(static_cast<std::uint32_t>(kRemoteAimPlayerCap + 1), late));
	}

	return 0;
}
