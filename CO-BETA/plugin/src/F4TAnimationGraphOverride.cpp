// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/GraphOverride.{h,cpp}.
// source:commonlibf4 RE/I/IAnimationGraphManagerHolder.h; slot use verified against the shipped headers, no address trusted.
#include "pch.h"

#include "F4TAnimationGraphOverride.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TVtableGuard.h"

#include "RE/A/AIProcess.h"
#include "RE/A/Actor.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/M/MiddleHighProcessData.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <numbers>
#include <string_view>

namespace F4T::AnimationGraphOverride
{
	namespace
	{
		using Holder = RE::IAnimationGraphManagerHolder;
		using PreUpdate_t = void (*)(const Holder*, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>&);
		constexpr std::size_t kPreUpdateSlot = 0x16;

		using Notify_t = bool (*)(Holder*, const RE::BSFixedString&);
		constexpr std::size_t kNotifySlot = 0x01;

		struct Slot
		{
			std::atomic<const Holder*> holder{ nullptr };
			std::atomic<float> speed{ 0.0f };
			std::atomic<float> direction{ 0.0f };
			std::atomic<float> pitch{ 0.0f };
			std::atomic<bool> resync{ false };
		};

		std::array<Slot, 32> g_slots;

		PreUpdate_t g_original = nullptr;
		Notify_t g_originalNotify = nullptr;
		thread_local bool t_ours = false;

		struct OursScope
		{
			OursScope() { t_ours = true; }
			~OursScope() { t_ours = false; }
		};

		bool IsBodyEvent(Holder* a_self, const char* a_event, Slot*& a_outSlot)
		{
			a_outSlot = nullptr;
			if (t_ours || !a_event) {
				return false;
			}
			for (auto& candidate : g_slots) {
				if (candidate.holder.load(std::memory_order_acquire) == a_self) {
					a_outSlot = &candidate;
					return true;
				}
			}
			return false;
		}

		bool DropBodyEngineEvent(const char* a_event)
		{
			return _stricmp(a_event, "moveStop") == 0 || _stricmp(a_event, "SprintStop") == 0 ||
			       _stricmp(a_event, "sneakStop") == 0 || _stricmp(a_event, "sneakStart") == 0;
		}

		void Hook(const Holder* a_self, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_mgr)
		{
			g_original(a_self, a_mgr);
			for (auto& s : g_slots) {
				if (s.holder.load(std::memory_order_acquire) != a_self) {
					continue;
				}
				auto* holder = const_cast<Holder*>(a_self);
				static const RE::BSFixedString kSpeed{ "Speed" };
				static const RE::BSFixedString kSpeedSmoothed{ "SpeedSmoothed" };
				static const RE::BSFixedString kDirection{ "Direction" };
				static const RE::BSFixedString kDirectionDegrees{ "DirectionDegrees" };
				static const RE::BSFixedString kPitch{ "Pitch" };
				const float speed = s.speed.load(std::memory_order_relaxed);
				const float dir = s.direction.load(std::memory_order_relaxed);
				holder->SetGraphVariableFloat(kSpeed, speed);
				holder->SetGraphVariableFloat(kSpeedSmoothed, speed);
				holder->SetGraphVariableFloat(kDirection, dir);
				holder->SetGraphVariableFloat(kDirectionDegrees, (dir > 0.5F ? dir - 1.0F : dir) * 360.0F);
				holder->SetGraphVariableFloat(kPitch, s.pitch.load(std::memory_order_relaxed));
				return;
			}
		}

		bool NotifyHook(Holder* a_self, const RE::BSFixedString& a_event)
		{
			Slot* slot = nullptr;
			if (IsBodyEvent(a_self, a_event.c_str(), slot)) {
				if (DropBodyEngineEvent(a_event.c_str())) {
					return true;
				}
				const bool ok = g_originalNotify(a_self, a_event);
				slot->resync.store(true, std::memory_order_release);
				return ok;
			}
			return g_originalNotify(a_self, a_event);
		}

		const Holder* HolderOf(RE::Actor* a_actor)
		{
			return a_actor ? static_cast<const Holder*>(a_actor) : nullptr;
		}

		Slot* SlotOf(const Holder* a_holder)
		{
			for (auto& s : g_slots) {
				if (s.holder.load(std::memory_order_acquire) == a_holder) {
					return &s;
				}
			}
			return nullptr;
		}

		bool ReadableBytes(const void* a_address, std::size_t a_size)
		{
			if (!a_address || a_size == 0) {
				return false;
			}
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(a_address, &info, sizeof(info)) == 0) {
				return false;
			}
			if (info.State != MEM_COMMIT) {
				return false;
			}
			if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
				return false;
			}
			const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
			const auto end = base + info.RegionSize;
			const auto want = static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(a_address));
			return want >= base && want + a_size <= end;
		}

		bool PooledEntryReadable(const RE::BSStringPool::Entry* a_entry)
		{
			const auto* iter = a_entry;
			for (std::size_t hop = 0; hop < 8; ++hop) {
				if (!iter || !ReadableBytes(iter, sizeof(*iter))) {
					return false;
				}
				if (!iter->shallow()) {
					return true;
				}
				iter = iter->_right;
			}
			return false;
		}

		bool ChannelNameEquals(void* a_channel, const char* a_name)
		{
			constexpr std::size_t kChannelNameOffset = 0x10;
			const auto* field = static_cast<const char*>(a_channel) + kChannelNameOffset;
			if (!ReadableBytes(field, sizeof(RE::BSFixedString))) {
				return false;
			}
			void* raw = nullptr;
			std::memcpy(&raw, field, sizeof(raw));
			if (!raw || !PooledEntryReadable(static_cast<const RE::BSStringPool::Entry*>(raw))) {
				return false;
			}
			const char* name = reinterpret_cast<const RE::BSFixedString*>(field)->c_str();
			if (!name || !ReadableBytes(name, 1)) {
				return false;
			}
			return _stricmp(name, a_name) == 0;
		}

		bool MatchesAny(void* a_channel, std::initializer_list<const char*> a_names)
		{
			if (!a_channel) {
				return false;
			}
			for (const auto* name : a_names) {
				if (ChannelNameEquals(a_channel, name)) {
					return true;
				}
			}
			return false;
		}

		void Detach(RE::Actor* a_actor, std::initializer_list<const char*> a_names)
		{
			if (!a_actor) {
				return;
			}
			auto* mgr = a_actor->currentProcess && a_actor->currentProcess->middleHigh
				? a_actor->currentProcess->middleHigh->animationGraphManager.get()
				: nullptr;
			if (!mgr) {
				return;
			}

			constexpr std::size_t kBoundChannelOffset = 0x10;
			struct RawArray
			{
				void** data;
				std::uint32_t capacity;
				std::uint32_t pad;
				std::uint32_t size;
			};
			std::uint32_t removed = 0;
			{
				RE::BSAutoLock lock{ mgr->updateLock };
				auto* arr = reinterpret_cast<RawArray*>(reinterpret_cast<char*>(mgr) + kBoundChannelOffset);
				if (!ReadableBytes(arr, sizeof(*arr))) {
					return;
				}
				if (!arr->data || arr->size > 256) {
					F4T::DebugLogging::Log("GraphOverride", "bound channels look wrong (size {}), not touching them", arr->size);
					return;
				}
				if (!ReadableBytes(arr->data, arr->size * sizeof(void*))) {
					return;
				}
				for (std::uint32_t i = 0; i < arr->size; ++i) {
					if (MatchesAny(arr->data[i], a_names)) {
						++removed;
					}
				}
				if (removed == 0) {
					return;
				}
				std::uint32_t kept = 0;
				for (std::uint32_t i = 0; i < arr->size; ++i) {
					if (!MatchesAny(arr->data[i], a_names)) {
						arr->data[kept++] = arr->data[i];
					}
				}
				arr->size = kept;
			}
			F4T::DebugLogging::Log(
				"GraphOverride", "actor={:08X}: unbound {} movement channels", a_actor->GetFormID(), removed);
		}

		std::atomic<bool> g_hookInstalled{ false };
		std::atomic<bool> g_installRefused{ false };

		VtableGuard::SlotRequest MakeRequest(
			std::uintptr_t a_vtableAddress,
			std::size_t a_slot,
			std::uintptr_t a_replacement,
			const VtableGuard::AddressRange& a_code,
			const VtableGuard::AddressRange& a_vtable)
		{
			VtableGuard::SlotRequest request{};
			request.vtableAddress = a_vtableAddress;
			request.slotIndex = a_slot;
			request.replacement = a_replacement;
			request.codeSection = a_code;
			request.vtableSection = a_vtable;
			request.slotValue = VtableGuard::ReadSlot(request);
			return request;
		}

		void LogSlotRvas(const char* a_what, std::uintptr_t a_preUpdate, std::uintptr_t a_notify, std::uintptr_t a_base)
		{
			const auto rva = [](std::uintptr_t a_fn, std::uintptr_t a_baseAddr) -> std::uintptr_t {
				return a_fn > a_baseAddr ? a_fn - a_baseAddr : 0;
			};
			F4T::DebugLogging::Log(
				"GraphOverride",
				"{} preUpdate slot RVA={:#x} notify slot RVA={:#x}",
				a_what,
				rva(a_preUpdate, a_base),
				rva(a_notify, a_base));
		}
	}

	bool Install(RE::Actor* a_anyBody)
	{
		if (g_hookInstalled || g_installRefused || !a_anyBody) {
			return g_hookInstalled;
		}

		const auto module = REX::FModule::GetExecutingModule();
		const auto moduleBase = module.GetBaseAddress();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");
		const VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
		const VtableGuard::AddressRange vtableSection{ rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };

		const auto vtbl = *reinterpret_cast<std::uintptr_t**>(const_cast<Holder*>(HolderOf(a_anyBody)));
		const auto preUpdateRequest =
			MakeRequest(reinterpret_cast<std::uintptr_t>(vtbl), kPreUpdateSlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&Hook), codeSection, vtableSection);
		const auto notifyRequest =
			MakeRequest(reinterpret_cast<std::uintptr_t>(vtbl), kNotifySlot, REX::UNRESTRICTED_CAST<std::uintptr_t>(&NotifyHook), codeSection, vtableSection);

		LogSlotRvas("probe", preUpdateRequest.slotValue, notifyRequest.slotValue, moduleBase);

		if (VtableGuard::VerifySlot(preUpdateRequest) != VtableGuard::SlotVerdict::kOk) {
			REX::WARN(std::string_view{ "GraphOverride NOT installed (pre-update slot refused)." });
			g_installRefused = true;
			return false;
		}
		if (VtableGuard::VerifySlot(notifyRequest) != VtableGuard::SlotVerdict::kOk) {
			REX::WARN(std::string_view{ "GraphOverride NOT installed (notify slot refused)." });
			g_installRefused = true;
			return false;
		}

		g_original = reinterpret_cast<PreUpdate_t>(preUpdateRequest.slotValue);
		g_originalNotify = reinterpret_cast<Notify_t>(notifyRequest.slotValue);
		REL::Relocation<std::uintptr_t> holderVtable{ reinterpret_cast<std::uintptr_t>(vtbl) };
		holderVtable.write_vfunc(kPreUpdateSlot, &Hook);
		holderVtable.write_vfunc(kNotifySlot, &NotifyHook);

		g_hookInstalled =
			g_original != nullptr && g_originalNotify != nullptr &&
			VtableGuard::WriteSucceeded(
				*reinterpret_cast<std::uintptr_t*>(&vtbl[kPreUpdateSlot]), REX::UNRESTRICTED_CAST<std::uintptr_t>(&Hook)) &&
			VtableGuard::WriteSucceeded(
				*reinterpret_cast<std::uintptr_t*>(&vtbl[kNotifySlot]), REX::UNRESTRICTED_CAST<std::uintptr_t>(&NotifyHook));
		if (g_hookInstalled) {
			F4T::DebugLogging::Write("GraphOverride", "hooked Character graph pre-update + events");
		} else {
			g_installRefused = true;
			REX::WARN(std::string_view{ "GraphOverride FAILED to install: a vtable slot did not take the write." });
		}
		return g_hookInstalled;
	}

	bool Installed()
	{
		return g_hookInstalled;
	}

	void DetachMovementChannels(RE::Actor* a_body)
	{
		Detach(a_body, { "Speed", "Direction", "SpeedSmoothed", "DirectionDegrees", "Pitch" });
	}

	void SetProxySpeed(RE::Actor* a_body, float a_speed, float a_directionRadians)
	{
		const Holder* h = HolderOf(a_body);
		if (!h) {
			return;
		}
		Slot* free = nullptr;
		for (auto& s : g_slots) {
			const Holder* cur = s.holder.load(std::memory_order_relaxed);
			if (cur == h) {
				free = &s;
				break;
			}
			if (!cur && !free) {
				free = &s;
			}
		}
		if (!free) {
			return;
		}
		auto dir = std::isfinite(a_directionRadians) ? a_directionRadians / (2.0F * std::numbers::pi_v<float>) : 0.0F;
		dir -= std::floor(dir);
		if (free->holder.load(std::memory_order_relaxed) != h) {
			free->pitch.store(0.0F, std::memory_order_relaxed);
			free->resync.store(false, std::memory_order_relaxed);
		}
		free->speed.store(std::isfinite(a_speed) && a_speed >= 0.0F ? a_speed : 0.0F, std::memory_order_relaxed);
		free->direction.store(dir, std::memory_order_relaxed);
		free->holder.store(h, std::memory_order_release);
	}

	void SetProxyPitch(RE::Actor* a_body, float a_pitchDegrees)
	{
		if (Slot* s = SlotOf(HolderOf(a_body))) {
			s->pitch.store(std::isfinite(a_pitchDegrees) ? a_pitchDegrees : 0.0F, std::memory_order_relaxed);
		}
	}

	void ClearProxy(RE::Actor* a_body)
	{
		if (Slot* s = SlotOf(HolderOf(a_body))) {
			s->holder.store(nullptr, std::memory_order_release);
		}
	}

	bool SendGraphEvent(RE::Actor* a_body, const char* a_event)
	{
		const OursScope ours;
		return a_body && a_body->NotifyAnimationGraphImpl(RE::BSFixedString{ a_event });
	}

	bool TakeProxyResync(RE::Actor* a_body)
	{
		Slot* slot = SlotOf(HolderOf(a_body));
		return slot && slot->resync.exchange(false, std::memory_order_acq_rel);
	}
}
