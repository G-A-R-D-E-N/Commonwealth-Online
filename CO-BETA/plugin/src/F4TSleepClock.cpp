#include "pch.h"

#include "F4TSleepClock.h"
#include "F4TSleepClockMath.h"
#include "F4TWorldStateSync.h"

#include "RE/C/Calendar.h"

#include <chrono>
#include <cmath>

namespace
{
	constexpr int kSettleFrames = 30;

	bool g_active{ false };
	int g_settleFramesLeft{ 0 };
	F4T::SleepClockMath::Baseline g_baseline{};

	double RealSecondsNow()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void FinishSettle()
	{
		double hour = 0.0;
		double daysPassed = 0.0;
		F4T::SleepClockMath::SharedAt(g_baseline, RealSecondsNow(), hour, daysPassed);
		g_settleFramesLeft = 0;
		g_baseline = {};
		F4T::WorldStateSync::ApplySleepClockSettle(hour, daysPassed);
	}
}

namespace F4T::SleepClock
{
	void Begin()
	{
		if (g_settleFramesLeft > 0) {
			FinishSettle();
		}

		const auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour || !calendar->gameDaysPassed || !calendar->timeScale) {
			return;
		}

		const auto hour = calendar->gameHour->GetValue();
		const auto daysPassed = calendar->gameDaysPassed->GetValue();
		const auto timeScale = calendar->timeScale->GetValue();
		if (!std::isfinite(hour) || !std::isfinite(daysPassed) || !std::isfinite(timeScale)) {
			return;
		}

		g_baseline = F4T::SleepClockMath::Begin(hour, daysPassed, RealSecondsNow(), timeScale);
		g_active = true;
	}

	void End()
	{
		if (!g_active) {
			return;
		}

		g_active = false;
		g_settleFramesLeft = kSettleFrames;
	}

	void Cancel()
	{
		g_active = false;
		g_baseline = {};
	}

	void Tick()
	{
		if (g_settleFramesLeft > 0 && --g_settleFramesLeft == 0) {
			FinishSettle();
		}
	}

	bool Active()
	{
		return g_active;
	}

	bool InSettle()
	{
		return g_settleFramesLeft > 0;
	}

	void SharedNow(double& a_hour, double& a_daysPassed)
	{
		F4T::SleepClockMath::SharedAt(g_baseline, RealSecondsNow(), a_hour, a_daysPassed);
	}
}
