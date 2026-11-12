#pragma once

namespace RE
{
	class TESObjectREFR;
}

namespace F4T::SleepController
{
	bool TryBegin(RE::TESObjectREFR* a_bed);
	void Tick();
	void OnDisconnect();
}
