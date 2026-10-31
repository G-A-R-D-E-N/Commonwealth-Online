// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/GraphOverride.cpp (InstallNoTalk).
// TODO-verify(#568) source:E:/GardenEngine/re-answers/2026-10-27-fs-phase1-deferred-facts.md
#include "pch.h"

#include "F4TProxyNoTalk.h"

#include "F4TClientCompatibility.h"
#include "F4TDebugLogging.h"
#include "F4THookThreadTable.h"
#include "F4TProxyPuppet.h"
#include "F4TVtableGuard.h"

#include "RE/T/TESBoundObject.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectREFR.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ProxyNoTalk
{
	namespace
	{
		constexpr std::size_t kActivateSlot = 0x40;
		constexpr std::size_t kGetActivateTextSlot = 0x60;

		using Activate_t = bool (*)(RE::TESForm*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESBoundObject*, std::int32_t);
		using GetActivateText_t = bool (*)(RE::TESBoundObject*, RE::TESObjectREFR*, RE::BSString&);

		Activate_t g_originalActivate = nullptr;
		GetActivateText_t g_originalGetActivateText = nullptr;
		std::atomic<bool> g_installed{ false };
		std::atomic<bool> g_refused{ false };

		struct NoTalkRvas
		{
			std::uintptr_t activate;
			std::uintptr_t getActivateText;
		};

		// TODO-verify(#568) source:E:/GardenEngine/re-answers/2026-10-27-fs-phase1-deferred-facts.md
		constexpr NoTalkRvas kOgRvas{ 0x5B6050, 0x5B6FE0 };
		// TODO-verify(#568) source:E:/GardenEngine/re-answers/2026-10-27-fs-phase1-deferred-facts.md
		constexpr NoTalkRvas kNgRvas{ 0x602090, 0x603090 };
		// TODO-verify(#568) source:E:/GardenEngine/re-answers/2026-10-27-fs-phase1-deferred-facts.md
		constexpr NoTalkRvas kAeRvas{ 0x655D60, 0x656D60 };

		std::optional<NoTalkRvas> ExpectedRvasForRuntime()
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

		bool IsBodyRef(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return false;
			}
			try {
				return ProxyPuppet::IsProxyFormID(a_ref->GetFormID());
			} catch (...) {
				return false;
			}
		}

		// TODO-verify(#568): Activate parameter list (this, itemActivated, actionRef, objectToGet, count) is unverified at runtime.
		bool ActivateHook(
			RE::TESForm* a_self,
			RE::TESObjectREFR* a_ref,
			RE::TESObjectREFR* a_actionRef,
			RE::TESBoundObject* a_objectToGet,
			std::int32_t a_count)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ProxyActivate);
			if (IsBodyRef(a_ref)) {
				return true;
			}
			return g_originalActivate(a_self, a_ref, a_actionRef, a_objectToGet, a_count);
		}

		bool GetActivateTextHook(RE::TESBoundObject* a_self, RE::TESObjectREFR* a_ref, RE::BSString& a_out)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ProxyGetActivateText);
			if (IsBodyRef(a_ref)) {
				return false;
			}
			return g_originalGetActivateText(a_self, a_ref, a_out);
		}

		template <class Fn>
		void RestoreSlotIfReplaced(REL::Relocation<std::uintptr_t>& a_table, const VtableGuard::SlotRequest& a_request, Fn a_original)
		{
			if (a_original != nullptr && VtableGuard::ReadSlot(a_request) == a_request.replacement) {
				a_table.write_vfunc(a_request.slotIndex, a_original);
			}
		}

		VtableGuard::SlotRequest MakeRequest(
			std::uintptr_t a_vtableAddress,
			std::size_t a_slot,
			std::uintptr_t a_replacement,
			const VtableGuard::AddressRange& a_codeSection,
			const VtableGuard::AddressRange& a_vtableSection)
		{
			VtableGuard::SlotRequest request{};
			request.vtableAddress = a_vtableAddress;
			request.slotIndex = a_slot;
			request.replacement = a_replacement;
			request.codeSection = a_codeSection;
			request.vtableSection = a_vtableSection;
			request.slotValue = VtableGuard::ReadSlot(request);
			return request;
		}
	}

	bool Install(RE::Actor* a_proxy)
	{
		if (g_installed || g_refused || !a_proxy) {
			return g_installed;
		}
		auto* base = a_proxy->GetObjectReference();
		if (!base || !base->As<RE::TESNPC>()) {
			return false;
		}

		const auto expected = ExpectedRvasForRuntime();
		if (!expected) {
			REX::WARN(std::string_view{ "ProxyNoTalk NOT installed: the game runtime is not a verified version." });
			g_refused = true;
			return false;
		}

		const auto module = REX::FModule::GetExecutingModule();
		const auto moduleBase = module.GetBaseAddress();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");
		const VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
		const VtableGuard::AddressRange vtableSection{ rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };

		const auto vtableAddress = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<std::uintptr_t**>(base));
		REL::Relocation<std::uintptr_t> table{ vtableAddress };
		const auto activateRequest = MakeRequest(
			vtableAddress, kActivateSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&ActivateHook), codeSection, vtableSection);
		const auto textRequest = MakeRequest(
			vtableAddress, kGetActivateTextSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&GetActivateTextHook), codeSection, vtableSection);

		const auto refuse = [](std::string_view a_slot) {
			const auto message = std::string{ "ProxyNoTalk NOT installed (" } + std::string{ a_slot } + " slot refused). Proxies keep the NPC prompt.";
			REX::WARN(std::string_view{ message });
			g_refused = true;
		};
		if (VtableGuard::VerifySlot(activateRequest) != VtableGuard::SlotVerdict::kOk) {
			refuse("Activate");
			return false;
		}
		if (VtableGuard::VerifySlot(textRequest) != VtableGuard::SlotVerdict::kOk) {
			refuse("GetActivateText");
			return false;
		}
		if (!VtableGuard::SlotMatchesExpectedRva(activateRequest.slotValue, moduleBase, expected->activate) ||
		    !VtableGuard::SlotMatchesExpectedRva(textRequest.slotValue, moduleBase, expected->getActivateText)) {
			refuse("identity");
			return false;
		}

		g_originalActivate = reinterpret_cast<Activate_t>(activateRequest.slotValue);
		g_originalGetActivateText = reinterpret_cast<GetActivateText_t>(textRequest.slotValue);
		table.write_vfunc(kActivateSlot, &ActivateHook);
		table.write_vfunc(kGetActivateTextSlot, &GetActivateTextHook);

		const auto slotAt = [vtableAddress](std::size_t a_slot) {
			return *reinterpret_cast<std::uintptr_t*>(vtableAddress + a_slot * sizeof(std::uintptr_t));
		};
		g_installed =
			VtableGuard::WriteSucceeded(slotAt(kActivateSlot), activateRequest.replacement) &&
			VtableGuard::WriteSucceeded(slotAt(kGetActivateTextSlot), textRequest.replacement);
		if (g_installed) {
			F4T::DebugLogging::Write("ProxyNoTalk", "proxy bodies: no Talk prompt and no activation");
		} else {
			RestoreSlotIfReplaced(table, activateRequest, g_originalActivate);
			RestoreSlotIfReplaced(table, textRequest, g_originalGetActivateText);
			g_refused = true;
			REX::WARN(std::string_view{ "ProxyNoTalk FAILED to install: a TESNPC vtable slot did not take the write." });
		}
		return g_installed;
	}

	bool Installed()
	{
		return g_installed;
	}
}
