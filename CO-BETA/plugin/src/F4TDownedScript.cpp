#include "pch.h"

#include "F4TDownedScript.h"

#include "F4TDebugLogging.h"

#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"

#include <memory>
#include <string>

namespace
{
	constexpr std::uint32_t kActorVmType{ 65 };
	constexpr std::string_view kLogComponent{ "DownedScript" };
}

namespace F4T::Downed::Script
{
	bool CallActorBool(RE::Actor& a_actor, std::string_view a_function, bool a_value)
	{
		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!vm) {
			return false;
		}
		const auto& handles = vm->GetObjectHandlePolicy();
		const auto handle = handles.GetHandleForObject(kActorVmType, std::addressof(a_actor));
		if (handle == handles.EmptyHandle()) {
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::Object> object;
		if (!vm->FindBoundObject(handle, "Actor", false, object, false) &&
			vm->CreateObject("Actor", object) &&
			object) {
			vm->GetObjectBindPolicy().BindObject(object, handle);
		}
		if (!object) {
			return false;
		}
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
		const auto function = std::string{ a_function };
		const auto queued = vm->DispatchMethodCall(
			static_cast<std::uint64_t>(handle),
			"Actor",
			function.c_str(),
			callback,
			a_value);
		if (!queued) {
			F4T::DebugLogging::Log(kLogComponent, "dispatch of {} on actor {:08X} was refused", function, a_actor.GetFormID());
		}
		return queued;
	}
}
