#include "F4TRangedShotQueue.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>

int main()
{
	using namespace std::chrono_literals;
	using namespace F4T::RangedShotQueue;
	const auto now = std::chrono::steady_clock::time_point{} + 10s;
	Queue queue;
	std::uint32_t next = 1;
	assert(queue.Push({ 1, 11, now, 0 }) == 0);
	assert(queue.Push({ 1, 12, now, 0 }) == 0);
	auto first = queue.Drain(1, 1, now, 2s, next);
	assert(first.size() == 1 && first[0].shot == 11 && first[0].sequence == 1);
	assert(queue.Retry(1, first) == 0);
	auto retried = queue.Drain(1, 2, now, 2s, next);
	assert(retried.size() == 2 && retried[0].shot == 11 && retried[0].sequence == 1);
	assert(retried[1].shot == 12 && retried[1].sequence == 2 && next == 3);

	queue.Push({ 1, 13, now - 3s, 0 });
	queue.Push({ 2, 14, now, 0 });
	queue.Push({ 1, 15, now - 2s, 0 });
	auto current = queue.Drain(1, 3, now, 2s, next);
	assert(current.size() == 1 && current[0].shot == 15 && current[0].sequence == 3);
	assert(queue.Retry(2, current) == 0);
	assert(queue.Drain(1, 1, now, 2s, next).empty());

	next = (std::numeric_limits<std::uint32_t>::max)();
	queue.Push({ 1, 16, now, 0 });
	queue.Push({ 1, 17, now, 0 });
	auto wrapped = queue.Drain(1, 2, now, 2s, next);
	assert(wrapped.size() == 2 && wrapped[0].sequence == (std::numeric_limits<std::uint32_t>::max)());
	assert(wrapped[1].sequence == 1 && next == 2);

	for (std::size_t i = 0; i < Queue::kCapacity; ++i) assert(queue.Push({ 1, 100 + i, now, 0 }) == 0);
	assert(queue.Push({ 1, 999, now, 0 }) == 1);
	auto bounded = queue.Drain(1, Queue::kCapacity, now, 2s, next);
	assert(bounded.size() == Queue::kCapacity && bounded.front().shot == 101 && bounded.back().shot == 999);
	for (std::size_t i = 0; i < Queue::kCapacity; ++i) assert(queue.Push({ 1, 200 + i, now, 0 }) == 0);
	assert(queue.Retry(1, wrapped) == 3);
	auto afterRetry = queue.Drain(1, Queue::kCapacity, now, 2s, next);
	assert(afterRetry.size() == Queue::kCapacity && afterRetry[0].shot == 16 && afterRetry[1].shot == 17);
	assert(afterRetry.back().shot == 261);
	queue.Push({ 1, 1000, now, 0 });
	queue.Clear();
	assert(queue.Drain(1, 1, now, 2s, next).empty());
	return 0;
}
