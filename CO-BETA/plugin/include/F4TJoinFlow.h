#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::JoinFlow
{
	void BeginConnecting();
	void NotifyAuthenticated(bool a_firstTime);
	[[nodiscard]] bool IsActive();
	[[nodiscard]] bool IsReady();
	[[nodiscard]] bool CanPublishGameplayTransform();
	[[nodiscard]] bool IsFlyOverRunning();
	void Tick();
	void EndSessionJoin(bool a_success, std::string_view a_reason);
	void OnWorldReset(bool a_newWorld);
	void Shutdown();
	[[nodiscard]] std::string_view StateId();
}
