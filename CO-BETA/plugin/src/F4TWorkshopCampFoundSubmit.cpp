#include "pch.h"

#include "F4TCampGround.h"
#include "F4TInventoryPickupGate.h"
#include "F4TWorkshopReplicationClient.h"
#include "F4TWorkshopReplicationInternal.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SendHUDMessage.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	bool SubmitCampFound(const RE::TESObjectREFR& a_ref, std::uint32_t a_dropEntityId)
	{
		const auto space = SpaceOf(a_ref);
		if (!space) {
			F4T::InventoryPickupGate::RefuseCamp(a_dropEntityId);
			Warn("camp_kit_no_space", "Camp kit not placed: drop location has no worldspace.");
			RE::SendHUDMessage::ShowHUDMessage("Camp kit cannot be placed here.", nullptr, true, true);
			return true;
		}
		auto transform = TransformOf(a_ref);
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) { transform.z = player->GetPosition().z; }
		const auto ground = F4T::CampGround::SurfaceAt(transform.x, transform.y, transform.z);
		if (!ground) {
			F4T::InventoryPickupGate::RefuseCamp(a_dropEntityId);
			Warn("camp_kit_no_ground", "Camp kit not placed: no solid ground under the drop location.");
			RE::SendHUDMessage::ShowHUDMessage("Camp refused: no solid ground here. Try flat open ground.", nullptr, true, true);
			return true;
		}
		std::uint64_t operationId = 0;
		{
			std::scoped_lock lock(g_mutex);
			if (std::any_of(g_pendingCamps.begin(), g_pendingCamps.end(), [&](const auto& a_entry) { return a_entry.second.dropEntityId == a_dropEntityId; })) { return true; }
			operationId = g_nextOperationId++;
			g_pendingCamps[operationId] = PendingCamp{ a_dropEntityId, HandleOf(a_ref), std::chrono::steady_clock::now() };
		}
		Json packet{
			{ "type", "campFound" }, { "operationId", operationId }, { "dropEntityId", a_dropEntityId },
			{ "space", Json{ { "plugin", space->plugin }, { "localFormId", space->localFormId } } },
			{ "x", transform.x }, { "y", transform.y }, { "z", transform.z },
			{ "probe", Json{ { "height", ground->z }, { "slopeDegrees", ground->slopeDegrees }, { "navmeshHit", true } } }
		};
		QueuePacket(std::move(packet));
		return true;
	}

	bool ResolvePendingCamp(std::uint64_t a_operationId, bool a_placed, std::optional<Workshop::BuildZoneId> a_zone)
	{
		if (ResolvePendingCampStore(a_operationId, a_zone)) {
			return true;
		}
		std::scoped_lock lock(g_mutex);
		const auto pending = g_pendingCamps.find(a_operationId);
		if (pending == g_pendingCamps.end()) {
			return false;
		}
		if (a_placed) {
			Despawn(pending->second.localHandle);
		} else {
			F4T::InventoryPickupGate::RefuseCamp(pending->second.dropEntityId);
		}
		g_pendingCamps.erase(pending);
		return true;
	}
}
