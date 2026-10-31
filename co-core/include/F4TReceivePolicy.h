#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "F4TPacketRateLimit.h"
#include "F4TReceiveFrame.h"

namespace F4T::ReceivePolicy
{
	enum class Disposition
	{
		ParseFrame,
		DropFrame,
		CloseConnection
	};

	struct Config
	{
		std::size_t              maximumFrameBytes{ ReceiveFrame::DefaultMaximumFrameBytes };
		PacketRateLimit::Config  budget{};
	};

	struct Step
	{
		Disposition disposition{ Disposition::DropFrame };
		std::size_t frameStart{};
		std::size_t frameLength{};
		std::size_t nextParsePosition{};
	};

	struct State
	{
		PacketRateLimit::State budget{};
		bool                   stopped{};
	};

	[[nodiscard]] State Initialize(double a_nowMonotonicSeconds) noexcept;

	// Disposition for the next completed frame, or nullopt when no frame remains
	// or the session has stopped (a terminal frame discards the rest of the buffer).
	[[nodiscard]] std::optional<Step> Next(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		State& a_state,
		double a_nowMonotonicSeconds,
		Config a_config = {}) noexcept;

	// An unterminated remainder over the limit is terminal, not a clear-and-continue.
	[[nodiscard]] bool PendingOverflow(
		std::string_view a_buffer,
		std::size_t a_parsePosition,
		std::size_t a_maximumFrameBytes = ReceiveFrame::DefaultMaximumFrameBytes) noexcept;
}
