#include "pch.h"

#include "F4TSessionLaunchDiagnostics.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include <string>

namespace F4T::SessionLaunchDiagnostics
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return F4T::Networking::GetLocalPlayerLogPrefix() + " [SessionLaunch]";
		}
	}

	void LogInfo(std::string_view a_message)
	{
		REX::INFO("{} {}", GetLogPrefix(), a_message);
	}

	void LogWarning(std::string_view a_message)
	{
		REX::WARN("{} {}", GetLogPrefix(), a_message);
	}

	void LogDebug(std::string_view a_message)
	{
		F4T::DebugLogging::Write("SessionLaunch", GetLogPrefix() + " " + std::string(a_message));
	}

	std::string_view PhaseName(CharGenPhase a_phase)
	{
		switch (a_phase) {
		case CharGenPhase::None:
			return "None";
		case CharGenPhase::StartingNewGame:
			return "StartingNewGame";
		case CharGenPhase::VanillaIntro:
			return "VanillaIntro";
		case CharGenPhase::WaitingForContinue:
			return "WaitingForContinue";
		case CharGenPhase::WaitingForPodExit:
			return "WaitingForPodExit";
		case CharGenPhase::WaitingForGameplayWorldReady:
			return "WaitingForGameplayWorldReady";
		case CharGenPhase::WaitingForProfileBind:
			return "WaitingForProfileBind";
		case CharGenPhase::Complete:
			return "Complete";
		case CharGenPhase::Failed:
			return "Failed";
		}
		return "Unknown";
	}
}
