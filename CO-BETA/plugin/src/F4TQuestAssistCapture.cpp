#include "pch.h"

#include "F4TQuestAssistCapture.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TQuestAssistOrigin.h"
#include "F4TQuestFormKey.h"
#include "F4TQuestInbound.h"
#include "F4TQuestPapyrus.h"
#include "F4TQuestWire.h"

#include "RE/B/BGSBaseAlias.h"
#include "RE/B/BGSQuestObjective.h"
#include "RE/B/BGSRefAlias.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/Q/QUEST_OBJECTIVE_STATE.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESDeathEvent.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESQuestTarget.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using F4T::QuestAssistOrigin::ObjectiveTarget;

	struct PendingDeath
	{
		std::uint32_t actorFormId{};
		std::uint64_t nowMilliseconds{};
	};

	std::atomic<bool> g_registered{ false };
	std::mutex g_pendingMutex;
	std::vector<PendingDeath> g_pendingDeaths;

	std::uint64_t NowMilliseconds()
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
				.count());
	}

	std::uint32_t SourceFormIdOf(std::uint32_t a_actorFormId)
	{
		if (const auto puppet = F4T::NpcSync::TryGetPuppetByActorFormId(a_actorFormId)) {
			return puppet->sourceFormId;
		}
		if (const auto host = F4T::NpcSync::TryGetHostNpcByActorFormId(a_actorFormId)) {
			return host->sourceFormId;
		}
		return 0;
	}

	RE::BGSBaseAlias* FindAliasById(const RE::TESQuest* a_quest, std::uint32_t a_aliasId)
	{
		for (auto* alias : a_quest->aliases) {
			if (alias != nullptr && alias->aliasID == a_aliasId) {
				return alias;
			}
		}
		return nullptr;
	}

	void CollectObjectiveTargets(
		const RE::TESQuest* a_quest,
		std::string_view a_key,
		std::vector<ObjectiveTarget>& a_targets)
	{
		if (a_quest == nullptr) {
			return;
		}
		for (auto* objective : a_quest->objectives) {
			if (objective == nullptr || objective->targets == nullptr) {
				continue;
			}
			if (static_cast<std::int32_t>(objective->state) != static_cast<std::int32_t>(RE::QUEST_OBJECTIVE_STATE::kDisplayed)) {
				continue;
			}
			ObjectiveTarget target{ std::string{ a_key }, objective->index, {} };
			for (std::uint32_t index = 0; index < objective->numTargets; ++index) {
				auto* entry = objective->targets[index];
				if (entry == nullptr) {
					continue;
				}
				auto* alias = FindAliasById(a_quest, entry->aliasID);
				if (alias == nullptr || std::string_view{ alias->QType().c_str() } != RE::BGSRefAlias::TYPE) {
					continue;
				}
				auto* reference = static_cast<RE::BGSRefAlias*>(alias)->GetReference();
				if (reference != nullptr && reference->GetFormID() != 0) {
					target.actorFormIds.push_back(reference->GetFormID());
				}
			}
			if (!target.actorFormIds.empty()) {
				a_targets.push_back(std::move(target));
			}
		}
	}

	void OriginateAssists(const F4T::QuestAssistOrigin::DeathWithHit& a_death, std::uint64_t a_nowMilliseconds)
	{
		if (!F4T::Networking::IsConnectedToServer() || !F4T::QuestInbound::IsActive()) {
			return;
		}
		const auto loadOrder = F4T::QuestFormKey::GetLoadOrder();
		if (loadOrder.empty()) {
			return;
		}
		const auto targetKey = F4T::QuestFormKey::ToKey(a_death.deadFormId, loadOrder);
		if (!targetKey) {
			return;
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (dataHandler == nullptr) {
			return;
		}
		std::vector<ObjectiveTarget> targets;
		for (const auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
			if (quest == nullptr) {
				continue;
			}
			const auto report = F4T::QuestPapyrus::FindReport(quest->GetFormID());
			if (!report || !report->running || report->completed) {
				continue;
			}
			const auto key = F4T::QuestFormKey::ToKey(quest->GetFormID(), loadOrder);
			if (!key) {
				continue;
			}
			CollectObjectiveTargets(quest, *key, targets);
		}
		const auto match = F4T::QuestAssistOrigin::MatchActorObjectives(targets, a_death.deadFormId, a_death.hit, a_nowMilliseconds);
		if (!match) {
			return;
		}
		for (const auto& candidate : match->candidates) {
			const auto packet = F4T::QuestWire::EncodeAssist(F4T::QuestWire::QuestAssistOut{
				match->hit.sequence,
				candidate.quest,
				candidate.objectiveId,
				"killActor",
				*targetKey,
				match->hit.attackSequence });
			if (!F4T::Networking::Detail::SendPacketBytes(packet.dump())) {
				F4T::DebugLogging::Log("QuestSync", "could not send an assist for quest={}", candidate.quest);
			}
		}
	}

	class AssistDeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESDeathEvent& a_event,
			RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (!a_event.dead || !F4T::QuestAssistOrigin::Enabled()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* dying = a_event.actorDying.get();
			const auto* killer = a_event.actorKiller.get();
			if (player == nullptr || dying == nullptr || killer == nullptr || killer->GetFormID() != player->GetFormID()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			std::lock_guard lock(g_pendingMutex);
			if (g_pendingDeaths.size() < F4T::QuestAssistOrigin::kMaximumPendingDeaths) {
				g_pendingDeaths.push_back(PendingDeath{ dying->GetFormID(), NowMilliseconds() });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	AssistDeathSink g_sink;
}

namespace F4T::QuestAssistCapture
{
	void SetOriginating(bool a_originating)
	{
		if (a_originating && !g_registered.load(std::memory_order_acquire)) {
			if (auto* source = RE::TESDeathEvent::GetEventSource()) {
				source->RegisterSink(&g_sink);
				g_registered.store(true, std::memory_order_release);
				F4T::DebugLogging::Log("QuestSync", "quest assist origination enabled");
			}
		}
		F4T::QuestAssistOrigin::SetEnabled(a_originating);
	}

	void NoteSentNpcHit(
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		std::uint32_t a_sourceFormId,
		std::uint64_t a_nowMilliseconds)
	{
		F4T::QuestAssistOrigin::RecordSentHit(a_sequence, a_attackSequence, a_sourceFormId, a_nowMilliseconds);
	}

	bool CreditTargetMatches(
		std::string_view a_questKey,
		std::uint32_t a_resolvedQuestFormId,
		std::uint32_t a_objectiveId,
		std::string_view a_targetKey)
	{
		const auto loadOrder = F4T::QuestFormKey::GetLoadOrder();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (a_targetKey.empty() || loadOrder.empty() || dataHandler == nullptr) {
			return false;
		}
		const RE::TESQuest* quest = nullptr;
		for (const auto* candidate : dataHandler->GetFormArray<RE::TESQuest>()) {
			if (candidate != nullptr && candidate->GetFormID() == a_resolvedQuestFormId) {
				quest = candidate;
				break;
			}
		}
		if (quest == nullptr) {
			return false;
		}
		std::vector<ObjectiveTarget> targets;
		CollectObjectiveTargets(quest, a_questKey, targets);
		return F4T::QuestAssistOrigin::ObjectiveTargetsContain(
			targets,
			a_objectiveId,
			a_targetKey,
			[&loadOrder](std::uint32_t a_actorFormId) {
				std::vector<std::string> keys;
				if (const auto key = F4T::QuestFormKey::ToKey(a_actorFormId, loadOrder)) {
					keys.push_back(*key);
				}
				if (const auto sourceFormId = SourceFormIdOf(a_actorFormId); sourceFormId != 0) {
					if (const auto key = F4T::QuestFormKey::ToKey(sourceFormId, loadOrder)) {
						keys.push_back(*key);
					}
				}
				return keys;
			});
	}

	void Process(std::uint64_t a_nowMilliseconds)
	{
		std::vector<PendingDeath> pending;
		{
			std::lock_guard lock(g_pendingMutex);
			pending.swap(g_pendingDeaths);
		}
		for (const auto& death : pending) {
			const auto sourceFormId = SourceFormIdOf(death.actorFormId);
			if (sourceFormId != 0) {
				F4T::QuestAssistOrigin::QueueDeath(sourceFormId, death.nowMilliseconds);
			}
		}
		while (const auto death = F4T::QuestAssistOrigin::TakeDeathWithHit(a_nowMilliseconds)) {
			OriginateAssists(*death, a_nowMilliseconds);
		}
	}

	void Clear()
	{
		{
			std::lock_guard lock(g_pendingMutex);
			g_pendingDeaths.clear();
		}
		F4T::QuestAssistOrigin::Clear();
	}
}
