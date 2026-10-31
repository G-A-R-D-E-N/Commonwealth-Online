#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::WorldStateSync
{
	struct PendingWorldState
	{
		float gameHour{ 0.0F };
		float gameDaysPassed{ 0.0F };
		std::optional<std::uint32_t> weatherFormId;
		bool hasWeatherFormId{ false };
		bool applyTime{ false };
	};

	struct PendingServerWorldState
	{
		std::optional<std::string> timeHHmm;
		std::optional<std::string> weatherFormId;
	};

	bool IsWorldStateHost();
	std::optional<std::uint32_t> GetWorldStateHostPlayerId();
	void SetWorldStateHostPlayerId(std::uint32_t a_hostPlayerId);
	void QueueWorldStateHostPlayerId(std::uint32_t a_hostPlayerId);
	void Reset();
	void RequestReset();
	void SetPendingWorldState(PendingWorldState a_state);
	void SetPendingServerWorldState(PendingServerWorldState a_state);
	void PollHostCapture();
	void ApplyPendingState();
}
