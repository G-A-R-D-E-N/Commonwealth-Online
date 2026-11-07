#include "pch.h"

#include "F4TWorkshopReplicationInternal.h"

#include <mutex>
#include <optional>
#include <utility>

namespace F4T::WorkshopReplicationClient
{
	namespace
	{
		std::optional<Workshop::ObjectId> ObjectIdForHandle(const ZoneState& a_zone, const RE::ObjectRefHandle& a_handle)
		{
			const auto handleOf = [](RE::ObjectRefHandle a_copy) { return a_copy.get_handle(); };
			const auto needle = handleOf(a_handle);
			for (const auto& [objectId, handle] : a_zone.objects) {
				if (handleOf(handle) == needle) {
					return objectId;
				}
			}
			return std::nullopt;
		}
	}

	bool SubmitMove(const RE::TESObjectREFR& a_ref)
	{
		const auto zoneId = ActiveBuildZoneId();
		if (!zoneId) {
			return false;
		}
		const auto space = SpaceOf(a_ref);
		if (!space) {
			return false;
		}
		Workshop::ObjectId objectId = 0;
		Workshop::Revision revision = 0;
		Workshop::Transform prior{};
		bool hasPrior = false;
		RE::ObjectRefHandle handle;
		{
			std::scoped_lock lock(g_mutex);
			const auto zone = g_zones.find(*zoneId);
			if (zone == g_zones.end()) {
				return false;
			}
			handle = HandleOf(a_ref);
			const auto mapped = ObjectIdForHandle(zone->second, handle);
			if (!mapped) {
				return false;
			}
			objectId = *mapped;
			revision = zone->second.revision;
			prior = TransformOf(a_ref);
			hasPrior = true;
		}
		Workshop::Request request;
		request.buildZoneId = *zoneId;
		request.expectedRevision = revision;
		request.kind = Workshop::OperationKind::Move;
		request.objectId = objectId;
		request.space = *space;
		request.transform = TransformOf(a_ref);
		{
			std::scoped_lock lock(g_mutex);
			request.operationId = g_nextOperationId++;
		}
		Pending pending;
		pending.kind = PendingKind::Move;
		pending.request = request;
		pending.localHandle = handle;
		pending.priorTransform = prior;
		pending.hasPriorTransform = hasPrior;
		return EnqueuePending(std::move(pending));
	}

	bool SubmitScrap(std::uint32_t a_refHandle)
	{
		const auto zoneId = ActiveBuildZoneId();
		if (!zoneId) {
			return false;
		}
		Workshop::ObjectId objectId = 0;
		Workshop::Revision revision = 0;
		RE::ObjectRefHandle handle;
		{
			std::scoped_lock lock(g_mutex);
			const auto zone = g_zones.find(*zoneId);
			if (zone == g_zones.end()) {
				return false;
			}
			bool found = false;
			for (const auto& [id, stored] : zone->second.objects) {
				auto copy = stored;
				if (copy.get_handle() == a_refHandle) {
					objectId = id;
					handle = stored;
					found = true;
					break;
				}
			}
			if (!found) {
				return false;
			}
			revision = zone->second.revision;
		}
		Workshop::Request request;
		request.buildZoneId = *zoneId;
		request.expectedRevision = revision;
		request.kind = Workshop::OperationKind::Scrap;
		request.objectId = objectId;
		{
			std::scoped_lock lock(g_mutex);
			request.operationId = g_nextOperationId++;
		}
		Pending pending;
		pending.kind = PendingKind::Scrap;
		pending.request = request;
		pending.localHandle = handle;
		return EnqueuePending(std::move(pending));
	}

	std::optional<PlacedObject> FindPlacedObject(const RE::TESObjectREFR& a_ref)
	{
		const auto handle = const_cast<RE::TESObjectREFR&>(a_ref).GetHandle();
		std::scoped_lock lock(g_mutex);
		for (const auto& [buildZoneId, zone] : g_zones) {
			if (const auto objectId = ObjectIdForHandle(zone, handle)) {
				return PlacedObject{ buildZoneId, *objectId };
			}
		}
		return std::nullopt;
	}

	RE::ObjectRefHandle FindPlacedObjectHandle(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId)
	{
		std::scoped_lock lock(g_mutex);
		const auto zone = g_zones.find(a_buildZoneId);
		if (zone == g_zones.end()) return {};
		const auto object = zone->second.objects.find(a_objectId);
		return object == zone->second.objects.end() ? RE::ObjectRefHandle{} : object->second;
	}
}
