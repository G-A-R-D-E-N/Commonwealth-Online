#include "F4TRetiredQueue.h"

#include <cassert>
#include <cstdint>
#include <string>

int main()
{
	using Queue = F4T::RetiredQueue<std::uint32_t, std::string, 3>;

	{
		Queue queue;
		queue.Push(7U, "partial");
		const auto taken = queue.Take(7U);
		assert(taken && *taken == "partial");
		assert(!queue.Take(7U));
		assert(!queue.Take(8U));
	}

	{
		Queue queue;
		queue.Push(2U, "old");
		queue.Push(3U, "other");
		queue.Push(2U, "new");
		const auto newest = queue.Take(2U);
		assert(newest && *newest == "new");
		assert(!queue.Take(2U));
		const auto other = queue.Take(3U);
		assert(other && *other == "other");
	}

	{
		Queue queue;
		queue.Push(1U, "a");
		queue.Push(2U, "b");
		queue.Push(3U, "c");
		queue.Push(4U, "d");
		assert(!queue.Take(1U));
		const auto second = queue.Take(2U);
		const auto third = queue.Take(3U);
		const auto fourth = queue.Take(4U);
		assert(second && *second == "b");
		assert(third && *third == "c");
		assert(fourth && *fourth == "d");
	}

	{
		Queue queue;
		queue.Push(1U, "a");
		queue.Push(2U, "b");
		queue.Push(1U, "c");
		queue.Push(3U, "d");
		assert(queue.Take(2U));
		assert(queue.Take(1U));
		assert(queue.Take(3U));
	}

	{
		Queue queue;
		queue.Push(5U, "x");
		queue.Clear();
		assert(!queue.Take(5U));
	}

	return 0;
}
