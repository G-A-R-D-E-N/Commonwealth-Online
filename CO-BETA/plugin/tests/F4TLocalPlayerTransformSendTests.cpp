#include "F4TLocalPlayerTransformSend.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <vector>

namespace
{
	using F4T::LocalPlayerTransformSend::TransformPublishState;
	using Clock = std::chrono::steady_clock;
	using namespace std::chrono_literals;

	bool Publishes(bool a_bypassesThrottle, bool a_hasThrottledChange, bool a_withinSendInterval)
	{
		return F4T::LocalPlayerTransformSend::ShouldPublishTransform(
			TransformPublishState{ a_bypassesThrottle, a_hasThrottledChange, a_withinSendInterval });
	}

	std::vector<Clock::duration> SendIntervals(
		bool a_isMoving,
		Clock::duration a_horizon,
		std::chrono::microseconds a_evenFrame,
		std::chrono::microseconds a_oddFrame)
	{
		std::vector<Clock::duration> intervals;
		Clock::duration now{};
		Clock::duration lastSend{};
		for (std::size_t frame = 0; now < a_horizon; ++frame) {
			now += frame % 2 == 0 ? a_evenFrame : a_oddFrame;
			TransformPublishState state{};
			state.isMoving = a_isMoving;
			state.bypassesThrottle = now - lastSend >= 2s;
			state.withinSendInterval = F4T::LocalPlayerTransformSend::IsWithinSendInterval(now - lastSend, a_isMoving);
			if (F4T::LocalPlayerTransformSend::ShouldPublishTransform(state)) {
				intervals.push_back(now - lastSend);
				lastSend = now;
			}
		}
		return intervals;
	}

	std::size_t CountBypassPublishes(bool a_floorGated, Clock::duration a_horizon, std::chrono::microseconds a_frame, bool a_flapping)
	{
		std::size_t count = 0;
		Clock::duration now{};
		Clock::duration lastSend{ -std::chrono::seconds{ 10 } };
		while (now < a_horizon) {
			now += a_frame;
			TransformPublishState state{};
			state.bypassesThrottle = a_flapping && (!a_floorGated || F4T::LocalPlayerTransformSend::BypassFloorElapsed(now - lastSend));
			state.withinSendInterval = F4T::LocalPlayerTransformSend::IsWithinSendInterval(now - lastSend, false);
			if (F4T::LocalPlayerTransformSend::ShouldPublishTransform(state)) {
				++count;
				lastSend = now;
			}
		}
		return count;
	}

	Clock::duration LastPublishTime(Clock::duration a_horizon, std::chrono::microseconds a_frame, bool a_flapping)
	{
		Clock::duration now{};
		Clock::duration lastSend{ -std::chrono::seconds{ 10 } };
		while (now < a_horizon) {
			now += a_frame;
			TransformPublishState state{};
			state.bypassesThrottle = a_flapping && F4T::LocalPlayerTransformSend::BypassFloorElapsed(now - lastSend);
			state.withinSendInterval = F4T::LocalPlayerTransformSend::IsWithinSendInterval(now - lastSend, false);
			if (F4T::LocalPlayerTransformSend::ShouldPublishTransform(state)) {
				lastSend = now;
			}
		}
		return lastSend;
	}
}

int main()
{
	assert(F4T::LocalPlayerTransformSend::kTransformSendInterval == std::chrono::milliseconds{ 50 });

	assert(!Publishes(false, false, false));
	assert(!Publishes(false, false, true));
	assert(Publishes(false, true, false));
	assert(!Publishes(false, true, true));
	assert(Publishes(true, false, false));
	assert(Publishes(true, false, true));
	assert(Publishes(true, true, true));

	assert(F4T::LocalPlayerTransformSend::IsWithinSendInterval(49ms, false));
	assert(!F4T::LocalPlayerTransformSend::IsWithinSendInterval(50ms, false));
	assert(F4T::LocalPlayerTransformSend::IsWithinSendInterval(41ms, true));
	assert(!F4T::LocalPlayerTransformSend::IsWithinSendInterval(42ms, true));

	{
		const auto intervals = SendIntervals(true, 2s, 16000us, 17000us);
		assert(intervals.size() >= 38);
		for (const auto interval : intervals) {
			assert(interval >= 42ms && interval <= 58ms);
		}
	}

	{
		const auto intervals = SendIntervals(false, 4100ms, 16000us, 16000us);
		assert(intervals.size() == 2);
		assert(intervals[0] == 2s && intervals[1] == 2s);
	}

	{
		TransformPublishState moving{};
		moving.isMoving = true;
		moving.withinSendInterval = true;
		assert(!F4T::LocalPlayerTransformSend::ShouldPublishTransform(moving));

		TransformPublishState start = moving;
		start.movingEdge = true;
		start.hasThrottledChange = true;
		assert(F4T::LocalPlayerTransformSend::ShouldPublishTransform(start));

		TransformPublishState stop{};
		stop.movingEdge = true;
		stop.hasThrottledChange = true;
		stop.withinSendInterval = true;
		assert(F4T::LocalPlayerTransformSend::ShouldPublishTransform(stop));
	}

	assert(F4T::LocalPlayerTransformSend::kBypassSendFloor == 33ms);
	assert(!F4T::LocalPlayerTransformSend::BypassFloorElapsed(32ms));
	assert(F4T::LocalPlayerTransformSend::BypassFloorElapsed(33ms));

	{
		const auto ungated = CountBypassPublishes(false, 1s, 6944us, true);
		assert(ungated >= 120);
		const auto gated = CountBypassPublishes(true, 1s, 6944us, true);
		assert(gated >= 25 && gated <= 30);
		assert(CountBypassPublishes(true, 1s, 6944us, false) == 0);
	}

	{
		const auto lastSend = LastPublishTime(1s, 6944us, true);
		auto now = Clock::duration{ 1s };
		while (!F4T::LocalPlayerTransformSend::BypassFloorElapsed(now - lastSend)) {
			now += 6944us;
		}
		assert(now - Clock::duration{ 1s } <= 40ms);
	}

	return 0;
}
