#include "pch.h"

#include "F4TSleepMarker.h"
#include "F4TSleepMarkerFilter.h"
#include "F4TRuntimeSelect.h"
#include "F4TDebugLogging.h"
#include "RE/A/Actor.h"
#include "REL/Trampoline.h"
#include "REL/ASM.h"

#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>

namespace F4T::SleepMarker
{
	namespace
	{
		constexpr REL::ID kSleepProcedure{ 1439829, 2211303, 2211303 };
		constexpr REL::ID kSleepCollector{ 523193, 2193929, 2193929 };
		constexpr REL::ID kSleepMarkerCallback{ 1056662, 2193971, 2193971 };
		using CollectFn = bool (*)(RE::Actor*, void*, std::uint32_t, std::uint8_t, std::uint8_t, RE::TESObjectREFR*, void*);
		using MarkerFn = int (*)(void*, std::uint32_t, void*);
		CollectFn g_collect{};
		MarkerFn g_marker{};
		std::atomic<bool> g_ready{};
		REL::Trampoline g_trampoline{ "SleepMarker" };
		struct Registration
		{
			RE::NiPointer<RE::TESObjectREFR> actor;
			RE::NiPointer<RE::TESObjectREFR> bed;
			std::uint32_t marker{};
		};
		std::mutex g_mutex;
		Registry g_registry;
		thread_local Selection g_selection;

		bool Collect(RE::Actor* a_actor, void* a_data, std::uint32_t a_value, std::uint8_t a_flag,
			std::uint8_t a_otherFlag, RE::TESObjectREFR* a_bed, void* a_output)
		{
			std::shared_ptr<const Registration> registration;
			if (g_ready.load(std::memory_order_acquire)) {
				std::scoped_lock lock(g_mutex);
				const auto found = g_registry.find(a_actor);
				if (found != g_registry.end()) registration = std::static_pointer_cast<const Registration>(found->second);
			}
			if (registration && registration->bed.get() != a_bed) registration.reset();
			const CollectionScope scope(g_selection, registration ?
				Selection{ registration->actor.get(), registration->bed.get(), registration->marker } : Selection{});
			return g_collect(a_actor, a_data, a_value, a_flag, a_otherFlag, a_bed, a_output);
		}

		int Marker(void* a_context, std::uint32_t a_marker, void* a_data)
		{
			const void* identity[2]{};
			if (a_context) std::memcpy(identity, a_context, sizeof(identity));
			if (g_selection.Reject(identity[0], identity[1], a_marker)) return 1;
			return g_marker(a_context, a_marker, a_data);
		}
	}

	void Install()
	{
		static bool attempted{};
		if (attempted) return;
		attempted = true;
		const auto module = REX::FModule::GetExecutingModule();
		if (module.GetFileVersion() != RuntimeSelect::VerifiedVersion()) return;
		const auto text = module.GetSection(".text");
		const auto begin = text.GetAddress();
		const auto end = begin + text.GetSize();
		const auto collector = kSleepCollector.address();
		const auto callback = kSleepMarkerCallback.address();
		const auto collectSite = kSleepProcedure.address() + (RuntimeSelect::IsOG() ? 0x232 : 0x24D);
		const auto markerSite = collector + 0xD3;
		const auto matches = [&](std::uintptr_t a_site, std::uintptr_t a_target) {
			return a_site >= begin && a_site < end && end - a_site >= 5 &&
				MatchesCall({ reinterpret_cast<const std::uint8_t*>(a_site), 5 }, a_site, a_target);
		};
		if (collector < begin || collector >= end || callback < begin || callback >= end ||
			!matches(collectSite, collector) || !matches(markerSite, callback)) {
			DebugLogging::Write("SleepMarker", "Exact marker hooks unavailable: callsites differ; remote sleep disabled");
			return;
		}
		g_collect = reinterpret_cast<CollectFn>(collector);
		g_marker = reinterpret_cast<MarkerFn>(callback);
		const auto* trampoline = F4SE::GetTrampolineInterface();
		void* memory = trampoline ? trampoline->AllocateFromBranchPool(64) : nullptr;
		if (!memory) {
			DebugLogging::Write("SleepMarker", "Exact marker trampoline unavailable; remote sleep disabled");
			return;
		}
		g_trampoline.set_trampoline(memory, 64);
		const auto collectBranch = g_trampoline.allocate_branch5(reinterpret_cast<std::uintptr_t>(&Collect));
		const auto markerBranch = g_trampoline.allocate_branch5(reinterpret_cast<std::uintptr_t>(&Marker));
		const REL::ASM::CALL5 collectCall(collectSite, collectBranch);
		const REL::ASM::CALL5 markerCall(markerSite, markerBranch);
		if (!MatchesCall({ reinterpret_cast<const std::uint8_t*>(&collectCall), 5 }, collectSite, collectBranch) ||
			!MatchesCall({ reinterpret_cast<const std::uint8_t*>(&markerCall), 5 }, markerSite, markerBranch)) return;
		const bool written = REL::WriteSafeData(markerSite, markerCall) && REL::WriteSafeData(collectSite, collectCall);
		const bool flushed = ::FlushInstructionCache(::GetCurrentProcess(), nullptr, 0) != 0;
		if (!written || !flushed) {
			DebugLogging::Write("SleepMarker", "Exact marker hooks could not be written; remote sleep disabled");
			return;
		}
		g_ready.store(matches(collectSite, collectBranch) && matches(markerSite, markerBranch), std::memory_order_release);
	}

	Token Register(RE::Actor& a_actor, RE::TESObjectREFR& a_bed, std::uint32_t a_marker)
	{
		if (!g_ready.load(std::memory_order_acquire)) return {};
		auto registration = std::make_shared<const Registration>(Registration{
			RE::NiPointer<RE::TESObjectREFR>{ &a_actor }, RE::NiPointer<RE::TESObjectREFR>{ &a_bed }, a_marker });
		decltype(g_registry)::node_type previous;
		{
			std::scoped_lock lock(g_mutex);
			previous = g_registry.extract(&a_actor);
			g_registry.emplace(&a_actor, registration);
		}
		return registration;
	}

	void Remove(const Token& a_token)
	{
		decltype(g_registry)::node_type removed;
		{
			std::scoped_lock lock(g_mutex);
			removed = ExtractRegistration(g_registry, a_token);
		}
	}
}
