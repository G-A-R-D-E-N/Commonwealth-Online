#include "pch.h"

#include "F4TInventoryHooks.h"
#include "F4THookThreadTable.h"

#include "F4TClientCompatibility.h"
#include "F4TDebugLogging.h"
#include "F4TInventoryHookTable.h"
#include "F4TInventoryPickupGate.h"
#include "F4TVtableGuard.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <intrin.h>
#include <optional>
#include <string_view>
#include <type_traits>

namespace F4T::InventoryHooks
{
	namespace
	{
		// source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md (section 8 R-I1/R-I4: the caller address is logged at these hot entry points; the log line must be rate-limited)
		constexpr std::int64_t kObserverLogIntervalMs = 1000;

		using DropObjectFn = RE::ObjectRefHandle* (*)(RE::Actor*, RE::ObjectRefHandle*, const RE::BGSObjectInstance&, RE::BSTSmallArray<std::uint32_t, 4>*, std::int32_t, const RE::NiPoint3*, const RE::NiPoint3*);
		static_assert(!std::is_trivially_copyable_v<RE::ObjectRefHandle>, "DropObject returns ObjectRefHandle through a hidden buffer, so the buffer is the explicit a_result parameter");
		using PickUpObjectFn = void (*)(RE::Actor*, RE::TESObjectREFR*, std::int32_t, bool);
		using DoItemTransferFn = void (*)(RE::ContainerMenu*, std::uint32_t, std::uint32_t, bool);

		std::uintptr_t g_moduleBase{ 0 };
		std::uintptr_t g_dropObjectOriginal{ 0 };
		std::uintptr_t g_pickUpObjectOriginal{ 0 };
		std::uintptr_t g_doItemTransferOriginal{ 0 };
		std::atomic<std::int64_t> g_dropObjectLastLogMs{ 0 };
		std::atomic<std::int64_t> g_pickUpObjectLastLogMs{ 0 };
		std::atomic<std::int64_t> g_doItemTransferLastLogMs{ 0 };

		bool ShouldLog(std::atomic<std::int64_t>& a_lastMs) noexcept
		{
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
								 std::chrono::steady_clock::now().time_since_epoch())
								 .count();
			const auto last = a_lastMs.load(std::memory_order_relaxed);
			if (last != 0 && now - last < kObserverLogIntervalMs) {
				return false;
			}
			a_lastMs.store(now, std::memory_order_relaxed);
			return true;
		}

		RE::ObjectRefHandle* DropObjectHook(
			RE::Actor* a_this,
			RE::ObjectRefHandle* a_result,
			const RE::BGSObjectInstance& a_object,
			RE::BSTSmallArray<std::uint32_t, 4>* a_stackData,
			std::int32_t a_number,
			const RE::NiPoint3* a_point,
			const RE::NiPoint3* a_rotate)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::DropObject);
			const auto original = reinterpret_cast<DropObjectFn>(g_dropObjectOriginal);
			if (ShouldLog(g_dropObjectLastLogMs)) {
				// concept-level: observer only, must not throw into the engine
				try {
					const auto* form = a_object.object;
					F4T::DebugLogging::Log(
						"InventoryHook",
						"DropObject observed: thread {}, caller rva {:#x}, item form {:#010x}, count {}",
						REX::W32::GetCurrentThreadId(),
						reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase,
						form ? form->GetFormID() : 0,
						a_number);
				} catch (...) {
				}
			}
			return original(a_this, a_result, a_object, a_stackData, a_number, a_point, a_rotate);
		}

		void PickUpObjectHook(
			RE::Actor* a_this,
			RE::TESObjectREFR* a_objREFR,
			std::int32_t a_count,
			bool a_playPickUpSounds)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::PickUpObject);
			const auto original = reinterpret_cast<PickUpObjectFn>(g_pickUpObjectOriginal);
			const auto refFormId = a_objREFR ? a_objREFR->GetFormID() : 0;
			if (F4T::InventoryPickupGate::Hold(a_objREFR, a_count, a_playPickUpSounds, original)) {
				return;
			}
			original(a_this, a_objREFR, a_count, a_playPickUpSounds);
			if (ShouldLog(g_pickUpObjectLastLogMs)) {
				try {
					F4T::DebugLogging::Log(
						"InventoryHook",
						"PickUpObject observed: thread {}, caller rva {:#x}, ref form {:#010x}, count {}",
						REX::W32::GetCurrentThreadId(),
						reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase,
						refFormId,
						a_count);
				} catch (...) {
				}
			}
		}

		void DoItemTransferHook(
			RE::ContainerMenu* a_this,
			std::uint32_t a_itemIndex,
			std::uint32_t a_count,
			bool a_fromContainer)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::DoItemTransfer);
			const auto original = reinterpret_cast<DoItemTransferFn>(g_doItemTransferOriginal);
			if (ShouldLog(g_doItemTransferLastLogMs)) {
				try {
					F4T::DebugLogging::Log(
						"InventoryHook",
						"DoItemTransfer observed: thread {}, caller rva {:#x}, item index {}, requested count {}, from container {}",
						REX::W32::GetCurrentThreadId(),
						reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase,
						a_itemIndex,
						a_count,
						a_fromContainer);
				} catch (...) {
				}
			}
			original(a_this, a_itemIndex, a_count, a_fromContainer);
		}

		std::optional<F4T::ClientCompatibility::BuildTarget> VerifiedTarget()
		{
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			if (version.build() != 0) {
				return std::nullopt;
			}
			return F4T::ClientCompatibility::TargetFromRuntime(
				std::format("{}.{}.{}", version.major(), version.minor(), version.patch()));
		}

		const F4T::InventoryHookTable::RuntimeSlots& SlotsForTarget(F4T::ClientCompatibility::BuildTarget a_target)
		{
			using Target = F4T::ClientCompatibility::BuildTarget;
			switch (a_target) {
			case Target::OG:
				return F4T::InventoryHookTable::kOg;
			case Target::NG:
				return F4T::InventoryHookTable::kNg;
			default:
				return F4T::InventoryHookTable::kAe;
			}
		}

		bool InstallVtableSlot(
			std::uintptr_t a_vtableAddress,
			std::size_t a_slot,
			std::uintptr_t a_expectedRva,
			std::uintptr_t a_replacement,
			std::uintptr_t& a_original,
			std::string_view a_label)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtableAddress };
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");

			VtableGuard::SlotRequest request{};
			request.vtableAddress = vtable.address();
			request.slotIndex = a_slot;
			request.replacement = a_replacement;
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			const auto slotAddress = request.vtableAddress + (a_slot * sizeof(std::uintptr_t));
			request.slotValue = VtableGuard::ReadSlot(request);

			const auto verdict = VtableGuard::VerifySlot(request);
			if (verdict != VtableGuard::SlotVerdict::kOk) {
				REX::WARN("Inventory {} observer NOT installed ({}).", a_label, VtableGuard::Describe(verdict));
				return false;
			}

			if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, g_moduleBase, a_expectedRva)) {
				REX::WARN("Inventory {} observer NOT installed: the vtable slot does not hold this runtime's expected engine function.", a_label);
				return false;
			}

			a_original = request.slotValue;
			vtable.write_vfunc(a_slot, a_replacement);
			if (!VtableGuard::WriteSucceeded(*reinterpret_cast<const std::uintptr_t*>(slotAddress), request.replacement)) {
				REX::WARN("Inventory {} observer FAILED to install: vtable slot did not take the write.", a_label);
				return false;
			}
			F4T::DebugLogging::Log("InventoryHook", "{} observer installed on vtable slot {}", a_label, a_slot);
			return true;
		}
	}

	bool Install()
	{
		const auto target = VerifiedTarget();
		if (!target) {
			REX::WARN("Inventory boundary observers NOT installed: the game runtime is not a verified version.");
			return false;
		}

		const auto& slots = SlotsForTarget(*target);
		g_moduleBase = REX::FModule::GetExecutingModule().GetBaseAddress();
		const auto dropObject = InstallVtableSlot(
			g_moduleBase + slots.playerCharacterVtableRva,
			slots.dropObjectSlot,
			slots.dropObjectRva,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&DropObjectHook),
			g_dropObjectOriginal,
			"DropObject");
		const auto pickUpObject = InstallVtableSlot(
			g_moduleBase + slots.playerCharacterVtableRva,
			slots.pickUpObjectSlot,
			slots.pickUpObjectRva,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&PickUpObjectHook),
			g_pickUpObjectOriginal,
			"PickUpObject");
		const auto doItemTransfer = InstallVtableSlot(
			g_moduleBase + slots.containerMenuVtableRva,
			slots.doItemTransferSlot,
			slots.doItemTransferRva,
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&DoItemTransferHook),
			g_doItemTransferOriginal,
			"DoItemTransfer");
		return dropObject && pickUpObject && doItemTransfer;
	}
}
