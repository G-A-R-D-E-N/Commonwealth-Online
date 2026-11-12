#include "pch.h"

#include "F4TProxyVitals.h"

#include "F4TDebugLogging.h"
#include "F4TDownedEngine.h"
#include "F4TNpcActorGuard.h"

#include <memory>

namespace
{
	constexpr double kResurrectRetrySeconds{ 2.0 };
	constexpr double kPoseRetrySeconds{ 2.0 };
	constexpr double kHealthReassertSeconds{ 1.0 };

	void KillProxy(RE::Actor& a_proxy, F4T::ProxyVitals::State& a_state)
	{
		a_state.restoreEssential = a_proxy.boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential);
		a_state.restoreProtected = a_proxy.boolFlags.any(RE::Actor::BOOL_FLAGS::kProtected);
		a_proxy.boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected);
		{
			F4T::NpcActorGuard::ScopedCoKill coKill;
			a_proxy.KillImpl(nullptr, 0.0F, false, false);
		}
		F4T::DebugLogging::Log(
			"ProxyVitals",
			"proxy {:08X} killed to match its remote player's death: dead={}",
			a_proxy.GetFormID(),
			a_proxy.IsDead(true));
	}

	void ResurrectProxy(RE::Actor& a_proxy, F4T::ProxyVitals::State& a_state)
	{
		a_proxy.Resurrect(false, true);
		if (a_state.restoreEssential) {
			a_proxy.boolFlags.set(RE::Actor::BOOL_FLAGS::kEssential);
		}
		if (a_state.restoreProtected) {
			a_proxy.boolFlags.set(RE::Actor::BOOL_FLAGS::kProtected);
		}
		a_state.restoreEssential = false;
		a_state.restoreProtected = false;
		a_state.poseDowned = false;
		a_state.appliedHealthPercent.reset();
		F4T::DebugLogging::Log(
			"ProxyVitals",
			"proxy {:08X} resurrected while its remote player is alive: dead={}",
			a_proxy.GetFormID(),
			a_proxy.IsDead(true));
	}

	bool ReconcileHealth(RE::Actor& a_proxy, std::int32_t a_healthPercent)
	{
		const auto* health = F4T::Downed::Engine::HealthInfo();
		if (!health) {
			return false;
		}
		const auto maximum = F4T::Downed::Engine::GetMaximumHealth(std::addressof(a_proxy), *health);
		if (!(maximum > 0.0F)) {
			return false;
		}
		F4T::NpcActorGuard::ScopedCoWrite coWrite;
		return F4T::Downed::Engine::SetHealth(
			a_proxy,
			*health,
			F4T::PlayerVitals::ComputeTargetHealth(maximum, a_healthPercent));
	}

	bool ProxyHealthDrifted(RE::Actor& a_proxy, std::int32_t a_healthPercent)
	{
		const auto* health = F4T::Downed::Engine::HealthInfo();
		if (!health) {
			return false;
		}
		const auto maximum = F4T::Downed::Engine::GetMaximumHealth(std::addressof(a_proxy), *health);
		if (!(maximum > 0.0F)) {
			return false;
		}
		const auto current = F4T::PlayerVitals::ComputeHealthPercent(a_proxy.GetActorValue(*health), maximum);
		return current && (*current - a_healthPercent > 1 || a_healthPercent - *current > 1);
	}
}

namespace F4T::ProxyVitals
{
	void Apply(RE::Actor& a_proxy, State& a_state, const F4T::PlayerVitals::Vitals& a_vitals)
	{
		if (std::addressof(a_proxy) == RE::PlayerCharacter::GetSingleton()) {
			return;
		}
		if (!F4T::Downed::Engine::CanTouchWorld()) {
			return;
		}
		if (a_vitals.isDead) {
			a_state.poseDowned = false;
			if (!a_state.killed) {
				a_state.killed = true;
				if (!a_proxy.IsDead(true)) {
					KillProxy(a_proxy, a_state);
				}
			}
			return;
		}
		a_state.killed = false;
		if (a_proxy.IsDead(true)) {
			const auto now = F4T::Downed::Engine::NowSeconds();
			if (!a_state.lastResurrectSeconds || now - *a_state.lastResurrectSeconds >= kResurrectRetrySeconds) {
				a_state.lastResurrectSeconds = now;
				ResurrectProxy(a_proxy, a_state);
			}
			return;
		}
		if (a_vitals.isDowned != a_state.poseDowned) {
			const auto now = F4T::Downed::Engine::NowSeconds();
			if (now >= a_state.nextPoseRetrySeconds) {
				a_state.nextPoseRetrySeconds = now + kPoseRetrySeconds;
				if (F4T::Downed::Engine::SetPose(a_proxy, a_vitals.isDowned)) {
					a_state.poseDowned = a_vitals.isDowned;
				}
			}
		}
		if (a_state.appliedHealthPercent == a_vitals.healthPercent) {
			const auto now = F4T::Downed::Engine::NowSeconds();
			if (now < a_state.nextHealthReassertSeconds || !ProxyHealthDrifted(a_proxy, a_vitals.healthPercent)) {
				return;
			}
			a_state.nextHealthReassertSeconds = now + kHealthReassertSeconds;
		}
		if (ReconcileHealth(a_proxy, a_vitals.healthPercent)) {
			a_state.appliedHealthPercent = a_vitals.healthPercent;
		}
	}
}
