#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TDebugLogging.h"
#include "F4TIntroQuestProbe.h"
#include "F4TNpcActorGuard.h"
#include "F4TNpcSyncHealth.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TProxySaveSafety.h"
#include "F4TRemoteNpcState.h"

#include "RE/N/NEW_REFR_DATA.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <unordered_map>

namespace F4T::NpcSync::Detail
{
	namespace
	{
		constexpr auto kRemoteSnapshotTimeout = 2s;
		constexpr auto kSpawnSearchTimeout = 2s;
		constexpr float kPositionLerpAlpha = 0.35F;
		constexpr std::uint8_t kMaxDeathAttempts = 5;
		constexpr auto kDeathRetryInterval = 250ms;

		struct PendingSpawn
		{
			std::uint32_t npcId{};
			std::uint32_t baseFormId{};
			std::uint32_t sourceFormId{};
			RE::ObjectRefHandle handle;
			Clock::time_point startedAt{};
		};

		std::unordered_map<std::uint32_t, std::uint32_t> g_createdPuppetBases;
		std::optional<PendingSpawn> g_pendingSpawn;

		void RecordCreatedPuppet(const RE::TESObjectREFR& a_ref, std::uint32_t a_baseFormId)
		{
			g_createdPuppetBases[a_ref.GetFormID()] = a_baseFormId;
		}

		void DeletePuppet(RE::TESObjectREFR& a_ref)
		{
			g_createdPuppetBases.erase(a_ref.GetFormID());
			F4T::ProxySaveSafety::ForgetStandIn(a_ref.GetFormID());
			a_ref.Disable();
			a_ref.MarkAsDeleted();
		}

		void DeletePendingSpawn()
		{
			if (g_pendingSpawn) {
				if (const auto ref = g_pendingSpawn->handle.get()) {
					DeletePuppet(*ref);
				}
				g_pendingSpawn.reset();
			}
		}

		void NeutralizePuppet(RE::Actor& a_actor)
		{
			if (a_actor.IsInCombat()) {
				a_actor.StopCombat();
			}
			a_actor.ClearAttackStates();
		}

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 N4.5 w3-n45-killimpl-body-{og,ng,ae} VERIFIED, w3-n45-essential-kill-ae PARTIAL, review item 2 early outs)
		bool ApplyPuppetDeath(RE::Actor& a_puppet, const RE::PlayerCharacter& a_player)
		{
			if (std::addressof(a_puppet) == std::addressof(a_player) ||
				g_suppressedActors.contains(a_puppet.GetFormID()) ||
				!IsPuppetActor(a_puppet.GetFormID())) {
				return false;
			}
			a_puppet.boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected);
			{
				F4T::NpcActorGuard::ScopedCoKill coKill;
				a_puppet.KillImpl(nullptr, 0.0F, false, false);
			}
			return a_puppet.IsDead(true);
		}

		bool BeginSpawn(const NpcState& a_state, RE::PlayerCharacter& a_player)
		{
			auto* sourceActor = FindMatchingSourceActor(a_state, a_player);
			if (!sourceActor) {
				return false;
			}

			auto* baseNpc = F4T::NpcSync::ResolveStableBase(*sourceActor);
			auto* parentCell = sourceActor->GetParentCell();
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			auto* boundObject = baseNpc ? baseNpc->As<RE::TESBoundObject>() : nullptr;
			if (!baseNpc || !parentCell || !dataHandler || !boundObject) {
				return false;
			}

			RE::NEW_REFR_DATA spawnData;
			spawnData.object = boundObject;
			spawnData.location = { a_state.x, a_state.y, a_state.z };
			spawnData.direction = { 0.0F, 0.0F, a_state.angleZ };
			spawnData.reference = nullptr;
			spawnData.forcePersist = false;
			spawnData.initializeScripts = false;
			spawnData.initiallyDisabled = false;
			if (parentCell->IsExterior()) {
				spawnData.interior = nullptr;
				spawnData.world = parentCell->worldSpace;
			} else {
				spawnData.interior = parentCell;
				spawnData.world = nullptr;
			}

			auto handle = dataHandler->CreateReferenceAtLocation(spawnData);
			if (!handle) {
				return false;
			}
			if (const auto created = handle.get()) {
				RecordCreatedPuppet(*created, a_state.baseFormId);
			}

			g_pendingSpawn = PendingSpawn{
				a_state.npcId,
				a_state.baseFormId,
				a_state.sourceFormId,
				handle,
				Clock::now()
			};
			return true;
		}

		RE::Actor* ResolvePendingSpawn()
		{
			if (!g_pendingSpawn || !g_pendingSpawn->handle) {
				return nullptr;
			}
			const auto ref = g_pendingSpawn->handle.get();
			return ref ? ref->As<RE::Actor>() : nullptr;
		}

		bool IsStateInLocalLocation(const NpcState& a_state, const RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			if (!cell) {
				return false;
			}
			const auto localCell = FormatFormId(cell->GetFormID());
			if (localCell == a_state.cellId) {
				return true;
			}
			return cell->IsExterior() && cell->worldSpace &&
			       FormatFormId(cell->worldSpace->GetFormID()) == a_state.worldspaceId;
		}

		void UpdatePendingSpawn(const std::unordered_map<std::uint32_t, NpcState>& a_states)
		{
			if (!g_pendingSpawn) {
				return;
			}

			const auto state = a_states.find(g_pendingSpawn->npcId);
			if (state == a_states.end()) {
				DeletePendingSpawn();
				return;
			}

			if (auto* actor = ResolvePendingSpawn()) {
				RecordCreatedPuppet(*actor, state->second.baseFormId);
				PuppetSlot slot{
					state->second.npcId,
					state->second.baseFormId,
					state->second.sourceFormId,
					GetActorHandle(*actor),
					false,
					{},
					0,
					{},
					state->second.cellId,
					state->second.worldspaceId
				};
				const RE::NiPoint3 position{ state->second.x, state->second.y, state->second.z };
				actor->SetPosition(position, true);
				actor->SetHeading(state->second.angleZ);
				actor->SetAlpha(1.0F);
				NeutralizePuppet(*actor);
				g_puppetSlots[slot.npcId] = std::move(slot);
				MarkRegistryDirty();
				F4T::DebugLogging::Log(
					"NpcSync",
					"puppet spawned npcId={} actor={:08X} baseFormId={:08X}",
					state->second.npcId,
					actor->GetFormID(),
					state->second.baseFormId);
				g_pendingSpawn.reset();
				return;
			}

			if (Clock::now() - g_pendingSpawn->startedAt >= kSpawnSearchTimeout) {
				REX::WARN(
					"Enemy puppet spawn timed out: npcId={}, baseFormId={:08X}.",
					g_pendingSpawn->npcId,
					g_pendingSpawn->baseFormId);
				DeletePendingSpawn();
			}
		}
	}

	std::unordered_map<std::uint32_t, PuppetSlot> g_puppetSlots;

	RE::ActorHandle GetActorHandle(RE::Actor& a_actor)
	{
		return RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(std::addressof(a_actor));
	}

	bool IsPuppetActor(std::uint32_t a_formId)
	{
		return std::any_of(
			g_puppetSlots.begin(),
			g_puppetSlots.end(),
			[a_formId](const auto& a_entry) {
				const auto actor = a_entry.second.handle.get();
				return actor && actor->GetFormID() == a_formId;
			});
	}

	bool IsRecordedCreatedPuppet(std::uint32_t a_formId)
	{
		return g_createdPuppetBases.contains(a_formId);
	}

	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W5 review: 0xFF puppet is saved as a created ref; no verified save marker, so purge by recorded ref and base)
	void PurgeStalePuppets()
	{
		for (auto iterator = g_createdPuppetBases.begin(); iterator != g_createdPuppetBases.end();) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(iterator->first);
			if (!actor) {
				++iterator;
				continue;
			}
			const auto recordedBase = iterator->second;
			iterator = g_createdPuppetBases.erase(iterator);
			if (actor->IsCreated() && !actor->IsDeleted() && !IsPuppetActor(actor->GetFormID()) &&
				F4T::NpcSync::StableBaseFormId(*actor) == recordedBase) {
				F4T::DebugLogging::Log("NpcSync", "purged stale puppet actor={:08X}", actor->GetFormID());
				DeletePuppet(*actor);
			}
		}
	}

	void ClearPuppets()
	{
		for (auto& [_, slot] : g_puppetSlots) {
			if (const auto actor = slot.handle.get()) {
				DeletePuppet(*actor);
			}
		}
		if (!g_puppetSlots.empty()) {
			g_puppetSlots.clear();
			MarkRegistryDirty();
		}
		DeletePendingSpawn();
	}

	void UpdatePuppets(RE::PlayerCharacter& a_player)
	{
		const auto lastReceived = F4T::RemoteNpcState::GetLastReceivedTime();
		if (lastReceived == Clock::time_point{} || Clock::now() - lastReceived > kRemoteSnapshotTimeout) {
			ClearPuppets();
			RestoreSuppressedActors();
			return;
		}

		std::unordered_map<std::uint32_t, NpcState> states;
		for (const auto& state : F4T::RemoteNpcState::GetSnapshot()) {
			if (!IsStateIdentityUsable(state) || !IsStateInLocalLocation(state, a_player)) {
				continue;
			}

			if (HasLocalFollowerMatch(state, a_player)) {
				continue;
			}

			if (state.isDead &&
				!g_puppetSlots.contains(state.npcId) &&
				!HasMatchingAliveLocalNpc(state, a_player)) {
				continue;
			}
			if (!g_puppetSlots.contains(state.npcId) && !FindMatchingSourceActor(state, a_player)) {
				continue;
			}

			states.emplace(state.npcId, state);
		}

		SuppressDuplicateLocalNpcs(states, a_player);

		for (auto iterator = g_puppetSlots.begin(); iterator != g_puppetSlots.end();) {
			if (!states.contains(iterator->first)) {
				if (iterator->second.missingSince == Clock::time_point{}) {
					iterator->second.missingSince = Clock::now();
					++iterator;
				} else if (Clock::now() - iterator->second.missingSince >= kMissingNpcGrace) {
					if (const auto actor = iterator->second.handle.get()) {
						DeletePuppet(*actor);
					}
					iterator = g_puppetSlots.erase(iterator);
					MarkRegistryDirty();
				} else {
					++iterator;
				}
			} else {
				iterator->second.missingSince = {};
				++iterator;
			}
		}

		UpdatePendingSpawn(states);
		for (const auto& [npcId, state] : states) {
			auto slot = g_puppetSlots.find(npcId);
			if (slot == g_puppetSlots.end()) {
				if (!g_pendingSpawn && BeginSpawn(state, a_player)) {
					F4T::IntroQuestProbe::LogNpcDecision("puppet-spawn", state.sourceFormId, state.baseFormId);
				}
				continue;
			}

			const auto actor = slot->second.handle.get();
			if (!actor) {
				g_puppetSlots.erase(slot);
				MarkRegistryDirty();
				continue;
			}

			NeutralizePuppet(*actor);
			const auto current = actor->GetPosition();
			const RE::NiPoint3 target{ state.x, state.y, state.z };
			const auto next = current + (target - current) * kPositionLerpAlpha;
			actor->SetPosition(next, true);
			actor->SetHeading(state.angleZ);

			const auto deathNow = Clock::now();
			if (state.isDead && !slot->second.deathApplied && slot->second.deathAttempts < kMaxDeathAttempts &&
				deathNow - slot->second.lastDeathAttempt >= kDeathRetryInterval) {
				++slot->second.deathAttempts;
				slot->second.lastDeathAttempt = deathNow;
				slot->second.deathApplied = ApplyPuppetDeath(*actor, a_player);
				F4T::DebugLogging::Log(
					"NpcSync",
					"puppet death npcId={} actor={:08X} attempt={} applied={}",
					npcId,
					actor->GetFormID(),
					slot->second.deathAttempts,
					slot->second.deathApplied);
				F4T::IntroQuestProbe::LogNpcDecision(
					slot->second.deathApplied ? "puppet-death-applied" : "puppet-death-pending",
					state.sourceFormId,
					state.baseFormId);
			}
			F4T::NpcSync::ApplyAuthorityHealth(*actor, state);
		}
	}
}
