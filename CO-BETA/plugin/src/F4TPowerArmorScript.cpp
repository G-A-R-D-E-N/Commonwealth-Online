#include "pch.h"

#include "F4TPowerArmorScript.h"

#include "F4TProxyActorControllerInternal.h"

#include "RE/A/Actor.h"
#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectREFR.h"

#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace
{
	using F4T::ProxyActorController::detail::LogThrottledWarning;

	constexpr std::uint32_t kActorVmType{ 65 };
	constexpr std::uint32_t kReferenceVmType{ 64 };

	std::mutex g_lockedFramesMutex;
	std::unordered_set<std::uint32_t> g_lockedFrames;

	template <class... Args>
	bool DispatchOnObject(std::uint32_t a_vmType, const void* a_object, const char* a_className, const char* a_function, Args... a_args)
	{
		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!vm) {
			return false;
		}
		const auto& handles = vm->GetObjectHandlePolicy();
		const auto handle = handles.GetHandleForObject(a_vmType, a_object);
		if (handle == handles.EmptyHandle()) {
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::Object> object;
		if (!vm->FindBoundObject(handle, a_className, false, object, false) &&
			vm->CreateObject(a_className, object) &&
			object) {
			vm->GetObjectBindPolicy().BindObject(object, handle);
		}
		if (!object) {
			return false;
		}
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
		return vm->DispatchMethodCall(
			static_cast<std::uint64_t>(handle),
			a_className,
			a_function,
			callback,
			a_args...);
	}
}

namespace F4T::PowerArmorScript
{
	bool SwitchActorToPowerArmor(RE::Actor& a_actor, RE::TESObjectREFR& a_frame)
	{
		if (IsFrameLockedByUs(a_frame.GetFormID())) {
			LogThrottledWarning(
				"power_armor_locked_frame_enter",
				std::format("Refused to enter power armor frame {:08X}; it is locked for another player.", a_frame.GetFormID()));
			return false;
		}
		return DispatchOnObject(kActorVmType, std::addressof(a_actor), "Actor", "SwitchToPowerArmor", std::addressof(a_frame));
	}

	bool ExitActorFromPowerArmor(RE::Actor& a_actor)
	{
		return DispatchOnObject(kActorVmType, std::addressof(a_actor), "Actor", "SwitchToPowerArmor", static_cast<RE::TESObjectREFR*>(nullptr));
	}

	bool SetFrameLocked(RE::TESObjectREFR& a_frame, bool a_locked)
	{
		if (!DispatchOnObject(kReferenceVmType, std::addressof(a_frame), "ObjectReference", "BlockActivation", a_locked, false)) {
			return false;
		}
		const auto frameFormId = a_frame.GetFormID();
		const std::scoped_lock lock(g_lockedFramesMutex);
		if (a_locked) {
			g_lockedFrames.insert(frameFormId);
		} else {
			g_lockedFrames.erase(frameFormId);
		}
		return true;
	}

	bool DisableFrame(RE::TESObjectREFR& a_frame)
	{
		return DispatchOnObject(kReferenceVmType, std::addressof(a_frame), "ObjectReference", "Disable", false);
	}

	void ForgetFrameLock(std::uint32_t a_frameFormId)
	{
		const std::scoped_lock lock(g_lockedFramesMutex);
		g_lockedFrames.erase(a_frameFormId);
	}

	bool IsFrameLockedByUs(std::uint32_t a_frameFormId)
	{
		const std::scoped_lock lock(g_lockedFramesMutex);
		return g_lockedFrames.contains(a_frameFormId);
	}

	bool SetFrameOwner(RE::TESObjectREFR& a_frame, RE::TESNPC* a_ownerBase)
	{
		if (!a_ownerBase) {
			return false;
		}
		return DispatchOnObject(kReferenceVmType, std::addressof(a_frame), "ObjectReference", "SetActorOwner", a_ownerBase, false);
	}
}
