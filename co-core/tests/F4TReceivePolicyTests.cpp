#include "F4TReceivePolicy.h"

#include <cassert>
#include <string>

using namespace F4T::ReceivePolicy;
using namespace F4T::ReceiveFrame;

namespace
{
	Config SmallPacketBudget()
	{
		Config config;
		config.budget.maximumPacketsPerWindow = 2;
		config.budget.maximumBytesPerWindow = 1024;
		config.budget.maximumViolationWindows = 3;
		config.budget.windowSeconds = 1.0;
		return config;
	}
}

int main()
{
	// A frame at the boundary is parsed.
	{
		std::string buffer(DefaultMaximumFrameBytes, 'a');
		buffer.push_back('\n');
		auto state = Initialize(0.0);
		const auto step = Next(buffer, 0, state, 0.0);
		assert(step.has_value());
		assert(step->disposition == Disposition::ParseFrame);
		assert(step->nextParsePosition == buffer.size());
	}

	// A completed frame over the boundary closes the connection.
	{
		std::string buffer(DefaultMaximumFrameBytes + 1, 'a');
		buffer.push_back('\n');
		auto state = Initialize(0.0);
		const auto step = Next(buffer, 0, state, 0.0);
		assert(step.has_value());
		assert(step->disposition == Disposition::CloseConnection);
	}

	// An unterminated remainder over the boundary is terminal.
	{
		const std::string over(DefaultMaximumFrameBytes + 1, 'a');
		assert(PendingOverflow(over, 0));
		const std::string boundary(DefaultMaximumFrameBytes, 'a');
		assert(!PendingOverflow(boundary, 0));
	}

	// A single over-budget frame is dropped; sustained windows close.
	{
		const auto config = SmallPacketBudget();
		auto state = Initialize(0.0);

		const std::string windowOne = "a\nb\nc\n";
		assert(Next(windowOne, 0, state, 0.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowOne, 2, state, 0.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowOne, 4, state, 0.0, config)->disposition == Disposition::DropFrame);

		const std::string windowTwo = "d\ne\nf\n";
		assert(Next(windowTwo, 0, state, 1.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowTwo, 2, state, 1.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowTwo, 4, state, 1.0, config)->disposition == Disposition::DropFrame);

		const std::string windowThree = "g\nh\ni\n";
		assert(Next(windowThree, 0, state, 2.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowThree, 2, state, 2.0, config)->disposition == Disposition::ParseFrame);
		assert(Next(windowThree, 4, state, 2.0, config)->disposition == Disposition::CloseConnection);
	}

	// A terminal session frame stops the loop; later buffered frames never parse.
	{
		const std::string buffer = "sessionEnded\n{\"type\":\"transform\"}\n";
		auto state = Initialize(0.0);
		const auto first = Next(buffer, 0, state, 0.0);
		assert(first.has_value());
		assert(first->disposition == Disposition::ParseFrame);

		// Pre-fix behaviour: without the stop flag the later frame is still reachable.
		const auto reachable = Next(buffer, first->nextParsePosition, state, 0.0);
		assert(reachable.has_value());

		state.stopped = true;
		assert(!Next(buffer, first->nextParsePosition, state, 0.0).has_value());
	}

	return 0;
}
