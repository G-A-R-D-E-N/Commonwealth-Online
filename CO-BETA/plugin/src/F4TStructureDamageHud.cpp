#include "pch.h"

#include "F4TWorkshopReplicationClient.h"

#include "CommonwealthOnlineUI_API.h"

#include <chrono>
#include <mutex>
#include <utility>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::chrono::seconds kLocalHitWindow{ 5 };
		std::mutex g_localHitMutex;
		std::optional<PlacedObject> g_localHit;
		std::optional<PlacedObject> g_displayed;
		std::chrono::steady_clock::time_point g_localHitTime{};

		void QueueStructureHudPush(Json a_state)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			taskInterface->AddTask([state = std::move(a_state)]() {
				auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI23>();
				if (!api || !api->PushStructureHud) {
					return;
				}
				const auto text = state.dump();
				api->PushStructureHud(text.c_str());
			});
		}

		void QueueStructureHudClear(Workshop::ObjectId a_object)
		{
			QueueStructureHudPush(Json{ { "health", 0 }, { "maxHealth", 1 }, { "objectId", a_object } });
		}
	}

	void NoteLocalStructureHit(const PlacedObject& a_object)
	{
		std::optional<PlacedObject> cleared;
		{
			std::scoped_lock lock(g_localHitMutex);
			g_localHit = a_object;
			g_localHitTime = std::chrono::steady_clock::now();
			if (g_displayed && (g_displayed->buildZoneId != a_object.buildZoneId || g_displayed->objectId != a_object.objectId)) {
				cleared = g_displayed;
				g_displayed.reset();
			}
		}
		if (cleared) {
			QueueStructureHudClear(cleared->objectId);
		}
	}

	void PushStructureDestroyedHud(Workshop::BuildZoneId a_zone, Workshop::ObjectId a_object)
	{
		{
			std::scoped_lock lock(g_localHitMutex);
			if (!g_displayed || g_displayed->buildZoneId != a_zone || g_displayed->objectId != a_object) {
				return;
			}
			g_displayed.reset();
		}
		QueueStructureHudClear(a_object);
	}

	void PushStructureDamageHud(const Json& a_packet)
	{
		const auto health = a_packet.find("health");
		const auto maxHealth = a_packet.find("maxHealth");
		const auto objectId = a_packet.find("objectId");
		const auto zoneId = a_packet.find("buildZoneId");
		if (health == a_packet.end() || maxHealth == a_packet.end() || objectId == a_packet.end() || zoneId == a_packet.end() ||
			!health->is_number() || !maxHealth->is_number() || !objectId->is_number_unsigned() || !zoneId->is_number_unsigned()) {
			return;
		}
		const PlacedObject shown{ zoneId->get<Workshop::BuildZoneId>(), objectId->get<Workshop::ObjectId>() };
		{
			std::scoped_lock lock(g_localHitMutex);
			if (!g_localHit || g_localHit->buildZoneId != shown.buildZoneId || g_localHit->objectId != shown.objectId ||
				std::chrono::steady_clock::now() - g_localHitTime > kLocalHitWindow) {
				return;
			}
			g_displayed = shown;
		}
		QueueStructureHudPush(Json{ { "health", *health }, { "maxHealth", *maxHealth }, { "objectId", *objectId } });
	}
}
