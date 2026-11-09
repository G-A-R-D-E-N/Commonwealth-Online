#pragma once

#include <cstdint>

namespace RE
{
	class PlayerCharacter;
}

namespace F4T::SleepBedHook
{
	void Install();
	void Tick();
	void CallNativeSleep(RE::PlayerCharacter* a_player, std::int32_t a_hours);
}
