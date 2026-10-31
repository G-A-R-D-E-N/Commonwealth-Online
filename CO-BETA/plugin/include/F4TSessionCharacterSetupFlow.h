#pragma once

namespace F4T::SessionCharacterSetupFlow
{
	void Reset();
	[[nodiscard]] bool StartVanillaNewGame();
	[[nodiscard]] bool BeginOnboarding();
	[[nodiscard]] bool ShowContinueAlert();
	[[nodiscard]] bool PollContinueAlert();
	void ShowReturnToMenuAlert();
	void ContinueToVault();
	void Abort();
	[[nodiscard]] bool IsPodExitSignal();
	[[nodiscard]] bool IsComplete();
	void EnsureRunning();
}
