#pragma once

#include <string_view>

namespace F4T::SessionLaunchDiagnostics
{
	enum class CharGenPhase
	{
		None,
		StartingNewGame,
		VanillaIntro,
		WaitingForContinue,
		WaitingForPodExit,
		WaitingForGameplayWorldReady,
		WaitingForProfileBind,
		Complete,
		Failed
	};

	std::string_view PhaseName(CharGenPhase a_phase);
	void LogInfo(std::string_view a_message);
	void LogWarning(std::string_view a_message);
	void LogDebug(std::string_view a_message);
}
