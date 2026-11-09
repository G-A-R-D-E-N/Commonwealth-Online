#include "pch.h"

#include "F4TProxyAttackHitGuard.h"
#include "F4THookThreadTable.h"
#include "F4TClientCompatibility.h"
#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TProxyLiveFireGuard.h"
#include "F4TProxyPuppet.h"
#include "F4TVtableGuard.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ProxyAttackHitGuard
{
	namespace
	{
		constexpr std::size_t kExecuteHandlerVtableIndex = 1;

		using ExecuteHandlerFn = bool (*)(void*, RE::Actor&, RE::BSFixedString&);

		ExecuteHandlerFn g_originalHitFrameHandler = nullptr;
		ExecuteHandlerFn g_originalWeaponFireHandler = nullptr;
		std::atomic<bool> g_hookInstalled{ false };

		struct HandlerRvas
		{
			std::uintptr_t hitFrame;
			std::uintptr_t weaponFire;
		};

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/findings.jsonl (w4-n73-hitframe-og-v2, w4-n73-weaponfire-gate-og: expected RVA)
		constexpr HandlerRvas kOgRvas{ 0xF6B920, 0xF6E370 };
		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/findings.jsonl (w4-n73-hitframe-ng-v2, w4-n73-weaponfire-gate-ng: expected RVA)
		constexpr HandlerRvas kNgRvas{ 0xD76D40, 0xD79B90 };
		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/findings.jsonl (w4-n73-hitframe-ae-v2, w4-n73-weaponfire-gate-ae-v2: expected RVA)
		constexpr HandlerRvas kAeRvas{ 0xDFCD90, 0xDFFBE0 };

		std::optional<HandlerRvas> ExpectedRvasForRuntime()
		{
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			if (version.build() != 0) {
				return std::nullopt;
			}
			const auto target = F4T::ClientCompatibility::TargetFromRuntime(
				std::format("{}.{}.{}", version.major(), version.minor(), version.patch()));
			if (!target) {
				return std::nullopt;
			}
			using Target = F4T::ClientCompatibility::BuildTarget;
			switch (*target) {
			case Target::OG:
				return kOgRvas;
			case Target::NG:
				return kNgRvas;
			default:
				return kAeRvas;
			}
		}

		bool IsProxyActor(RE::Actor& a_actor)
		{
			try {
				return ProxyPuppet::IsProxyFormID(a_actor.GetFormID());
			} catch (...) {
				return false;
			}
		}

		bool HitFrameHandlerHook(void* a_this, RE::Actor& a_actor, RE::BSFixedString& a_payload)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::HitFrame);
			if (IsProxyActor(a_actor)) {
				return false;
			}
			return g_originalHitFrameHandler(a_this, a_actor, a_payload);
		}

		bool WeaponFireHandlerHook(void* a_this, RE::Actor& a_actor, RE::BSFixedString& a_payload)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::WeaponFire);
			if (IsProxyActor(a_actor) && !F4T::ProxyLiveFire::Arm(&a_actor)) {
				return true;
			}
			return g_originalWeaponFireHandler(a_this, a_actor, a_payload);
		}

		std::uintptr_t ReadExecuteHandlerSlot(std::uintptr_t a_vtableAddress)
		{
			return *reinterpret_cast<std::uintptr_t*>(
				a_vtableAddress + (kExecuteHandlerVtableIndex * sizeof(std::uintptr_t)));
		}

		VtableGuard::SlotRequest MakeSlotRequest(
			std::uintptr_t a_vtableAddress,
			std::uintptr_t a_replacement,
			const VtableGuard::AddressRange& a_codeSection,
			const VtableGuard::AddressRange& a_vtableSection)
		{
			VtableGuard::SlotRequest request{};
			request.vtableAddress = a_vtableAddress;
			request.slotIndex = kExecuteHandlerVtableIndex;
			request.replacement = a_replacement;
			request.codeSection = a_codeSection;
			request.vtableSection = a_vtableSection;
			if (request.vtableAddress != 0) {
				request.slotValue = ReadExecuteHandlerSlot(request.vtableAddress);
			}
			return request;
		}

		void RestoreSlotIfReplaced(
			REL::Relocation<std::uintptr_t>& a_vtable,
			const VtableGuard::SlotRequest& a_request,
			ExecuteHandlerFn a_original)
		{
			if (a_original != nullptr && ReadExecuteHandlerSlot(a_request.vtableAddress) == a_request.replacement) {
				a_vtable.write_vfunc(kExecuteHandlerVtableIndex, a_original);
			}
		}

		void LogRefusal(std::string_view a_handler, VtableGuard::SlotVerdict a_verdict)
		{
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" proxy attack hit guard NOT installed (" + std::string{ a_handler } + " slot: " +
				std::string{ VtableGuard::Describe(a_verdict) } +
				"). Neither slot was written; proxy attack animation replay is refused this session.";
			REX::WARN(std::string_view{ refusal });
		}

		void LogIdentityRefusal(std::string_view a_handler)
		{
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" proxy attack hit guard NOT installed: the " + std::string{ a_handler } +
				" vtable slot does not hold this runtime's expected engine function. Neither slot was written; proxy attack animation replay is refused this session.";
			REX::WARN(std::string_view{ refusal });
		}
	}

	void Install()
	{
		if (g_hookInstalled) {
			return;
		}

		const auto expected = ExpectedRvasForRuntime();
		if (!expected) {
			REX::WARN("proxy attack hit guard NOT installed: the game runtime is not a verified version. Proxy attack animation replay is refused this session.");
			return;
		}

		const auto module = REX::FModule::GetExecutingModule();
		const auto moduleBase = module.GetBaseAddress();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");
		const VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
		const VtableGuard::AddressRange vtableSection{ rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (co309-npc6-hitframehandler-vtable-is-tier2: vtable address id 846835, vslot 1)
		REL::Relocation<std::uintptr_t> hitFrameVtable{ RE::VTABLE::HitFrameHandler[0] };
		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (w4-n73-weaponfire-gate-{og,ng,ae}: vtable address id 1548757, vslot 1)
		REL::Relocation<std::uintptr_t> weaponFireVtable{ RE::VTABLE::WeaponFireHandler[0] };

		const auto hitFrameRequest = MakeSlotRequest(
			hitFrameVtable.address(),
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&HitFrameHandlerHook),
			codeSection,
			vtableSection);
		const auto hitFrameVerdict = VtableGuard::VerifySlot(hitFrameRequest);
		if (hitFrameVerdict != VtableGuard::SlotVerdict::kOk) {
			LogRefusal("HitFrameHandler", hitFrameVerdict);
			return;
		}
		if (!VtableGuard::SlotMatchesExpectedRva(hitFrameRequest.slotValue, moduleBase, expected->hitFrame)) {
			LogIdentityRefusal("HitFrameHandler");
			return;
		}

		const auto weaponFireRequest = MakeSlotRequest(
			weaponFireVtable.address(),
			REX::UNRESTRICTED_CAST<std::uintptr_t>(&WeaponFireHandlerHook),
			codeSection,
			vtableSection);
		const auto weaponFireVerdict = VtableGuard::VerifySlot(weaponFireRequest);
		if (weaponFireVerdict != VtableGuard::SlotVerdict::kOk) {
			LogRefusal("WeaponFireHandler", weaponFireVerdict);
			return;
		}
		if (!VtableGuard::SlotMatchesExpectedRva(weaponFireRequest.slotValue, moduleBase, expected->weaponFire)) {
			LogIdentityRefusal("WeaponFireHandler");
			return;
		}

		g_originalHitFrameHandler = reinterpret_cast<ExecuteHandlerFn>(hitFrameRequest.slotValue);
		hitFrameVtable.write_vfunc(kExecuteHandlerVtableIndex, &HitFrameHandlerHook);

		g_originalWeaponFireHandler = reinterpret_cast<ExecuteHandlerFn>(weaponFireRequest.slotValue);
		weaponFireVtable.write_vfunc(kExecuteHandlerVtableIndex, &WeaponFireHandlerHook);

		const bool hitFrameInstalled =
			g_originalHitFrameHandler != nullptr &&
			VtableGuard::WriteSucceeded(ReadExecuteHandlerSlot(hitFrameRequest.vtableAddress), hitFrameRequest.replacement);
		const bool weaponFireInstalled =
			g_originalWeaponFireHandler != nullptr &&
			VtableGuard::WriteSucceeded(ReadExecuteHandlerSlot(weaponFireRequest.vtableAddress), weaponFireRequest.replacement);

		g_hookInstalled = hitFrameInstalled && weaponFireInstalled;
		if (!g_hookInstalled) {
			RestoreSlotIfReplaced(hitFrameVtable, hitFrameRequest, g_originalHitFrameHandler);
			RestoreSlotIfReplaced(weaponFireVtable, weaponFireRequest, g_originalWeaponFireHandler);
		}

		const auto message =
			F4T::Networking::GetLocalPlayerLogPrefix() +
			(g_hookInstalled
					? " proxy attack hit guard installed: HitFrameHandler is skipped for proxies; WeaponFireHandler fires live only with proxy damage suppression, otherwise it is skipped."
					: " proxy attack hit guard FAILED to install: a vtable slot did not take the write.");
		if (g_hookInstalled) {
			F4T::DebugLogging::Write("ProxyAttackHitGuard", message);
		} else {
			REX::WARN(std::string_view{ message });
		}
	}

	bool IsInstalled()
	{
		return g_hookInstalled;
	}
}
