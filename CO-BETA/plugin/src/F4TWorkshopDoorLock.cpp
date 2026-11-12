#include "pch.h"

#include "F4TWorkshopDoorLock.h"

#include "F4TDebugLogging.h"
#include "F4TWorkshopReplicationClient.h"
#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESActivateEvent.h"

#include <cstdint>
#include <memory>

namespace F4T::WorkshopDoorLock
{
	namespace
	{
		constexpr std::uint32_t kObjectReferenceVmType{ 64 };

		bool DispatchObjectReference(RE::TESObjectREFR& a_ref, const char* a_function, bool a_first)
		{
			const auto game = RE::GameVM::GetSingleton();
			const auto vm = game ? game->GetVM() : nullptr;
			if (!vm) {
				return false;
			}
			const auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(kObjectReferenceVmType, std::addressof(a_ref));
			if (handle == handles.EmptyHandle()) {
				return false;
			}
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
			const auto queued = vm->DispatchMethodCall(static_cast<std::uint64_t>(handle), "ObjectReference", a_function, callback, a_first, false);
			if (!queued) {
				F4T::DebugLogging::Log("WorkshopDoorLock", "ObjectReference.{} on ref {:08X} was refused", a_function, a_ref.GetFormID());
			}
			return queued;
		}

		bool SetActivationBlocked(RE::TESObjectREFR& a_ref, bool a_blocked)
		{
			return DispatchObjectReference(a_ref, "BlockActivation", a_blocked);
		}

		class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (player && a_event.objectActivated && a_event.actionRef.get() == player) {
					F4T::WorkshopReplicationClient::HandleDoorActivation(*a_event.objectActivated, player->IsSneaking());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		ActivateSink g_sink;
		bool g_registered{};
	}

	bool SetDoorLocked(RE::TESObjectREFR& a_ref, bool a_locked)
	{
		return DispatchObjectReference(a_ref, "Lock", a_locked);
	}

	void RefreshActivationBlock(RE::TESObjectREFR& a_ref, bool a_ownerSide, bool a_ownedZone, bool a_doorLocked)
	{
		const auto* base = a_ref.GetObjectReference();
		if (!base) {
			return;
		}
		bool blocked{};
		if (base->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
			blocked = !a_ownerSide && a_doorLocked;
		} else if (base->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
			blocked = !a_ownerSide && a_ownedZone;
		} else {
			return;
		}
		if (a_ref.IsActivationBlocked() != blocked) {
			SetActivationBlocked(a_ref, blocked);
		}
	}

	void EnsureActivateSink()
	{
		if (!g_registered) {
			if (auto* source = RE::TESActivateEvent::GetEventSource()) {
				source->RegisterSink(&g_sink);
				g_registered = true;
			}
		}
	}
}
