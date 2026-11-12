#pragma once

namespace F4T::SleepClock
{
	void Begin();
	void End();
	void Cancel();
	void Tick();
	bool Active();
	bool InSettle();
	void SharedNow(double& a_hour, double& a_daysPassed);
}
