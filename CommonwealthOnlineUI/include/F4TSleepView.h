#pragma once

#include <cstdint>

namespace F4T::SleepView
{
	enum class Phase { Closed, Choose, Resting };
	enum class Action { Sleep, Cancel };
	using ActionCallback = void (*)(Action, std::uint32_t, std::uint32_t);

	void Initialize();
	void EnsureView();
	bool Open(std::uint32_t a_token, std::uint32_t a_maxHours, ActionCallback a_callback);
	void SetPhase(Phase a_phase, std::uint32_t a_hours);
	void SetProgress(float a_elapsedHours);
	void Close();
	void SyncControllerActions();
	void Cancel();
	bool IsCurrentlyVisible();
	bool IsReady();
	bool TryDispatchControllerInput(const char* a_button, const char* a_phase);
}
