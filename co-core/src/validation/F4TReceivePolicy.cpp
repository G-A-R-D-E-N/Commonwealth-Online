#include "F4TReceivePolicy.h"

#include <cstdint>

namespace F4T::ReceivePolicy
{
	State Initialize(double a_nowMonotonicSeconds) noexcept
	{
		State state;
		state.budget = PacketRateLimit::Initialize(a_nowMonotonicSeconds);
		return state;
	}

	std::optional<Step> Next(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		State& a_state,
		double a_nowMonotonicSeconds,
		Config a_config) noexcept
	{
		if (a_state.stopped) {
			return std::nullopt;
		}

		const auto completion = ReceiveFrame::FindNext(a_buffer, a_parsePosition, a_config.maximumFrameBytes);
		if (!completion) {
			return std::nullopt;
		}

		Step step;
		step.frameStart = completion->frameStart;
		step.frameLength = completion->frameLength;
		step.nextParsePosition = completion->nextParsePosition;

		if (completion->frameExceedsLimit) {
			step.disposition = Disposition::CloseConnection;
			return step;
		}

		const auto budget = PacketRateLimit::Observe(
			a_state.budget,
			a_nowMonotonicSeconds,
			a_config.budget,
			static_cast<std::uint64_t>(completion->frameLength));
		if (budget.verdict == PacketRateLimit::Verdict::Disconnect) {
			step.disposition = Disposition::CloseConnection;
			return step;
		}

		step.disposition = budget.verdict == PacketRateLimit::Verdict::Limited ?
		                       Disposition::DropFrame :
		                       Disposition::ParseFrame;
		return step;
	}

	bool PendingOverflow(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes) noexcept
	{
		return ReceiveFrame::PendingExceedsLimit(a_buffer, a_parsePosition, a_maximumFrameBytes);
	}
}
