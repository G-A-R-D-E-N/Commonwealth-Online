// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/main.cpp (body AV setup) and sync/CoopSync.cpp (HitSink).
// TODO-verify(#568) source:E:/GardenEngine/re-answers/2026-10-27-fs-phase1-deferred-facts.md
#include "pch.h"

#include "F4TProxyLiveFireGuard.h"

#include "F4TDebugLogging.h"
#include "F4THookThreadTable.h"
#include "F4TProxyPuppet.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorValue.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESHitEvent.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace F4T::ProxyLiveFire
{
	namespace
	{
		bool IsProxyFormID(std::uint32_t a_formID)
		{
			try {
				return ProxyPuppet::IsProxyFormID(a_formID);
			} catch (...) {
				return false;
			}
		}

		class GiveBackSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				F4T::HookThread::Note(F4T::HookThread::Id::LiveFireGiveBack);
				auto* localPlayer = RE::PlayerCharacter::GetSingleton();
				if (!localPlayer || !a_event.usesHitData) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto aggressor = a_event.hitData.aggressor.get();
				const auto victim = a_event.hitData.target.get();
				if (!aggressor || victim.get() != localPlayer) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto healthDamage = a_event.hitData.healthDamage;
				if (!std::isfinite(healthDamage) || healthDamage <= 0.0F || !IsProxyFormID(aggressor->GetFormID())) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* actorValues = RE::ActorValue::GetSingleton();
				if (!actorValues || !actorValues->health) {
					return RE::BSEventNotifyControl::kContinue;
				}
				localPlayer->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, healthDamage);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		GiveBackSink g_giveBackSink;
		std::atomic<bool> g_sinkRegistered{ false };

		// TODO-verify(#568): attackDamageMult scaling of a proxy's live shot and ammo consumption are unverified.
		bool SuppressDamage(RE::Actor* a_proxy)
		{
			auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->attackDamageMult) {
				return false;
			}
			if (a_proxy->GetBaseActorValue(*actorValues->attackDamageMult) != 0.0F) {
				a_proxy->SetBaseActorValue(*actorValues->attackDamageMult, 0.0F);
			}
			return a_proxy->GetBaseActorValue(*actorValues->attackDamageMult) == 0.0F;
		}
	}

	bool Arm(RE::Actor* a_proxy)
	{
		if (!a_proxy) {
			return false;
		}
		if (!g_sinkRegistered.exchange(true)) {
			auto* eventSource = RE::TESHitEvent::GetEventSource();
			if (!eventSource) {
				g_sinkRegistered = false;
				return false;
			}
			eventSource->RegisterSink(&g_giveBackSink);
			F4T::DebugLogging::Write("ProxyLiveFire", "TESHitEvent give-back sink registered");
		}
		return SuppressDamage(a_proxy);
	}
}
