#include "F4TAdminItemQueue.h"

#include <cassert>
#include <cstdint>
#include <vector>

using F4T::AdminItems::PendingQueue;
using F4T::AdminItems::QueueResult;
using F4T::AdminItems::StillCurrent;

int main()
{
	{
		PendingQueue<int> queue;
		assert(queue.Enqueue(10, 1) == QueueResult::kQueued);
		assert(queue.Enqueue(11, 1) == QueueResult::kQueued);
		assert(queue.Enqueue(20, 2) == QueueResult::kQueued);

		const auto drained = queue.Drain(2, true);
		assert(drained.admitted.size() == 1);
		assert(drained.admitted[0] == 20);
		assert(drained.discarded == 2);
	}
	{
		PendingQueue<int> queue;
		assert(queue.Enqueue(7, 2) == QueueResult::kQueued);

		const auto drained = queue.Drain(2, false);
		assert(drained.admitted.empty());
		assert(drained.discarded == 1);
	}
	{
		PendingQueue<int> queue;
		for (int value = 0; value < 64; ++value) {
			assert(queue.Enqueue(value, 3) == QueueResult::kQueued);
		}
		assert(queue.Enqueue(64, 3) == QueueResult::kBusy);

		const auto drained = queue.Drain(3, true);
		assert(drained.admitted.size() == 64);
		assert(drained.admitted.front() == 0);
		assert(drained.admitted.back() == 63);
		assert(drained.discarded == 0);
		assert(queue.Enqueue(99, 3) == QueueResult::kQueued);
	}
	{
		PendingQueue<int> queue;
		assert(queue.Enqueue(1, 5) == QueueResult::kQueued);
		queue.Drain(5, true);

		const auto drained = queue.Drain(5, true);
		assert(drained.admitted.empty());
		assert(drained.discarded == 0);
	}
	{
		assert(StillCurrent(5, 5, true));
		assert(!StillCurrent(5, 6, true));
		assert(!StillCurrent(5, 5, false));

		PendingQueue<int> queue;
		assert(queue.Enqueue(1, 5) == QueueResult::kQueued);
		assert(queue.Enqueue(2, 5) == QueueResult::kQueued);

		const auto drained = queue.Drain(5, true);
		assert(drained.admitted.size() == 2);

		std::vector<int> applied;
		std::uint64_t current = 5;
		for (const auto request : drained.admitted) {
			if (!StillCurrent(5, current, true)) {
				break;
			}
			applied.push_back(request);
			current = 6;
		}
		assert(applied.size() == 1);
		assert(applied[0] == 1);
	}
	return 0;
}
