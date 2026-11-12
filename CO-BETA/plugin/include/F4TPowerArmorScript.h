#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
	class TESNPC;
	class TESObjectREFR;
}

namespace F4T::PowerArmorScript
{
	[[nodiscard]] bool SwitchActorToPowerArmor(RE::Actor& a_actor, RE::TESObjectREFR& a_frame);
	bool ExitActorFromPowerArmor(RE::Actor& a_actor);
	[[nodiscard]] bool SetFrameLocked(RE::TESObjectREFR& a_frame, bool a_locked);
	[[nodiscard]] bool SetFrameOwner(RE::TESObjectREFR& a_frame, RE::TESNPC* a_ownerBase);
	[[nodiscard]] bool DisableFrame(RE::TESObjectREFR& a_frame);
	void ForgetFrameLock(std::uint32_t a_frameFormId);
	[[nodiscard]] bool IsFrameLockedByUs(std::uint32_t a_frameFormId);
}
