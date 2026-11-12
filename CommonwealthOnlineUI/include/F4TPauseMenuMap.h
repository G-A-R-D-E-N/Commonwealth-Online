#pragma once

#include "F4TViewJson.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::PauseMenuMap
{
	void StartStreaming();
	void StopStreaming();
	bool IsStreaming();

	void RequestFastTravel(std::string_view a_markerId);
	void RequestTravelToPlayer(std::string_view a_playerId, std::int32_t a_cost);

	bool ConsumeFastTravelCloseRequest();

	void TickDeferredFastTravel();

	void SetTeleportBeganCallback(void (*a_callback)());

	std::string BuildUpdateJson();

	void Tick();
	void SampleFogOfWar();
}
