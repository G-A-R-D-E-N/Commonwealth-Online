#include "F4TCombatTicketStore.h"

#include <atomic>
#include <limits>

namespace F4T::CombatTicketStore
{
	namespace
	{
		std::atomic<std::uint64_t> s_lastShot{};
	}

	thread_local ShotScope* ShotScope::_current{};

	ShotScope::ShotScope(std::uint64_t a_generation) noexcept :
		_previous(_current),
		_generation(a_generation)
	{
		_current = this;
		if (a_generation == 0) {
			return;
		}
		auto current = s_lastShot.load(std::memory_order_relaxed);
		while (current != (std::numeric_limits<std::uint64_t>::max)()) {
			if (s_lastShot.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
				_shot = current + 1;
				return;
			}
		}
	}

	ShotScope::~ShotScope()
	{
		_current = _previous;
	}

	ShotScope* ShotScope::Current() noexcept
	{
		return _current;
	}

	bool Store::SetSession(std::uint64_t a_generation)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0) {
			_activeGeneration = 0;
			_launches.fill({});
			return true;
		}
		if (a_generation == _activeGeneration) {
			return true;
		}
		if (a_generation <= _highestGeneration) {
			return false;
		}
		_activeGeneration = a_generation;
		_highestGeneration = a_generation;
		_launches.fill({});
		return true;
	}

	bool Store::TrackShot(std::uint64_t a_generation, std::uint64_t a_shot)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0 || a_generation != _activeGeneration || a_shot == 0) {
			return false;
		}
		auto& entry = _shots[(a_shot - 1) % kCapacity];
		if (entry.shot == a_shot) {
			return entry.generation == a_generation;
		}
		if (entry.shot > a_shot) {
			return false;
		}
		entry = ShotEntry{ a_shot, a_generation, 0 };
		return true;
	}

	bool Store::CommitShot(std::uint64_t a_generation, std::uint64_t a_shot, std::uint32_t a_sequence)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0 || a_generation != _activeGeneration || a_shot == 0 || a_sequence == 0) {
			return false;
		}
		auto& entry = _shots[(a_shot - 1) % kCapacity];
		if (entry.shot != a_shot || entry.generation != a_generation ||
			(entry.sequence != 0 && entry.sequence != a_sequence)) {
			return false;
		}
		entry.sequence = a_sequence;
		return true;
	}

	std::uint32_t Store::Sequence(std::uint64_t a_generation, std::uint64_t a_shot)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0 || a_generation != _activeGeneration || a_shot == 0) {
			return 0;
		}
		const auto& entry = _shots[(a_shot - 1) % kCapacity];
		return entry.shot == a_shot && entry.generation == a_generation ? entry.sequence : 0;
	}

	bool Store::RecordLaunch(std::uint64_t a_generation, std::uint64_t a_shot, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_at)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0 || a_generation != _activeGeneration || a_shot == 0 || a_handle == 0) {
			return false;
		}
		_launches[_nextLaunch] = LaunchEntry{ a_generation, a_shot, a_handle, a_at };
		_nextLaunch = (_nextLaunch + 1) % kCapacity;
		return true;
	}

	std::uint64_t Store::ShotForHit(std::uint64_t a_generation, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_hitAt)
	{
		const std::scoped_lock lock(_mutex);
		if (a_generation == 0 || a_handle == 0) {
			return 0;
		}
		const LaunchEntry* match{};
		for (const auto& entry : _launches) {
			if (entry.generation != a_generation || entry.handle != a_handle || entry.at > a_hitAt) {
				continue;
			}
			if (!match || entry.at > match->at) {
				match = &entry;
			}
		}
		if (!match) {
			return 0;
		}
		if (a_hitAt - match->at < kRelaunchAmbiguity) {
			for (const auto& entry : _launches) {
				if (entry.generation == a_generation && entry.handle == a_handle && entry.at < match->at && match->at - entry.at < kRelaunchAmbiguity) {
					return 0;
				}
			}
		}
		return match->shot;
	}

	struct Ks7Vn2Ld9Rj4Ab1Wg6Zt3Fh {};
	static_assert(sizeof(Ks7Vn2Ld9Rj4Ab1Wg6Zt3Fh) == 1);
}
