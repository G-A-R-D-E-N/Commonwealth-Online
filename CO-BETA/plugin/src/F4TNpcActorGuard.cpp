#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcActorGuard.h"
#include "F4THookThreadTable.h"
#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TNetworking.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TProxyPuppet.h"
#include "F4TVtableGuard.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace F4T::NpcActorGuard
{
	namespace
	{
		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3: HandleHealthDamage vslot 0x110)
		constexpr std::size_t kHandleHealthDamageVtableIndex = 0x110;
		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 N4.5: KillImpl vslot 0x117, w3-n45-killimpl-body-{og,ng,ae} VERIFIED)
		constexpr std::size_t kKillImplVtableIndex = 0x117;

		using HandleHealthDamageFn = void (*)(RE::Actor*, RE::Actor*, float);
		using KillImplResult = std::invoke_result_t<decltype(&RE::Actor::KillImpl), RE::Actor&, RE::Actor*, float, bool, bool>;
		static_assert(std::is_void_v<KillImplResult>);
		using KillImplFn = void (*)(RE::Actor*, RE::Actor*, float, bool, bool);

		HandleHealthDamageFn g_originalHandleHealthDamage = nullptr;
		KillImplFn g_originalKillFn = nullptr;
		std::atomic<bool> g_hookInstalled{ false };

		thread_local int t_coWriteDepth = 0;
		thread_local int t_coKillDepth = 0;

		bool IsGuardedFormId(std::uint32_t a_formId)
		{
			try {
				return NpcSync::IsPuppetFormId(a_formId) || ProxyPuppet::IsProxyFormID(a_formId);
			} catch (...) {
				return false;
			}
		}

		void HandleHealthDamageHook(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::HandleHealthDamage);
			if (a_this && IsGuardedFormId(a_this->GetFormID()) && !IsCoWriteActive()) {
				return;
			}
			if (!g_originalHandleHealthDamage) {
				REX::WARN(std::string_view{ "NPC actor guard: HandleHealthDamage original is unresolved; skipping this call." });
				return;
			}
			g_originalHandleHealthDamage(a_this, a_attacker, a_damage);
		}

		void KillHook(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage, bool a_sendEvent, bool a_ragdollInstant)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::Kill);
			if (a_this && IsGuardedFormId(a_this->GetFormID()) && !IsCoWriteActive() && !IsCoKillActive()) {
				return;
			}
			if (!g_originalKillFn) {
				REX::WARN(std::string_view{ "NPC actor guard: KillImpl original is unresolved; skipping this call." });
				return;
			}
			g_originalKillFn(a_this, a_attacker, a_damage, a_sendEvent, a_ragdollInstant);
		}
	}

	bool Install()
	{
		if (g_hookInstalled) {
			return true;
		}

		const auto target = ExecutingRuntime::Target();
		if (!target) {
			REX::WARN("NPC actor guard NOT installed: the game runtime is not one of the supported OG, NG or AE builds.");
			return false;
		}

		REL::Relocation<std::uintptr_t> actorVtable{ RE::Actor::VTABLE[0] };

		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");

		VtableGuard::SlotRequest request{};
		request.vtableAddress = actorVtable.address();
		request.slotIndex = kHandleHealthDamageVtableIndex;
		request.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&HandleHealthDamageHook);
		request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
		request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
		request.slotValue = VtableGuard::ReadSlot(request);

		const auto verdict = VtableGuard::VerifySlot(request);
		if (verdict != VtableGuard::SlotVerdict::kOk) {
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" NPC actor guard NOT installed (" + std::string{ VtableGuard::Describe(verdict) } +
				"). NPC health damage is not guarded this session.";
			REX::WARN(std::string_view{ refusal });
			return false;
		}

		VtableGuard::SlotRequest killRequest{};
		killRequest.vtableAddress = actorVtable.address();
		killRequest.slotIndex = kKillImplVtableIndex;
		killRequest.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&KillHook);
		killRequest.codeSection = request.codeSection;
		killRequest.vtableSection = request.vtableSection;
		killRequest.slotValue = VtableGuard::ReadSlot(killRequest);

		const auto killVerdict = VtableGuard::VerifySlot(killRequest);
		if (killVerdict != VtableGuard::SlotVerdict::kOk) {
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" NPC actor guard NOT installed (KillImpl slot: " + std::string{ VtableGuard::Describe(killVerdict) } +
				"). Neither slot was written; NPC health damage and kills are not guarded this session.";
			REX::WARN(std::string_view{ refusal });
			return false;
		}

		// source:RE-Work/subsystems/commonwealth-online-re/npc-coop-co255/findings-hooks.jsonl (hooks-slot-0x110-{og,ng,ae}: expected RVA)
		const auto handleHealthRva = ExecutingRuntime::RvaFor(*target, 0xE01460, 0xC4CB10, 0xCD2AE0);
		// source:RE-Work/subsystems/commonwealth-online-re/npc-coop-co255/findings-damage.jsonl (damage-killimpl-entry-{og,ng,ae}: expected RVA)
		const auto killImplRva = ExecutingRuntime::RvaFor(*target, 0xDB27B0, 0xC1A0F0, 0xCA00C0);
		const auto moduleBase = module.GetBaseAddress();
		const bool handleHealthMatches = VtableGuard::SlotMatchesExpectedRva(request.slotValue, moduleBase, handleHealthRva);
		if (!handleHealthMatches || !VtableGuard::SlotMatchesExpectedRva(killRequest.slotValue, moduleBase, killImplRva)) {
			const auto refusal =
				F4T::Networking::GetLocalPlayerLogPrefix() +
				" NPC actor guard NOT installed: the " + std::string{ handleHealthMatches ? "KillImpl" : "HandleHealthDamage" } +
				" vtable slot does not hold this runtime's expected engine function. Neither slot was written; NPC health damage and kills are not guarded this session.";
			REX::WARN(std::string_view{ refusal });
			return false;
		}

		g_originalHandleHealthDamage = reinterpret_cast<HandleHealthDamageFn>(request.slotValue);
		actorVtable.write_vfunc(kHandleHealthDamageVtableIndex, &HandleHealthDamageHook);

		g_originalKillFn = reinterpret_cast<KillImplFn>(killRequest.slotValue);
		actorVtable.write_vfunc(kKillImplVtableIndex, &KillHook);

		const auto slotAfterWrite = *reinterpret_cast<std::uintptr_t*>(
			request.vtableAddress + (kHandleHealthDamageVtableIndex * sizeof(std::uintptr_t)));
		const bool handleHealthInstalled =
			g_originalHandleHealthDamage != nullptr &&
			VtableGuard::WriteSucceeded(slotAfterWrite, request.replacement);

		const auto killSlotAfterWrite = *reinterpret_cast<std::uintptr_t*>(
			killRequest.vtableAddress + (kKillImplVtableIndex * sizeof(std::uintptr_t)));
		const bool killInstalled =
			g_originalKillFn != nullptr &&
			VtableGuard::WriteSucceeded(killSlotAfterWrite, killRequest.replacement);

		g_hookInstalled = handleHealthInstalled && killInstalled;

		const auto message =
			F4T::Networking::GetLocalPlayerLogPrefix() +
			(g_hookInstalled
					? " NPC actor guard installed: HandleHealthDamage and KillImpl are held for puppets and proxies outside ScopedCoWrite and ScopedCoKill."
					: " NPC actor guard FAILED to install: vtable slot did not take the write.");
		if (g_hookInstalled) {
			F4T::DebugLogging::Write("NpcActorGuard", message);
		} else {
			REX::WARN(std::string_view{ message });
		}

		return g_hookInstalled;
	}

	bool IsInstalled()
	{
		return g_hookInstalled;
	}

	bool IsCoWriteActive()
	{
		return t_coWriteDepth > 0;
	}

	ScopedCoWrite::ScopedCoWrite()
	{
		++t_coWriteDepth;
	}

	ScopedCoWrite::~ScopedCoWrite()
	{
		--t_coWriteDepth;
	}

	bool IsCoKillActive()
	{
		return t_coKillDepth > 0;
	}

	ScopedCoKill::ScopedCoKill()
	{
		++t_coKillDepth;
	}

	ScopedCoKill::~ScopedCoKill()
	{
		--t_coKillDepth;
	}
}
