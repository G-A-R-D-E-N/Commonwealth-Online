#include "pch.h"

#include "F4TWorkshopReplicationInternal.h"

#include "F4TDebugLogging.h"
#include "F4TNetworkingInternal.h"
#include "F4TWorkshopRaids.h"
#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"
#include "RE/S/SendHUDMessage.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace F4T::WorkshopReplicationClient
{
	using namespace F4T::Networking::Detail;
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::uint32_t kObjectReferenceVmType{ 64 };
		constexpr std::uint8_t kMaximumBenchClaimTries{ 5 };
		constexpr std::chrono::seconds kBenchClaimRetry{ 1 };

		struct PendingCampStore
		{
			Workshop::BuildZoneId buildZoneId{};
			std::chrono::steady_clock::time_point sentAt{};
		};

		std::map<std::uint64_t, PendingCampStore> g_pendingCampStores;
		std::set<std::uint64_t> g_pendingCampRemoves;

		bool SetBenchOwnedByPlayer(RE::TESObjectREFR& a_bench)
		{
			const auto game = RE::GameVM::GetSingleton();
			const auto vm = game ? game->GetVM() : nullptr;
			if (!vm) {
				return false;
			}
			const auto& handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(kObjectReferenceVmType, std::addressof(a_bench));
			if (handle == handles.EmptyHandle()) {
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!vm->FindBoundObject(handle, "WorkshopScript", false, object, false) || !object) {
				return false;
			}
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
			const auto queued = vm->DispatchMethodCall(static_cast<std::uint64_t>(handle), "WorkshopScript", "SetOwnedByPlayer", callback, true);
			if (!queued) {
				F4T::DebugLogging::Log("CampStore", "camp bench {:08X} owner claim was refused", a_bench.GetFormID());
			}
			return queued;
		}
	}

	bool SubmitCampStore(const RE::TESObjectREFR& a_workbench, bool a_sneaking)
	{
		const auto placed = FindPlacedObject(a_workbench);
		if (!placed || placed->objectId != F4T::WorkshopRaids::WorkbenchObjectId) {
			return false;
		}
		std::scoped_lock lock(g_mutex);
		if (!g_ownCamps.contains(placed->buildZoneId)) {
			return false;
		}
		if (!a_sneaking) {
			ShowCampMoveAlert(placed->buildZoneId);
			static auto shownAt = std::chrono::steady_clock::time_point{};
			if (const auto now = std::chrono::steady_clock::now(); now - shownAt >= std::chrono::seconds(10)) { shownAt = now; RE::SendHUDMessage::ShowHUDMessage("Sneak + activate to pack up your camp.", nullptr, true, true); }
			return false;
		}
		RE::SendHUDMessage::ShowHUDMessage("Packing up your camp...", nullptr, true, true);
		if (std::any_of(g_pendingCampStores.begin(), g_pendingCampStores.end(), [&](const auto& a_entry) { return a_entry.second.buildZoneId == placed->buildZoneId; })) { return true; }
		const auto operationId = g_nextOperationId++;
		g_pendingCampStores[operationId] = PendingCampStore{ placed->buildZoneId, std::chrono::steady_clock::now() };
		QueuePacket(Json{ { "type", "campStore" }, { "operationId", operationId }, { "buildZoneId", placed->buildZoneId } });
		return true;
	}

	bool ResolvePendingCampStore(std::uint64_t a_operationId, std::optional<Workshop::BuildZoneId> a_zone)
	{
		std::scoped_lock lock(g_mutex);
		const auto pending = g_pendingCampStores.find(a_operationId);
		if (pending == g_pendingCampStores.end() || (a_zone && pending->second.buildZoneId != *a_zone)) {
			return false;
		}
		g_pendingCampStores.erase(pending);
		return true;
	}

	void RememberPendingCampRemove(std::uint64_t a_operationId)
	{
		std::scoped_lock lock(g_mutex);
		g_pendingCampRemoves.insert(a_operationId);
	}

	bool ResolvePendingCampRemove(std::uint64_t a_operationId)
	{
		std::scoped_lock lock(g_mutex);
		return g_pendingCampRemoves.erase(a_operationId) != 0;
	}

	void ForgetCampStores()
	{
		std::scoped_lock lock(g_mutex);
		g_pendingCampStores.clear();
		g_pendingCampRemoves.clear();
	}

	void TickCampStores()
	{
		std::vector<std::pair<Workshop::BuildZoneId, RE::ObjectRefHandle>> benches;
		{
			std::scoped_lock lock(g_mutex);
			const auto now = std::chrono::steady_clock::now();
			std::erase_if(g_pendingCampStores, [now](const auto& a_entry) {
				if (now - a_entry.second.sentAt < kPendingTimeout) { return false; }
				LogThrottledWarning("camp_store_timeout", "Camp packing timed out; camp left in place.");
				RE::SendHUDMessage::ShowHUDMessage("Camp packing timed out. Try again.", nullptr, true, true);
				return true;
			});
			for (const auto zoneId : g_ownCamps) {
				const auto zone = g_zones.find(zoneId);
				if (zone == g_zones.end() || zone->second.campBenchOwned || zone->second.benchClaimTries >= kMaximumBenchClaimTries || now < zone->second.nextBenchClaimAt) { continue; }
				const auto bench = zone->second.objects.find(F4T::WorkshopRaids::WorkbenchObjectId);
				if (bench != zone->second.objects.end()) { benches.emplace_back(zoneId, bench->second); }
			}
		}
		for (auto& [zoneId, handle] : benches) {
			const auto ref = handle.get();
			const bool claimed = ref && SetBenchOwnedByPlayer(*ref);
			std::scoped_lock lock(g_mutex);
			const auto zone = g_zones.find(zoneId);
			if (zone == g_zones.end()) { continue; }
			if (claimed) { zone->second.campBenchOwned = true; continue; }
			zone->second.nextBenchClaimAt = std::chrono::steady_clock::now() + kBenchClaimRetry;
			if (ref) { ++zone->second.benchClaimTries; }
		}
	}
}
