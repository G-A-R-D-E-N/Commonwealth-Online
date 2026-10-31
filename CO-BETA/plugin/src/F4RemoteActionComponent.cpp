#include "pch.h"

#include "F4RemoteActionComponent.h"

#include "F4AnimationDescriptor.h"
#include "F4TConsoleGuard.h"
#include "F4TCombatActionContract.h"
#include "F4TDebugLogging.h"
#include "F4TProxyAnimationSync.h"
#include "F4TProxyAttackHitGuard.h"
#include "F4TWeaponClassification.h"
#include "RE/A/Actor.h"
#include "RE/A/ActorState.h"
#include "RE/C/Console.h"
#include "RE/T/TESObjectWEAP.h"

#include <format>

namespace F4T::RemoteActionComponent
{
	namespace
	{
		void LogInfoMessage(const std::string& a_message)
		{
			F4T::DebugLogging::Write("RemoteAction", a_message);
		}

		void LogWarningMessage(const std::string& a_message)
		{
			REX::WARN(std::string_view{ a_message });
		}

		void EnsureProxyReadyToFire(RE::Actor& a_proxy, bool& a_alertSent)
		{
			a_proxy.boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackingDisabled);
			if (a_alertSent) {
				return;
			}
			a_alertSent = true;
			F4T::ConsoleGuard::ExecutePluginCommand(std::format("{:08X}.setalert 1", a_proxy.GetFormID()).c_str());
		}
	}

	void RemoteActionQueue::Enqueue(const RemoteActionSnapshot& a_action)
	{
		if (m_actionQueue.size() >= m_maxQueueSize) {
			LogWarningMessage(std::format(
				"RemoteActionQueue: Action queue full (max={}); dropping oldest action to make room.",
				m_maxQueueSize));
			m_actionQueue.erase(m_actionQueue.begin());
			m_readyAlertSentForFront = false;
		}
		if (m_actionQueue.empty()) {
			m_readyAlertSentForFront = false;
		}

		m_actionQueue.push_back(a_action);

		LogInfoMessage(std::format(
			"RemoteActionQueue: Enqueued action '{}' (type={}); queue size={}",
			a_action.eventName,
			a_action.actionType,
			m_actionQueue.size()));
	}

	void RemoteActionQueue::Clear()
	{
		if (!m_actionQueue.empty()) {
			LogInfoMessage(std::format(
				"RemoteActionQueue: Cleared {} pending actions.",
				m_actionQueue.size()));
			m_actionQueue.clear();
			m_readyAlertSentForFront = false;
		}
	}

	bool RemoteActionQueue::ReplayNextAction(RE::Actor& a_proxy)
	{
		bool replayed = false;
		while (!m_actionQueue.empty()) {
			const auto action = m_actionQueue.front();
			const auto applied = ApplyActionToProxy(a_proxy, action);
			if (F4T::CombatActionContract::ShouldRetryReplay(
					applied,
					F4T::CombatActionContract::IsPermanentReplayFailure(
						action.eventName.empty(),
						F4T::ProxyAttackHitGuard::IsInstalled()),
					action.captureTime,
					std::chrono::steady_clock::now())) {
				break;
			}

			m_actionQueue.erase(m_actionQueue.begin());
			m_readyAlertSentForFront = false;
			if (applied) {
				replayed = true;
				LogInfoMessage(std::format(
					"RemoteActionQueue: Replayed action '{}' on proxy {:08X}; {} pending.",
					action.eventName,
					a_proxy.GetFormID(),
					m_actionQueue.size()));
			} else if (F4T::ProxyAttackHitGuard::IsInstalled()) {
				LogWarningMessage(std::format(
					"RemoteActionQueue: Dropped action '{}' on proxy {:08X}; empty or past retry window; {} pending.",
					action.eventName,
					a_proxy.GetFormID(),
					m_actionQueue.size()));
			}
		}
		return replayed;
	}

	bool RemoteActionQueue::ApplyActionToProxy(RE::Actor& a_proxy, const RemoteActionSnapshot& a_action)
	{
		try {
			return TriggerActionAnimation(a_proxy, a_action);
		} catch (const std::exception& e) {
			LogWarningMessage(std::format(
				"RemoteActionQueue: Exception applying action to proxy {:08X}: {}",
				a_proxy.GetFormID(),
				e.what()));
			return false;
		}
	}

	bool RemoteActionQueue::TriggerActionAnimation(
		RE::Actor& a_proxy,
		const RemoteActionSnapshot& a_action)
	{
		if (a_action.eventName.empty()) {
			return false;
		}

		if (!F4T::ProxyAttackHitGuard::IsInstalled()) {
			static bool hitGuardRefusalLogged = false;
			if (!hitGuardRefusalLogged) {
				hitGuardRefusalLogged = true;
				LogWarningMessage("RemoteActionQueue: Proxy attack animation replay refused; the HitFrame and WeaponFire hook guard is not installed.");
			}
			return false;
		}

		if (a_action.actionType == 3U) {
			RE::BGSEquipIndex rightHandIndex;
			rightHandIndex.index = 0;
			RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
			const auto* item = a_proxy.GetEquippedItem(&equippedItem, rightHandIndex);
			if (!item || !item->object || !item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
				return false;
			}

			auto* weapon = static_cast<RE::TESObjectWEAP*>(item->object);
			const auto weaponType = F4T::WeaponClassification::ClassifyWeapon(weapon);
			if (!F4T::WeaponClassification::IsRangedArchetype(weaponType)) {
				return false;
			}

			EnsureProxyReadyToFire(a_proxy, m_readyAlertSentForFront);
			// source:E:/GardenEngine/re-answers/2026-10-20-hitframe-weaponfire-handler-vtables.md (Ranged graph events, row co309-npc6-ranged-transition-conditions): constraint
			const auto graphAccepted = F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, "attackStart");
			if (graphAccepted) {
				LogInfoMessage(std::format(
					"RemoteActionQueue: Replayed visual-only ranged fire on proxy {:08X} with weapon {:08X}: graphAccepted={}.",
					a_proxy.GetFormID(),
					weapon->GetFormID(),
					graphAccepted));
			}
			return graphAccepted;
		}

		const auto graphAccepted = F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, a_action.eventName);
		if (graphAccepted) {
			LogInfoMessage(std::format(
				"RemoteActionQueue: Triggered action '{}' (type={}) on proxy {:08X}: graphAccepted={}.",
				a_action.eventName,
				a_action.actionType,
				a_proxy.GetFormID(),
				graphAccepted));
		}

		return graphAccepted;
	}
}
