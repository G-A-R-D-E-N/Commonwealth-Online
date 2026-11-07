#include "pch.h"

#include "F4TProxyWeaponDraw.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyAnimationSync.h"
#include "F4TWeaponClassification.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorState.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/T/TESObjectWEAP.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>

namespace
{
	constexpr auto kProxyWeaponDrawLogInterval = 1s;
	constexpr double kStatePruneIntervalSeconds = 5.0;

	namespace GraphEvent
	{
		inline constexpr const char* kWeaponDraw = "weaponDraw";
		inline constexpr const char* kReadyStateEnter = "readyStateEnter";
		inline constexpr const char* kGunDownStateEnter = "gunDownStateEnter";
	}

	// Constraint: stale entries are pruned and a visit gap starts a new proxy generation.
	std::unordered_map<std::uint32_t, F4T::ProxyWeaponDraw::State> g_weaponDrawStates;

	double g_lastPruneSeconds = 0.0;

	double SteadySeconds(std::chrono::steady_clock::time_point a_time)
	{
		return std::chrono::duration<double>(a_time.time_since_epoch()).count();
	}

	RE::TESObjectWEAP* GetEquippedRightHandWeapon(RE::Actor& a_proxy, RE::BGSObjectInstance& a_equippedItem)
	{
		RE::BGSEquipIndex rightHandIndex;
		rightHandIndex.index = 0;
		const auto* equipped = a_proxy.GetEquippedItem(&a_equippedItem, rightHandIndex);
		if (!equipped || !equipped->object || !equipped->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
			return nullptr;
		}

		return static_cast<RE::TESObjectWEAP*>(equipped->object);
	}

	void ApplyProxyWeaponDrawDecision(
		RE::Actor& a_proxy,
		RE::TESObjectWEAP& a_weapon,
		RE::TBO_InstanceData* a_instanceData,
		F4T::ProxyWeaponDraw::Decision a_decision)
	{
		const bool drawn = a_decision == F4T::ProxyWeaponDraw::Decision::kDraw;

		RE::BGSEquipIndex equipIndex;
		equipIndex.index = 0;
		if (drawn) {
			RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ &a_weapon, a_instanceData };
			a_proxy.AttachWeapon(weaponInstance, equipIndex);
		}
		a_proxy.DoReparentWeapon(&a_weapon, equipIndex, drawn);
		a_proxy.DrawWeaponMagicHands(drawn);

		if (drawn) {
			F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, GraphEvent::kWeaponDraw);
			F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, GraphEvent::kReadyStateEnter);
			return;
		}

		const auto archetype = F4T::WeaponClassification::ClassifyWeapon(&a_weapon);
		if (F4T::WeaponClassification::IsRangedArchetype(archetype)) {
			F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, GraphEvent::kGunDownStateEnter);
		}
	}
}

namespace F4T::ProxyWeaponDraw
{
	void ReconcileProxyWeaponDraw(
		std::uint32_t a_remotePlayerId,
		RE::Actor& a_proxy,
		const RemotePlayerState::RemotePlayerState& a_remotePlayer,
		bool a_graphSettling)
	{
		if (a_remotePlayerId == 0 ||
			!ProxyActorController::detail::ProxyHasLoaded3D(a_proxy)) {
			return;
		}

		const auto now = SteadySeconds(std::chrono::steady_clock::now());
		if (now - g_lastPruneSeconds > kStatePruneIntervalSeconds) {
			g_lastPruneSeconds = now;
			PruneStale(g_weaponDrawStates, now);
		}

		auto& state = g_weaponDrawStates[a_remotePlayerId];
		BindToProxy(state, a_proxy.GetFormID(), now);
		RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
		auto* weapon = GetEquippedRightHandWeapon(a_proxy, equippedItem);

		const auto stableDesired = ResolveStableDesired(state, now, a_remotePlayer.weaponDrawn);
		if (!stableDesired) {
			return;
		}

		const bool actualDrawn = a_proxy.GetWeaponMagicDrawn();
		const auto decision = Decide(
			*stableDesired,
			actualDrawn,
			weapon != nullptr,
			a_graphSettling,
			state.lastChangeSeconds,
			now,
			state.attempts);

		if (decision == Decision::kNone) {
			if (weapon == nullptr || *stableDesired == actualDrawn) {
				ClearPendingKeepInterval(state);
			}
			return;
		}

		state.lastChangeSeconds = now;
		++state.attempts;

		ApplyProxyWeaponDrawDecision(a_proxy, *weapon, equippedItem.instanceData.get(), decision);

		ProxyActorController::detail::LogThrottledInfo(
			"runtime_proxy_weapon_draw_reconcile_" + std::to_string(a_remotePlayerId),
			std::format(
				"Reconciled proxy weapon draw for remote player {}: actor={:08X}, action={}, actualDrawnBefore={}, attempts={}.",
				a_remotePlayerId,
				a_proxy.GetFormID(),
				decision == Decision::kDraw ? "draw" : "sheathe",
				actualDrawn,
				state.attempts),
			kProxyWeaponDrawLogInterval);
	}
}
