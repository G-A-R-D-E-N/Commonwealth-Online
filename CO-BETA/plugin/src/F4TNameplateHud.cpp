#include "pch.h"

#include "F4TNameplateHud.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDownedEngine.h"
#include "F4TOverheadProjectionEngine.h"
#include "F4TProxyActorController.h"
#include "F4TVoiceRuntime.h"
#include "F4TVoiceTiers.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>

namespace F4T::NameplateHud
{
	namespace
	{
		constexpr auto kPushPeriod = std::chrono::milliseconds{ 33 };
		constexpr float kHeadHeight = 140.0F;
		constexpr float kMaxDistance = static_cast<float>(F4T::Voice::ShoutRange);
		constexpr auto kSightCache = std::chrono::milliseconds{ 250 };
		constexpr std::size_t kMaxLabels = 8;

		std::chrono::steady_clock::time_point g_nextPush{};
		std::string g_lastPayload{ "[]" };

		struct SightEntry
		{
			std::chrono::steady_clock::time_point checked{};
			bool visible{};
		};
		std::unordered_map<std::uint32_t, SightEntry> g_sight;

		// Constraint: HasLOSToTarget is a game-thread camera raycast, so cache it per player (source:CO-BETA/plugin/lib/commonlibf4/include/RE/IDs.h PlayerCharacter::HasLOSToTarget).
		bool HasSight(RE::PlayerCharacter& a_player, std::uint32_t a_id, RE::Actor& a_target, std::chrono::steady_clock::time_point a_now)
		{
			auto& entry = g_sight[a_id];
			if (entry.checked == std::chrono::steady_clock::time_point{} || a_now - entry.checked >= kSightCache) {
				bool pickPerformed = false;
				entry.visible = a_player.HasLOSToTarget(&a_target, &pickPerformed);
				entry.checked = a_now;
			}
			return entry.visible;
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI27* Api()
		{
			static std::atomic<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI27*> cached{ nullptr };
			auto* api = cached.load(std::memory_order_acquire);
			if (!api) {
				api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI27>();
				cached.store(api, std::memory_order_release);
			}
			return api;
		}

		nlohmann::json BuildPayload()
		{
			const auto now = std::chrono::steady_clock::now();
			auto labels = nlohmann::json::array();
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!player) {
				return labels;
			}
			std::erase_if(g_sight, [now](const auto& a_row) { return now - a_row.second.checked > std::chrono::seconds{ 5 }; });
			const auto playerPosition = player->GetPosition();
			for (const auto& proxy : F4T::ProxyActorController::GetActiveProxyActors()) {
				if (labels.size() >= kMaxLabels) {
					break;
				}
				if (!proxy.actor || !proxy.actor->Get3D() || !F4T::VoiceRuntime::IsPlayerSpeaking(proxy.remotePlayerId) ||
					F4T::VoiceRuntime::IsPlayerMuted(proxy.remotePlayerId)) {
					continue;
				}
				auto head = proxy.actor->GetPosition();
				if (!(F4T::Downed::Engine::DistanceBetween(playerPosition, head) < kMaxDistance)) {
					continue;
				}
				if (!HasSight(*player, proxy.remotePlayerId, *proxy.actor, now)) {
					continue;
				}
				head.z += kHeadHeight;
				const auto point = F4T::OverheadProjection::ProjectWorldPoint(head.x, head.y, head.z, 1.0F, 1.0F);
				if (!point) {
					continue;
				}
				labels.push_back(nlohmann::json{
					{ "id", proxy.remotePlayerId },
					{ "text", "" },
					{ "icon", "voice" },
					{ "x", point->x },
					{ "y", point->y },
					{ "opacity", 1.0 } });
			}
			return labels;
		}

		void Push(const std::string& a_payload)
		{
			auto* api = Api();
			if (!api || !api->PushNameplates || a_payload == g_lastPayload) {
				return;
			}
			api->PushNameplates(a_payload.c_str());
			g_lastPayload = a_payload;
		}
	}

	void Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < g_nextPush) {
			return;
		}
		g_nextPush = now + kPushPeriod;
		Push(BuildPayload().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
	}
}
