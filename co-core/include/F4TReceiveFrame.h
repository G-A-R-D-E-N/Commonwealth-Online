#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace F4T::ReceiveFrame
{
	inline constexpr std::size_t DefaultMaximumFrameBytes = 64U * 1024U;

	struct Completion
	{
		std::size_t frameStart{};
		std::size_t frameLength{};
		std::size_t nextParsePosition{};
		bool        frameExceedsLimit{};
	};

	// frameExceedsLimit lets the caller reject before allocating or parsing.
	[[nodiscard]] std::optional<Completion> FindNext(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes = DefaultMaximumFrameBytes) noexcept;

	// True when the unterminated remainder at a_parsePosition exceeds the limit.
	[[nodiscard]] bool PendingExceedsLimit(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes = DefaultMaximumFrameBytes) noexcept;
}
