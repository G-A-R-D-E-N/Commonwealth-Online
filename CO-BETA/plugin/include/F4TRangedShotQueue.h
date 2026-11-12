#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace F4T::RangedShotQueue
{
	struct Shot
	{
		std::uint64_t generation{};
		std::uint64_t shot{};
		std::chrono::steady_clock::time_point capturedAt{};
		std::uint32_t sequence{};
	};

	class Queue
	{
	public:
		static constexpr std::size_t kCapacity = 64;

		std::uint64_t Push(Shot a_shot);
		std::vector<Shot> Drain(std::uint64_t a_generation, std::size_t a_limit,
			std::chrono::steady_clock::time_point a_now, std::chrono::steady_clock::duration a_window,
			std::uint32_t& a_nextSequence);
		std::uint64_t Retry(std::uint64_t a_generation, std::span<const Shot> a_shots);
		void Clear();

	private:
		std::mutex _mutex;
		std::deque<Shot> _shots;
		std::uint64_t _evictions{};
	};
}
