#include "pch.h"

#include "F4TClientIdentity.h"
#include "F4TSettlementAccessClient.h"
#include "F4TWorkshopDoorLock.h"
#include "F4TWorkshopReplicationInternal.h"
#include "RE/S/SendHUDMessage.h"

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::chrono::milliseconds kDoorPollInterval{ 500 };

		bool IsDoorRef(const RE::TESObjectREFR& a_ref)
		{
			const auto* base = a_ref.GetObjectReference();
			return base && base->GetFormType() == RE::ENUM_FORM_ID::kDOOR;
		}

		bool DoorIsOpen(const RE::TESObjectREFR& a_ref)
		{
			const auto open = RE::BGSOpenCloseForm::GetOpenState(&a_ref);
			return open == RE::BGSOpenCloseForm::OPEN_STATE::kOpen || open == RE::BGSOpenCloseForm::OPEN_STATE::kOpening;
		}

		bool DoorIsLocked(RE::TESObjectREFR& a_ref)
		{
			const auto* refLock = a_ref.GetLock();
			return refLock && (refLock->flags & static_cast<std::uint8_t>(RE::REFR_LOCK::Flags::kLocked)) != 0;
		}

		Json DoorOpPacket(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId, std::string_view a_op)
		{
			return Json{ { "type", "doorOp" }, { "buildZoneId", a_buildZoneId }, { "objectId", a_objectId }, { "op", std::string{ a_op } } };
		}

		DoorSeen ApplyDoorState(RE::TESObjectREFR& a_ref, bool a_open, bool a_locked)
		{
			// TODO-verify: ActivateRef toggle and F4TObjectSync echo unverified in game
			if (DoorIsOpen(a_ref) != a_open) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					if (auto* refLock = a_ref.GetLock()) { refLock->SetLocked(false); }
					a_ref.ActivateRef(player, nullptr, 1, false, false, false);
				}
			}
			// TODO-verify: Papyrus Lock on remote doors unverified in game
			WorkshopDoorLock::SetDoorLocked(a_ref, a_locked);
			return DoorSeen{ DoorIsOpen(a_ref), DoorIsLocked(a_ref) };
		}
	}

	bool HandleDoorState(const Json& a_packet)
	{
		const auto zone = a_packet.find("buildZoneId");
		const auto objectId = a_packet.find("objectId");
		const auto open = a_packet.find("open");
		const auto locked = a_packet.find("locked");
		if (zone == a_packet.end() || objectId == a_packet.end() || open == a_packet.end() || locked == a_packet.end() ||
			!zone->is_number_unsigned() || !objectId->is_number_unsigned() || !open->is_boolean() || !locked->is_boolean()) {
			return false;
		}
		const auto buildZoneId = zone->get<Workshop::BuildZoneId>();
		const auto id = objectId->get<Workshop::ObjectId>();
		auto handle = FindPlacedObjectHandle(buildZoneId, id);
		const auto ref = handle.get();
		if (!ref || !IsDoorRef(*ref)) {
			return true;
		}
		const auto applied = ApplyDoorState(*ref, open->get<bool>(), locked->get<bool>());
		std::scoped_lock lock(g_mutex);
		if (const auto found = g_zones.find(buildZoneId); found != g_zones.end()) {
			found->second.doors[id] = applied;
		}
		return true;
	}

	void CollectDoorOps(std::vector<Json>& a_outbound, std::chrono::steady_clock::time_point a_now)
	{
		if (a_now < g_nextDoorPoll) {
			return;
		}
		g_nextDoorPoll = a_now + kDoorPollInterval;
		for (auto& [buildZoneId, zone] : g_zones) {
			for (auto& [objectId, handle] : zone.objects) {
				const auto ref = handle.get();
				if (!ref || !IsDoorRef(*ref)) {
					continue;
				}
				const DoorSeen current{ DoorIsOpen(*ref), DoorIsLocked(*ref) };
				const auto seen = zone.doors.try_emplace(objectId, current).first;
				if (current.open != seen->second.open) {
					a_outbound.push_back(DoorOpPacket(buildZoneId, objectId, current.open ? "open" : "close"));
				}
				if (current.locked != seen->second.locked) {
					a_outbound.push_back(DoorOpPacket(buildZoneId, objectId, current.locked ? "lock" : "unlock"));
				}
				seen->second = current;
			}
		}
		const auto allowed = F4T::SettlementAccessClient::BuildZonesAllowedFor(F4T::ClientIdentity::ConnectedProfileId());
		const auto owners = F4T::SettlementAccessClient::OwnersByBuildZone();
		for (auto& [buildZoneId, zone] : g_zones) {
			const auto owner = owners.find(buildZoneId);
			for (auto& [objectId, handle] : zone.objects) {
				if (const auto ref = handle.get()) {
					const auto door = zone.doors.find(objectId);
					WorkshopDoorLock::RefreshActivationBlock(*ref, allowed.contains(buildZoneId), owner != owners.end() && !owner->second.empty(), door != zone.doors.end() && door->second.locked);
				}
			}
		}
	}

	void HandleDoorActivation(RE::TESObjectREFR& a_ref, bool a_sneaking)
	{
		if (!IsDoorRef(a_ref)) {
			return;
		}
		const auto placed = FindPlacedObject(a_ref);
		if (!placed) {
			return;
		}
		const bool locked = DoorIsLocked(a_ref);
		if (!F4T::SettlementAccessClient::BuildZonesAllowedFor(F4T::ClientIdentity::ConnectedProfileId()).contains(placed->buildZoneId)) {
			if (locked) {
				RE::SendHUDMessage::ShowHUDMessage("This door is locked by the settlement owner.", nullptr, true, true);
			}
			return;
		}
		if (a_sneaking) {
			QueuePacket(DoorOpPacket(placed->buildZoneId, placed->objectId, locked ? "unlock" : "lock"));
		}
	}
}
