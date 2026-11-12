#include "F4TReceiveFrame.h"

namespace F4T::ReceiveFrame
{
	std::optional<Completion> FindNext(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes) noexcept
	{
		if (a_parsePosition > a_buffer.size()) {
			return std::nullopt;
		}

		const auto newlinePosition = a_buffer.find('\n', a_parsePosition);
		if (newlinePosition == std::string_view::npos) {
			return std::nullopt;
		}

		Completion completion;
		completion.frameStart = a_parsePosition;
		completion.frameLength = newlinePosition - a_parsePosition;
		completion.nextParsePosition = newlinePosition + 1;
		completion.frameExceedsLimit = completion.frameLength > a_maximumFrameBytes;
		return completion;
	}

	bool PendingExceedsLimit(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes) noexcept
	{
		if (a_parsePosition >= a_buffer.size()) {
			return false;
		}
		return (a_buffer.size() - a_parsePosition) > a_maximumFrameBytes;
	}
}
