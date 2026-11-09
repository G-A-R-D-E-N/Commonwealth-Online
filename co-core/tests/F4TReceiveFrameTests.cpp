#include "F4TReceiveFrame.h"

#include <cassert>
#include <string>

int main()
{
	using namespace F4T::ReceiveFrame;

	// No newline yet: nothing to complete.
	assert(!FindNext("partial", 0).has_value());

	// A frame exactly at the boundary is accepted.
	{
		std::string buffer(DefaultMaximumFrameBytes, 'a');
		buffer.push_back('\n');
		const auto completion = FindNext(buffer, 0);
		assert(completion.has_value());
		assert(!completion->frameExceedsLimit);
		assert(completion->frameLength == DefaultMaximumFrameBytes);
		assert(completion->nextParsePosition == buffer.size());
	}

	// A completed frame over the boundary is flagged before any parse.
	{
		std::string buffer(DefaultMaximumFrameBytes + 1, 'a');
		buffer.push_back('\n');
		const auto completion = FindNext(buffer, 0);
		assert(completion.has_value());
		assert(completion->frameExceedsLimit);
	}

	// A moving parse position finds each frame and reports its own length.
	{
		std::string buffer(DefaultMaximumFrameBytes + 1, 'a');
		buffer.push_back('\n');
		buffer += "ok\n";
		const auto first = FindNext(buffer, 0);
		assert(first.has_value());
		assert(first->frameExceedsLimit);
		const auto second = FindNext(buffer, first->nextParsePosition);
		assert(second.has_value());
		assert(!second->frameExceedsLimit);
		assert(second->frameLength == 2);
		assert(second->nextParsePosition == buffer.size());
	}

	// An empty terminated frame is not oversized.
	{
		const std::string buffer = "\n";
		const auto completion = FindNext(buffer, 0);
		assert(completion.has_value());
		assert(completion->frameLength == 0);
		assert(!completion->frameExceedsLimit);
	}

	// An unterminated pending remainder over the boundary is rejected.
	{
		const std::string over(DefaultMaximumFrameBytes + 1, 'a');
		assert(PendingExceedsLimit(over, 0));
		const std::string boundary(DefaultMaximumFrameBytes, 'a');
		assert(!PendingExceedsLimit(boundary, 0));
	}

	// Positions past the end are handled without reading out of range.
	assert(!FindNext("abc", 99).has_value());
	assert(!PendingExceedsLimit("abc", 99));

	return 0;
}
