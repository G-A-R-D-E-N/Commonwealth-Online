#include "pch.h"

#include "F4TWorkshopPowerApply.h"

#include "F4TDebugLogging.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TSettlementCapturePolicy.h"
#include "F4TWorkshopRaids.h"
#include "RE/E/ExtraPowerLinks.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/S/SplineUtils.h"
#include "RE/T/TES.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <set>

namespace F4T::WorkshopReplicationClient
{
	namespace
	{
		// constraint: these two forms must match Fallout4.esm on every runtime, see E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt R2-9
		constexpr std::uint32_t kWorkshopItemKeyword = 0x00054BA6;
		constexpr std::uint32_t kSplineBase = 0x0001D971;
		constexpr std::chrono::seconds kRetryEvery{ 3 };
		constexpr int kMaxRetries = 20;

		inline constexpr REL::ID kUpdateSpline{ 1150803, 2195071, 2195071 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kUpdateMovingWirelessItem{ 958345, 2195088, 2195088 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kEstablishTerminalLinks{ 608512, 2195102, 2195102 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kAddItem{ 1030944, 2194996, 2194996 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kAddConnection{ 523652, 2194998, 2194998 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kCurrentWorkshop{ 737927, 2689952, 4797241 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;
		inline constexpr REL::ID kRemoveConnection{ 800489, 2194999, 2194999 } /* E:/RE-work/co-workshop-wire-r3/findings-G-answers.txt */;

		std::mutex g_createdMutex;
		std::set<std::uint32_t> g_applyCreated;

		bool UpdateSpline(RE::TESObjectREFR* a_wire, RE::TESObjectREFR* a_end1, RE::TESObjectREFR* a_end2)
		{
			using Fn = bool (*)(RE::Workshop::ContextData*, RE::TESObjectREFR*, RE::TESObjectREFR*, std::int32_t, RE::TESObjectREFR*, std::int32_t);
			static REL::Relocation<Fn> func{ kUpdateSpline };
			return func(nullptr, a_wire, a_end1, 0, a_end2, 0);
		}

		void UpdateMovingWirelessItem(RE::TESObjectREFR* a_ref, RE::Workshop::ExtraData* a_extra)
		{
			using Fn = void (*)(RE::TESObjectREFR*, RE::Workshop::ExtraData*);
			static REL::Relocation<Fn> func{ kUpdateMovingWirelessItem };
			func(a_ref, a_extra);
		}

		void EstablishTerminalLinks(RE::TESObjectREFR* a_wire)
		{
			using Fn = void (*)(RE::TESObjectREFR*);
			static REL::Relocation<Fn> func{ kEstablishTerminalLinks };
			func(a_wire);
		}

		void AddItem(RE::Workshop::ExtraData* a_extra, RE::TESObjectREFR* a_item)
		{
			using Fn = void (*)(RE::Workshop::ExtraData*, RE::TESObjectREFR*);
			static REL::Relocation<Fn> func{ kAddItem };
			func(a_extra, a_item);
		}

		void AddConnection(RE::Workshop::ExtraData* a_extra, RE::TESObjectREFR* a_end1, RE::TESObjectREFR* a_end2, RE::TESObjectREFR* a_wire)
		{
			using Fn = void (*)(RE::Workshop::ExtraData*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESObjectREFR*);
			static REL::Relocation<Fn> func{ kAddConnection };
			func(a_extra, a_end1, a_end2, a_wire);
		}

		void RemoveConnection(RE::Workshop::ExtraData* a_extra, RE::TESObjectREFR* a_end1, RE::TESObjectREFR* a_end2, RE::TESObjectREFR* a_wire, bool a_notify)
		{
			using Fn = void (*)(RE::Workshop::ExtraData*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESObjectREFR*, bool);
			static REL::Relocation<Fn> func{ kRemoveConnection };
			func(a_extra, a_end1, a_end2, a_wire, a_notify);
		}

		class WorkshopScope
		{
		public:
			explicit WorkshopScope(RE::TESObjectREFR& a_workshop)
			{
				static REL::Relocation<std::uint32_t*> slot{ kCurrentWorkshop };
				m_slot = slot.get();
				m_saved = *m_slot;
				*m_slot = a_workshop.GetHandle().get_handle();
			}

			~WorkshopScope() { *m_slot = m_saved; }
			WorkshopScope(const WorkshopScope&) = delete;
			WorkshopScope& operator=(const WorkshopScope&) = delete;

		private:
			std::uint32_t* m_slot{ nullptr };
			std::uint32_t m_saved{ 0 };
		};

		RE::NiPointer<RE::TESObjectREFR> Resolve(const ZoneState& a_zone, Workshop::ObjectId a_objectId)
		{
			const auto found = a_zone.objects.find(a_objectId);
			if (found == a_zone.objects.end()) {
				return {};
			}
			auto ref = found->second.get();
			return ref && !ref->IsDeleted() ? ref : RE::NiPointer<RE::TESObjectREFR>{};
		}

		bool IsSpline(const RE::TESObjectREFR& a_ref)
		{
			const auto* base = a_ref.GetObjectReference();
			return base != nullptr && base->GetFormType() == RE::ENUM_FORM_ID::kBNDS;
		}

		std::uint32_t PowerLinkCount(const RE::TESObjectREFR& a_ref)
		{
			const auto extra = a_ref.extraList.get();
			const auto* links = extra ? extra->GetByType<RE::ExtraPowerLinks>() : nullptr;
			return links ? links->powerLinks.size() : 0;
		}

		RE::Workshop::ExtraData* ExtraOf(const RE::TESObjectREFR& a_workshop)
		{
			const auto extra = a_workshop.extraList.get();
			return extra ? extra->GetByType<RE::Workshop::ExtraData>() : nullptr;
		}

		void Remember(const RE::TESObjectREFR& a_ref)
		{
			std::scoped_lock lock(g_createdMutex);
			g_applyCreated.insert(a_ref.GetFormID());
		}

		void ForgetApplyCreated(const RE::TESObjectREFR& a_ref)
		{
			std::scoped_lock lock(g_createdMutex);
			g_applyCreated.erase(a_ref.GetFormID());
		}

		RE::NiPointer<RE::TESObjectREFR> ResolveBench(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone)
		{
			if (a_zone.campWorkbench) {
				return Resolve(a_zone, F4T::WorkshopRaids::WorkbenchObjectId);
			}
			if (auto cached = a_zone.settlementBench.get(); cached && !cached->IsDeleted() && ExtraOf(*cached)) {
				return cached;
			}
			const auto point = F4T::SettlementCaptureClient::WorkbenchPointOf(a_zoneId);
			auto* tes = RE::TES::GetSingleton();
			if (!point || !tes) {
				return {};
			}
			RE::NiPointer<RE::TESObjectREFR> best;
			auto bestDistance = (std::numeric_limits<float>::max)();
			const RE::NiPoint3 origin{ (*point)[0], (*point)[1], (*point)[2] };
			tes->ForEachReferenceInRange(origin, F4T::SettlementCapturePolicy::kWorkbenchPromptRadius, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && !a_ref->IsDeleted() && ExtraOf(*a_ref)) {
					const auto position = a_ref->GetPosition();
					const auto dx = position.x - origin.x;
					const auto dy = position.y - origin.y;
					const auto dz = position.z - origin.z;
					const auto distance = dx * dx + dy * dy + dz * dz;
					if (distance < bestDistance) {
						bestDistance = distance;
						best = a_ref;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			a_zone.settlementBench = best ? HandleOf(*best) : RE::ObjectRefHandle{};
			return best;
		}

		RE::ObjectRefHandle CreateWire(const RE::TESObjectREFR& a_at, RE::TESBoundObject& a_base)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			auto* cell = a_at.GetParentCell();
			if (!handler || !cell) {
				return {};
			}
			RE::NEW_REFR_DATA data;
			data.object = &a_base;
			data.location = a_at.GetPosition();
			data.reference = nullptr;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			data.extra = nullptr;
			data.maxLevel = 0;
			data.forcePersist = true;
			data.clearStillLoadingFlag = true;
			data.initializeScripts = false;
			data.initiallyDisabled = true;
			return handler->CreateReferenceAtLocation(data);
		}

		bool ApplyOneWire(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone, const F4T::WorkshopProtocol::PowerWire& a_wire)
		{
			const auto refA = Resolve(a_zone, a_wire.fromId);
			const auto refB = Resolve(a_zone, a_wire.toId);
			if (!refA || !refB || refA == refB) {
				Warn("workshop_power_ends_missing", "Workshop wire not applied yet: an endpoint is not loaded locally.");
				return false;
			}
			auto wire = Resolve(a_zone, a_wire.wireId);
			if (wire && !IsSpline(*wire)) {
				F4T::DebugLogging::Log("Workshop", "wire {} skipped: the zone object with that id is not a spline", a_wire.wireId);
				return true;
			}
			if (wire && PowerLinkCount(*wire) >= 2) {
				return true;
			}
			auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kWorkshopItemKeyword);
			auto* splineBase = RE::TESForm::GetFormByID<RE::TESBoundObject>(kSplineBase);
			if (!keyword || !splineBase) {
				Warn("workshop_power_forms", "Workshop wire not applied: the workshop keyword or spline base form is missing.");
				return false;
			}
			RE::NiPointer<RE::TESObjectREFR> bench;
			auto* workshop = refA->GetLinkedRef(keyword);
			if (workshop == nullptr) {
				bench = ResolveBench(a_zoneId, a_zone);
				workshop = bench.get();
			}
			auto* extra = workshop ? ExtraOf(*workshop) : nullptr;
			if (!extra) {
				if (workshop && a_zone.campWorkbench) {
					Warn("workshop_power_camp_no_extra", "Workshop wire not applied yet: the camp workbench has no power data (workshop extra) yet.");
				} else {
					Warn("workshop_power_no_workshop", "Workshop wire not applied yet: no local workshop with power data for its endpoints.");
				}
				return false;
			}
			const bool created = !wire;
			if (created) {
				const auto handle = CreateWire(*refA, *splineBase);
				wire = handle.get();
				if (!wire) {
					Warn("workshop_power_wire_refused", "Workshop wire not applied yet: the engine refused the wire reference.");
					return false;
				}
				a_zone.objects[a_wire.wireId] = handle;
			}
			Remember(*wire);
			wire->SetLinkedRef(workshop, keyword);
			{
				WorkshopScope scope{ *workshop };
				for (auto* end : { refA.get(), refB.get() }) {
					if (!end->GetLinkedRef(keyword)) {
						end->SetLinkedRef(workshop, keyword);
						AddItem(extra, end);
					}
				}
				if (!UpdateSpline(wire.get(), refB.get(), refA.get())) {
					F4T::DebugLogging::Log("Workshop", "wire {} not applied yet: the engine judged the spline invalid", a_wire.wireId);
					if (created) {
						ForgetApplyCreated(*wire);
						wire->SetLinkedRef(nullptr, keyword);
						Despawn(a_zone.objects[a_wire.wireId]);
						a_zone.objects.erase(a_wire.wireId);
					}
					return false;
				}
				RE::SplineUtils::ConnectSpline(refA.get(), 0, refB.get(), 0, wire.get());
				AddItem(extra, wire.get());
				AddConnection(extra, refA.get(), refB.get(), wire.get());
				UpdateMovingWirelessItem(refB.get(), extra);
				UpdateMovingWirelessItem(refA.get(), extra);
				EstablishTerminalLinks(wire.get());
			}
			wire->Enable(false);
			F4T::DebugLogging::Log("Workshop", "wire {} applied between objects {} and {}", a_wire.wireId, a_wire.fromId, a_wire.toId);
			return true;
		}

		void RemovePowerWire(ZoneState& a_zone, const F4T::WorkshopProtocol::PowerWire& a_wire)
		{
			const auto wire = Resolve(a_zone, a_wire.wireId);
			if (!wire || !IsSpline(*wire)) {
				F4T::DebugLogging::Log("Workshop", "wire {} is no longer listed and has no local spline to remove", a_wire.wireId);
				return;
			}
			auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kWorkshopItemKeyword);
			auto* workshop = keyword ? wire->GetLinkedRef(keyword) : nullptr;
			auto* extra = workshop ? ExtraOf(*workshop) : nullptr;
			const auto refA = Resolve(a_zone, a_wire.fromId);
			const auto refB = Resolve(a_zone, a_wire.toId);
			if (extra && refA && refB) {
				WorkshopScope scope{ *workshop };
				RE::SplineUtils::DisconnectSpline(*wire);
				RemoveConnection(extra, refA.get(), refB.get(), wire.get(), true);
			} else {
				RE::SplineUtils::DisconnectSpline(*wire);
				F4T::DebugLogging::Log("Workshop", "wire {} removed without a grid edit: workshop data or an endpoint is not loaded", a_wire.wireId);
			}
			if (workshop) {
				wire->SetLinkedRef(nullptr, keyword);
			}
			ForgetApplyCreated(*wire);
			if (const auto found = a_zone.objects.find(a_wire.wireId); found != a_zone.objects.end()) {
				Despawn(found->second);
				a_zone.objects.erase(found);
			}
			F4T::DebugLogging::Log("Workshop", "wire {} removed after the server stopped listing it", a_wire.wireId);
		}

		void ApplyPending(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone)
		{
			for (auto it = a_zone.appliedWires.begin(); it != a_zone.appliedWires.end();) {
				const auto id = it->first;
				if (std::ranges::any_of(a_zone.wires, [id](const auto& a_wire) { return a_wire.wireId == id; })) {
					++it;
					continue;
				}
				try {
					RemovePowerWire(a_zone, it->second);
				} catch (...) {
					F4T::DebugLogging::Log("Workshop", "wire {} removal failed with an exception", id);
				}
				it = a_zone.appliedWires.erase(it);
			}
			for (const auto& wire : a_zone.wires) {
				if (a_zone.appliedWires.contains(wire.wireId)) {
					continue;
				}
				auto done = true;
				try {
					done = ApplyOneWire(a_zoneId, a_zone, wire);
				} catch (...) {
					F4T::DebugLogging::Log("Workshop", "wire {} apply failed with an exception", wire.wireId);
				}
				if (done) {
					a_zone.appliedWires.insert_or_assign(wire.wireId, wire);
				}
			}
		}
	}

	void ApplyPowerWires(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone)
	{
		a_zone.powerTries = 0;
		a_zone.powerGaveUp = false;
		ApplyPending(a_zoneId, a_zone);
	}

	void RemoveAppliedPowerWires(ZoneState& a_zone)
	{
		for (const auto& entry : a_zone.appliedWires) {
			try {
				RemovePowerWire(a_zone, entry.second);
			} catch (...) {
				F4T::DebugLogging::Log("Workshop", "wire {} removal failed with an exception", entry.first);
			}
		}
		a_zone.appliedWires.clear();
	}

	void ResumePowerApply(ZoneState& a_zone)
	{
		a_zone.powerTries = 0;
		a_zone.powerGaveUp = false;
		a_zone.nextPowerRetryAt = {};
		std::erase_if(a_zone.appliedWires, [&a_zone](const auto& a_entry) { return !Resolve(a_zone, a_entry.first); });
	}

	void RetryPowerApply(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone, std::chrono::steady_clock::time_point a_now)
	{
		if (a_zone.powerGaveUp || a_now < a_zone.nextPowerRetryAt || a_zone.wires.size() == a_zone.appliedWires.size()) {
			return;
		}
		a_zone.nextPowerRetryAt = a_now + kRetryEvery;
		ApplyPending(a_zoneId, a_zone);
		if (a_zone.wires.size() != a_zone.appliedWires.size() && ++a_zone.powerTries >= kMaxRetries) {
			a_zone.powerGaveUp = true;
			F4T::DebugLogging::Log("Workshop", "power apply for zone {} gave up after {} tries with {} of {} wires applied; it resumes on the next powerState or zone load", a_zoneId, kMaxRetries, a_zone.appliedWires.size(), a_zone.wires.size());
		}
	}

	bool IsApplyCreatedRef(const RE::TESObjectREFR& a_ref)
	{
		std::scoped_lock lock(g_createdMutex);
		return g_applyCreated.contains(a_ref.GetFormID());
	}

	std::set<std::uint32_t> ApplyCreatedRefs()
	{
		std::scoped_lock lock(g_createdMutex);
		return g_applyCreated;
	}

	void ForgetApplyCreatedRefs(const std::set<std::uint32_t>& a_formIds)
	{
		std::scoped_lock lock(g_createdMutex);
		for (const auto formId : a_formIds) {
			g_applyCreated.erase(formId);
		}
	}
}
