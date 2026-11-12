#include "pch.h"

#include "F4TCampRegistration.h"

#include "F4TConsoleGuard.h"
#include "F4TWorkshopReplicationClient.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <set>

namespace F4T::CampRegistration
{
	namespace
	{
		constexpr std::uint32_t kCampWorkbenchRecord = 0x000C1AEB;
		constexpr float kPlayerRange = 256.0F;
		constexpr float kBenchMatchRange = 64.0F;
		constexpr std::chrono::seconds kRecheckPeriod{ 1 };

		struct PendingCamp
		{
			Workshop::Transform centre{};
			std::chrono::steady_clock::time_point nextCheck{};
		};

		std::map<Workshop::BuildZoneId, PendingCamp> g_pending;
		std::set<Workshop::BuildZoneId> g_registered;

		[[nodiscard]] float DistanceSquared(const RE::NiPoint3& a_point, const Workshop::Transform& a_centre)
		{
			const auto dx = a_point.x - a_centre.x;
			const auto dy = a_point.y - a_centre.y;
			const auto dz = a_point.z - a_centre.z;
			return dx * dx + dy * dy + dz * dz;
		}

		[[nodiscard]] bool BenchLoadedAt(RE::TESObjectCELL& a_cell, const Workshop::Transform& a_centre)
		{
			const auto* bench = RE::TESForm::GetFormByID<RE::TESBoundObject>(kCampWorkbenchRecord);
			if (!bench) {
				return false;
			}
			bool loaded = false;
			a_cell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetObjectReference() == bench && a_ref->Get3D() &&
					DistanceSquared(a_ref->GetPosition(), a_centre) <= kBenchMatchRange * kBenchMatchRange) {
					loaded = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return loaded;
		}
	}

	void RequestRegistration(Workshop::BuildZoneId a_buildZoneId, const Workshop::Transform& a_centre, bool a_ownCamp)
	{
		if (!a_ownCamp || g_registered.contains(a_buildZoneId)) {
			return;
		}
		g_pending[a_buildZoneId] = PendingCamp{ a_centre, {} };
	}

	void Forget(Workshop::BuildZoneId a_buildZoneId)
	{
		g_registered.erase(a_buildZoneId);
		g_pending.erase(a_buildZoneId);
	}

	void Tick()
	{
		if (g_pending.empty()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		const auto now = std::chrono::steady_clock::now();
		for (auto it = g_pending.begin(); it != g_pending.end();) {
			if (!F4T::WorkshopReplicationClient::ZoneRevision(it->first)) {
				it = g_pending.erase(it);
				continue;
			}
			if (!player || !cell || now < it->second.nextCheck) {
				++it;
				continue;
			}
			it->second.nextCheck = now + kRecheckPeriod;
			const auto range = kPlayerRange * kPlayerRange;
			if (DistanceSquared(player->GetPosition(), it->second.centre) > range || !BenchLoadedAt(*cell, it->second.centre)) {
				++it;
				continue;
			}
			F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest RegisterCamp");
			g_registered.insert(it->first);
			it = g_pending.erase(it);
		}
	}

	void Reset()
	{
		g_pending.clear();
		g_registered.clear();
	}
}
