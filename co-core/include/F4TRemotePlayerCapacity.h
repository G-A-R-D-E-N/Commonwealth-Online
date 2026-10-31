#pragma once

#include <cstddef>

namespace F4T::RemotePlayerCapacity
{
	inline constexpr std::size_t MaximumSessionPlayers = 16;

	// Bounds server-supplied player IDs; must stay >= MaximumSessionPlayers.
	inline constexpr std::size_t MaximumTrackedRemotePlayers = 256;

	enum class Admission
	{
		UpdateExisting,
		InsertNew,
		RejectAtCapacity
	};

	// An already-tracked ID always updates, including at capacity.
	[[nodiscard]] Admission Decide(std::size_t a_trackedCount, bool a_alreadyTracked) noexcept;
}
