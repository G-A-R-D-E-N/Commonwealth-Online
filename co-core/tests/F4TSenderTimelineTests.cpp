#include "F4TSenderTimeline.h"
#include "F4TSnapshotInterpolation.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
	using F4T::Replication::AdaptiveRenderDelay;
	using F4T::Replication::SenderClock;

	constexpr double kEpoch = 1.79e9;

	bool Near(double a_lhs, double a_rhs, double a_eps)
	{
		return std::fabs(a_lhs - a_rhs) <= a_eps;
	}
}

int main()
{
	{
		SenderClock clock{};
		const auto first = clock.Observe(kEpoch, 100.0);
		assert(first.result == SenderClock::Result::Reset);
		assert(Near(first.mappedSeconds, 100.0, 1.0e-9));
		assert(first.lateSeconds == 0.0 && first.senderIntervalSeconds == 0.0);
		assert(clock.Established());
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.030));
		const auto ten = clock.Observe(kEpoch + 0.05, kEpoch + 0.060);
		assert(ten.result == SenderClock::Result::Mapped);
		assert(Near(ten.mappedSeconds, kEpoch + 0.060, 1.0e-5));
		assert(Near(ten.lateSeconds, 0.0, 1.0e-5));
		const auto twentyFive = clock.Observe(kEpoch + 0.10, kEpoch + 0.125);
		assert(twentyFive.result == SenderClock::Result::Mapped);
		assert(Near(twentyFive.lateSeconds, 0.015, 1.0e-3));
		assert(Near(twentyFive.mappedSeconds - (kEpoch + 0.10), 0.010, 1.0e-3));
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.010));
		SenderClock::Mapping last{};
		for (int i = 1; i <= 5; ++i) {
			const double sender = kEpoch + i * 1.0;
			last = clock.Observe(sender, sender + 0.010 + 0.020 * i);
		}
		assert(last.result == SenderClock::Result::Mapped);
		assert(Near(last.mappedSeconds - (kEpoch + 5.0), 0.0202, 1.0e-4));
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.010));
		static_cast<void>(clock.Observe(kEpoch + 0.05, kEpoch + 0.060));
		assert(clock.Observe(kEpoch - 2.0, kEpoch + 0.100).result == SenderClock::Result::Reset);
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.010));
		static_cast<void>(clock.Observe(kEpoch + 0.05, kEpoch + 0.060));
		assert(clock.Observe(kEpoch + 0.05, kEpoch + 0.070).result == SenderClock::Result::Stale);
		assert(clock.Observe(kEpoch + 0.04, kEpoch + 0.080).result == SenderClock::Result::Stale);
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.010));
		assert(clock.Observe(kEpoch + 6.0, kEpoch + 6.010).result == SenderClock::Result::Reset);
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.010));
		static_cast<void>(clock.Observe(kEpoch + 0.05, kEpoch + 0.060));
		assert(clock.Observe(kEpoch + 0.10, kEpoch + 1.160).result == SenderClock::Result::Reset);
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 1.5));
		assert(clock.Observe(kEpoch + 2.0, kEpoch + 1.55).result == SenderClock::Result::Reset);
	}

	{
		SenderClock clock{};
		const double nan = std::numeric_limits<double>::quiet_NaN();
		assert(clock.Observe(0.0, 100.0).result == SenderClock::Result::Unavailable);
		assert(clock.Observe(-1.0, 100.0).result == SenderClock::Result::Unavailable);
		assert(clock.Observe(nan, 100.0).result == SenderClock::Result::Unavailable);
		assert(clock.Observe(kEpoch, nan).result == SenderClock::Result::Unavailable);
		assert(!clock.Established());
	}

	{
		SenderClock clock{};
		static_cast<void>(clock.Observe(kEpoch, kEpoch + 0.0123));
		const auto next = clock.Observe(kEpoch + 0.05, kEpoch + 0.0623);
		assert(next.result == SenderClock::Result::Mapped);
		assert(Near(next.mappedSeconds, kEpoch + 0.0623, 1.0e-4));
		assert(Near(next.lateSeconds, 0.0, 1.0e-4));
	}

	{
		AdaptiveRenderDelay delay{};
		constexpr double lates[4] = { 0.0, 0.010, 0.030, 0.020 };
		for (int i = 0; i < 16; ++i) {
			delay.NoteTimedSample(0.05, lates[i % 4]);
		}
		assert(Near(delay.TimedTargetDelay(), 0.090, 1.0e-9));
		for (int i = 0; i < 40; ++i) {
			delay.NoteTimedSample(0.05, lates[i % 4]);
		}
		assert(Near(delay.Delay(), 0.090, 1.0e-6));
	}

	{
		AdaptiveRenderDelay delay{};
		delay.NoteTimedSample(0.05, 0.0);
		delay.NoteTimedSample(0.05, 0.0);
		assert(Near(delay.Delay(), 0.095, 1.0e-9));
	}

	{
		AdaptiveRenderDelay delay{};
		delay.NoteTimedSample(0.6, 0.0);
		delay.NoteTimedSample(0.0, 0.0);
		delay.NoteTimedSample(0.05, -0.001);
		delay.NoteTimedSample(0.05, std::nan(""));
		assert(delay.Delay() == AdaptiveRenderDelay::kDefaultDelaySeconds);
		assert(delay.TimedTargetDelay() == AdaptiveRenderDelay::kDefaultDelaySeconds);
	}

	{
		AdaptiveRenderDelay delay{};
		delay.NoteTimedSample(0.05, 0.030);
		delay.NoteTimedSample(0.05, 0.030);
		delay.Reset();
		assert(delay.TimedTargetDelay() == AdaptiveRenderDelay::kDefaultDelaySeconds);
	}
}
