#include "F4TRangedShotQueue.h"

#include "F4TActionSequence.h"

#include <algorithm>

namespace F4T::RangedShotQueue
{
	std::uint64_t Queue::Push(Shot a_shot)
	{
		const std::scoped_lock lock(_mutex);
		_shots.push_back(a_shot);
		if (_shots.size() <= kCapacity) return 0;
		_shots.pop_front();
		return ++_evictions;
	}

	std::vector<Shot> Queue::Drain(std::uint64_t a_generation, std::size_t a_limit,
		std::chrono::steady_clock::time_point a_now, std::chrono::steady_clock::duration a_window,
		std::uint32_t& a_nextSequence)
	{
		const std::scoped_lock lock(_mutex);
		std::vector<Shot> result;
		result.reserve((std::min)(a_limit, _shots.size()));
		while (!_shots.empty() && result.size() < a_limit) {
			auto shot = _shots.front();
			_shots.pop_front();
			if (a_generation == 0 || shot.generation != a_generation || a_now > shot.capturedAt + a_window) continue;
			if (shot.sequence == 0) shot.sequence = F4T::ActionSequence::AllocateNonZero(a_nextSequence);
			result.push_back(shot);
		}
		return result;
	}

	std::uint64_t Queue::Retry(std::uint64_t a_generation, std::span<const Shot> a_shots)
	{
		const std::scoped_lock lock(_mutex);
		for (auto it = a_shots.rbegin(); it != a_shots.rend(); ++it) {
			if (a_generation != 0 && it->generation == a_generation) _shots.push_front(*it);
		}
		std::uint64_t evictions{};
		while (_shots.size() > kCapacity) {
			_shots.pop_back();
			evictions = ++_evictions;
		}
		return evictions;
	}

	void Queue::Clear()
	{
		const std::scoped_lock lock(_mutex);
		_shots.clear();
	}
}
