#include "pch.h"

#include "F4TFurnitureSeatSync.h"

#include "F4TAnimationGraphOverride.h"
#include "F4TFurnitureSync.h"
#include "F4TLocalPlayerTransformSend.h"
#include "F4TNetworking.h"
#include "F4TProxyActorController.h"

#include "RE/A/Actor.h"
#include "RE/A/AIProcess.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PowerArmor.h"
#include "RE/S/SIT_SLEEP_STATE.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>

namespace F4T::FurnitureSeatSync
{
	namespace
	{
		constexpr std::size_t kMaximumQueued = 64;
		constexpr const char* kEnterEvent = "sitStartFromStand";
		constexpr const char* kExitEvent = "sitStop";

		// Game thread only: the seat state the server last accepted from this client.
		bool g_sentSeated = false;
		std::uint64_t g_sentGeneration = 0;
		std::mutex g_inboundMutex;
		std::deque<F4T::FurnitureSync::RelayedUse> g_inbound;
		constexpr std::chrono::seconds kPendingWindow{ 30 };

		// Game thread only: the latest seat event per remote player whose proxy did not exist yet.
		struct PendingUse
		{
			F4T::FurnitureSync::Action action{ F4T::FurnitureSync::Action::Enter };
			std::chrono::steady_clock::time_point deadline{};
		};
		std::map<std::uint32_t, PendingUse> g_pending;
		std::uint64_t g_pendingGeneration = 0;

		constexpr bool IsSleepingState(RE::SIT_SLEEP_STATE a_state)
		{
			return a_state >= RE::SIT_SLEEP_STATE::kWantToSleep && a_state <= RE::SIT_SLEEP_STATE::kWantToWake;
		}
		static_assert(!IsSleepingState(RE::SIT_SLEEP_STATE::kIsSitting));
		static_assert(IsSleepingState(RE::SIT_SLEEP_STATE::kIsSleeping));
		static_assert(IsSleepingState(RE::SIT_SLEEP_STATE::kWantToWake));

		bool LocalPlayerSeated()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->currentProcess || RE::PowerArmor::ActorInPowerArmor(*player) ||
				IsSleepingState(player->DoGetSitSleepState())) {
				return false;
			}
			auto furniture = player->currentProcess->GetOccupiedFurniture();
			return furniture.get().get() != nullptr;
		}

		void PlayOnProxy(RE::Actor* a_proxy, F4T::FurnitureSync::Action a_action)
		{
			F4T::AnimationGraphOverride::SendGraphEvent(
				a_proxy,
				a_action == F4T::FurnitureSync::Action::Enter ? kEnterEvent : kExitEvent);
		}
	}

	void PublishLocal()
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		const bool connected = F4T::Networking::IsConnectedToServer();
		if (generation != g_sentGeneration || !connected) {
			g_sentSeated = false;
			g_sentGeneration = generation;
		}
		if (connected && F4T::LocalPlayerTransformSend::HasSentTransformThisConnection()) {
			const bool seated = LocalPlayerSeated();
			const auto action = seated ? F4T::FurnitureSync::Action::Enter : F4T::FurnitureSync::Action::Exit;
			if (seated != g_sentSeated && F4T::Networking::SendFurnitureUsePacket(F4T::FurnitureSync::ActionName(action))) {
				g_sentSeated = seated;
			}
		}

	}

	void Tick()
	{
		std::deque<F4T::FurnitureSync::RelayedUse> inbound;
		{
			std::lock_guard lock{ g_inboundMutex };
			inbound.swap(g_inbound);
		}
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (generation != g_pendingGeneration) {
			g_pending.clear();
			g_pendingGeneration = generation;
		}
		const auto now = std::chrono::steady_clock::now();
		for (const auto& use : inbound) {
			if (auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(use.playerId)) {
				g_pending.erase(use.playerId);
				PlayOnProxy(proxy, use.action);
			} else {
				g_pending[use.playerId] = PendingUse{ use.action, now + kPendingWindow };
			}
		}
		for (auto it = g_pending.begin(); it != g_pending.end();) {
			if (auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(it->first)) {
				PlayOnProxy(proxy, it->second.action);
				it = g_pending.erase(it);
			} else if (now >= it->second.deadline) {
				it = g_pending.erase(it);
			} else {
				++it;
			}
		}
	}

	void HandleRelayedPacket(const nlohmann::json& a_packet)
	{
		const auto use = F4T::FurnitureSync::ParseRelayedUse(a_packet);
		if (!use) {
			return;
		}
		std::lock_guard lock{ g_inboundMutex };
		if (g_inbound.size() < kMaximumQueued) {
			g_inbound.push_back(*use);
		}
	}
}
