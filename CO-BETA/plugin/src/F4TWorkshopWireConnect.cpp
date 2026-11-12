#include "pch.h"

#include "F4TWorkshopWireConnect.h"

#include "F4TWorkshopPowerApply.h"

#include "RE/E/ExtraPowerLinks.h"

#include <array>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	namespace
	{
		std::optional<Workshop::ObjectId> ObjectIdForFormId(const ZoneState& a_zone, std::uint32_t a_formId)
		{
			const auto refOf = [](RE::ObjectRefHandle a_copy) { return a_copy.get(); };
			for (const auto& [objectId, handle] : a_zone.objects) {
				if (auto ref = refOf(handle); ref && ref->GetFormID() == a_formId) {
					return objectId;
				}
			}
			return std::nullopt;
		}

		std::optional<std::array<Workshop::ObjectId, 2>> WireEndpoints(const ZoneState& a_zone, RE::TESObjectREFR& a_wire)
		{
			const auto* base = a_wire.GetObjectReference();
			const auto extra = a_wire.extraList.get();
			auto* links = extra ? extra->GetByType<RE::ExtraPowerLinks>() : nullptr;
			if (base == nullptr || base->GetFormType() != RE::ENUM_FORM_ID::kBNDS || links == nullptr || links->powerLinks.size() != 2) {
				F4T::DebugLogging::Log("Workshop", "wire connect dropped: ref {:08X} is not a spline with two power links", a_wire.GetFormID());
				return std::nullopt;
			}
			const auto from = ObjectIdForFormId(a_zone, links->powerLinks[0].formID);
			const auto to = ObjectIdForFormId(a_zone, links->powerLinks[1].formID);
			if (!from || !to || *from == *to) {
				F4T::DebugLogging::Log("Workshop", "wire connect dropped: spline {:08X} endpoints are not two distinct objects of the zone", a_wire.GetFormID());
				return std::nullopt;
			}
			return std::array<Workshop::ObjectId, 2>{ *from, *to };
		}
	}

	void QueueWireConnect(const ZoneState& a_zone, Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_wireId, RE::ObjectRefHandle a_wireHandle)
	{
		auto wire = a_wireHandle.get();
		if (!wire) {
			F4T::DebugLogging::Log("Workshop", "wire connect dropped: wire {} is not loaded", a_wireId);
			return;
		}
		if (IsApplyCreatedRef(*wire)) {
			return;
		}
		const auto ends = WireEndpoints(a_zone, *wire);
		if (!ends) {
			return;
		}
		QueuePacket(Json{ { "type", "wireOp" }, { "op", "connect" }, { "buildZoneId", a_buildZoneId }, { "operationId", g_nextOperationId++ }, { "fromId", (*ends)[0] }, { "toId", (*ends)[1] }, { "wireId", a_wireId } });
	}
}
