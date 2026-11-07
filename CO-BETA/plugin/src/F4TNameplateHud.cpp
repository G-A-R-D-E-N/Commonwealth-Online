#include "pch.h"

#include "F4TNameplateHud.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDownedEngine.h"
#include "F4TOverheadNamePolicy.h"
#include "F4TOverheadProjectionEngine.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TUIPrefs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace F4T::NameplateHud
{
	namespace
	{
		constexpr auto kPushPeriod = std::chrono::milliseconds{ 33 };
		constexpr float kHeadHeight = 125.0F;
		constexpr std::size_t kMaxNameBytes = 48;

		std::chrono::steady_clock::time_point g_nextPush{};
		std::string g_lastPayload{ "[]" };

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
			auto labels = nlohmann::json::array();
			auto* player = F4T::Downed::Engine::LocalPlayer();
			if (!F4T::UIPrefs::Get().showNameplates || !player) {
				return labels;
			}
			const auto proxies = F4T::ProxyActorController::GetActiveProxyActors();
			const auto playerPosition = player->GetPosition();
			std::vector<F4T::OverheadNamePolicy::Candidate> candidates;
			for (const auto& proxy : proxies) {
				if (!proxy.actor || !proxy.actor->Get3D()) {
					continue;
				}
				const auto remote = F4T::RemotePlayerState::GetRemotePlayer(proxy.remotePlayerId);
				if (!remote) {
					continue;
				}
				F4T::OverheadNamePolicy::Candidate candidate;
				candidate.playerId = proxy.remotePlayerId;
				candidate.distance = F4T::Downed::Engine::DistanceBetween(playerPosition, proxy.actor->GetPosition());
				candidate.displayName = F4T::SocialNameplate::ResolveDisplayName(remote->displayName, remote->characterName);
				if (candidate.displayName.size() > kMaxNameBytes) {
					candidate.displayName.resize(kMaxNameBytes);
				}
				candidate.factionTag = remote->factionTag;
				candidate.downed = remote->vitals && (remote->vitals->isDowned || remote->vitals->isDead);
				const auto sitSleep = proxy.actor->DoGetSitSleepState();
				candidate.sleeping = sitSleep >= RE::SIT_SLEEP_STATE::kWantToSleep && sitSleep <= RE::SIT_SLEEP_STATE::kWantToWake;
				candidates.push_back(std::move(candidate));
			}
			for (const auto& label : F4T::OverheadNamePolicy::Select(std::move(candidates), F4T::OverheadNamePolicy::Settings{})) {
				const auto proxy = std::ranges::find(proxies, label.playerId, &F4T::ProxyActorController::ActiveProxyActor::remotePlayerId);
				if (proxy == proxies.end()) {
					continue;
				}
				auto head = proxy->actor->GetPosition();
				head.z += kHeadHeight;
				const auto point = F4T::OverheadProjection::ProjectWorldPoint(head.x, head.y, head.z, 1.0F, 1.0F);
				if (!point) {
					continue;
				}
				labels.push_back(nlohmann::json{
					{ "id", label.playerId },
					{ "text", label.text },
					{ "x", point->x },
					{ "y", point->y },
					{ "opacity", label.opacity } });
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
