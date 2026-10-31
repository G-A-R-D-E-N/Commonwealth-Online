#pragma once

#include <string>
#include <string_view>

namespace F4T::PauseMenuMap
{
	void StartStreaming();
	void StopStreaming();
	bool IsStreaming();

	void RequestFastTravel(std::string_view a_markerId);

	bool ConsumeFastTravelCloseRequest();

	void TickDeferredFastTravel();

	std::string BuildUpdateJson();

	void Tick();
}
