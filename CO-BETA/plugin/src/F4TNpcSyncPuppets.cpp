#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TCrashBreadcrumbs.h"
#include "F4TDebugLogging.h"
#include "F4TIntroQuestProbe.h"
#include "F4TNetworking.h"
#include "F4TNpcActorGuard.h"
#include "F4TNpcSyncHealth.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncPolicy.h"
#include "F4TNpcSyncSweepPolicy.h"
#include "F4TProxySaveSafety.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"

#include "RE/N/NEW_REFR_DATA.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

namespace F4T::NpcSync::Detail
{
	namespace
	{
		constexpr auto kSpawnSearchTimeout = 2s;
		constexpr float kPositionLerpAlpha = 0.35F;
		constexpr float kAliasCorrectionDistance = 1024.0F;
		constexpr float kRuntimeAdoptDistance = 512.0F;

		struct PendingSpawn
		{
			NpcKey key;
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

		template <class TRef>
		F4T::NpcSyncSweep::PuppetDeleteOutcome DeletePuppetIfLive(const TRef& a_ref)
		{
			const bool resolved = static_cast<bool>(a_ref);
			RE::TESObjectREFR* ref = resolved ? std::addressof(*a_ref) : nullptr;
			const auto outcome = F4T::NpcSyncSweep::DecidePuppetDelete(resolved, ref && ref->IsDeleted());
			if (ref) {
				g_createdPuppetBases.erase(ref->GetFormID());
				F4T::ProxySaveSafety::ForgetStandIn(ref->GetFormID());
			}
			if (outcome == F4T::NpcSyncSweep::PuppetDeleteOutcome::kDeleted) {
				ref->Disable();
				ref->MarkAsDeleted();
			}
			F4T::CrashBreadcrumbs::Record(
				F4T::CrashBreadcrumbs::Code::kPuppetDelete,
				ref ? ref->GetFormID() : 0U,
				static_cast<std::uint32_t>(outcome));
			return outcome;
		}

		void DeletePuppet(RE::TESObjectREFR& a_ref)
		{
			(void)DeletePuppetIfLive(std::addressof(a_ref));
		}

		std::uint64_t NowMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
		}

		F4T::NpcSyncSweep::ThrottledTally g_staleSweepLog;

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

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 N4.5: the KillImpl call must match all runtimes)
		bool KillSyncedNpc(RE::Actor& a_actor)
		{
			a_actor.boolFlags.reset(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected);
			{
				F4T::NpcActorGuard::ScopedCoKill coKill;
				a_actor.KillImpl(nullptr, 0.0F, false, false);
			}
			return a_actor.IsDead(true);
		}

		bool ApplyPuppetDeath(RE::Actor& a_puppet, const RE::PlayerCharacter& a_player)
		{
			if (std::addressof(a_puppet) == std::addressof(a_player) ||
				g_suppressedActors.contains(a_puppet.GetFormID()) ||
				!IsPuppetActor(a_puppet.GetFormID())) {
				return false;
			}
			return KillSyncedNpc(a_puppet);
		}

		bool BeginSpawn(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
		{
			auto* sourceActor = FindMatchingSourceActor(a_key, a_state, a_player, a_loadedActors);
			auto* baseNpc = sourceActor ? F4T::NpcSync::ResolveStableBase(*sourceActor) : RE::TESForm::GetFormByID<RE::TESNPC>(a_state.baseFormId);
			auto* parentCell = sourceActor ? sourceActor->GetParentCell() : a_player.GetParentCell();
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
				a_key,
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

		bool IsCellInStateLocation(const RE::TESObjectCELL* a_cell, const NpcState& a_state)
		{
			if (!a_cell) {
				return false;
			}
			if (FormatFormId(a_cell->GetFormID()) == a_state.cellId) {
				return true;
			}
			return a_cell->IsExterior() && a_cell->worldSpace &&
			       FormatFormId(a_cell->worldSpace->GetFormID()) == a_state.worldspaceId;
		}

		bool IsStateInLocalLocation(const NpcState& a_state, const RE::PlayerCharacter& a_player)
		{
			return IsCellInStateLocation(a_player.GetParentCell(), a_state);
		}

		bool IsBusyWithScriptedAction(RE::Actor& a_actor)
		{
			const auto* process = a_actor.currentProcess;
			const auto* high = process ? process->high : nullptr;
			return a_actor.boolFlags.any(RE::Actor::BOOL_FLAGS::kScenePackage, RE::Actor::BOOL_FLAGS::kInRandomScene) ||
			       (high && (high->dialoguewithPlayer || high->startedDialogue));
		}

		void UpdatePendingSpawn(const NpcStates& a_states)
		{
			if (!g_pendingSpawn) {
				return;
			}

			const auto state = a_states.find(g_pendingSpawn->key);
			if (state == a_states.end()) {
				DeletePendingSpawn();
				return;
			}

			if (auto* actor = ResolvePendingSpawn()) {
				RecordCreatedPuppet(*actor, state->second.baseFormId);
				PuppetSlot slot{
					state->first,
					state->second.npcId,
					state->second.baseFormId,
					state->second.sourceFormId,
					GetActorHandle(*actor),
					false,
					{},
					0,
					{},
					{},
					{},
					0,
					state->second.cellId,
					state->second.worldspaceId
				};
				const RE::NiPoint3 position{ state->second.x, state->second.y, state->second.z };
				actor->SetPosition(position, true);
				actor->SetHeading(state->second.angleZ);
				actor->SetAlpha(1.0F);
				NeutralizePuppet(*actor);
				g_puppetSlots[slot.key] = std::move(slot);
				MarkRegistryDirty();
				F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kPuppetSpawn, actor->GetFormID(), state->second.npcId);
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

		bool LocalHostSendsState(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
		{
			if (a_state.sourceFormId != 0) {
				return g_lastHostSentSources.contains(a_state.sourceFormId);
			}
			const auto* claim = FindRuntimeClaim(a_key, a_state, a_player, a_loadedActors);
			if (!claim) {
				return false;
			}
			const auto record = g_hostNpcIdsByActorFormId.find(claim->GetFormID());
			return record != g_hostNpcIdsByActorFormId.end() && g_lastHostSentRuntimeNpcIds.contains(record->second.npcId);
		}

		void AdoptOrphanSlots(const NpcStates& a_states)
		{
			std::vector<const std::pair<const NpcKey, NpcState>*> wanted;
			for (const auto& entry : a_states) {
				if (!g_puppetSlots.contains(entry.first) && !(g_pendingSpawn && g_pendingSpawn->key == entry.first)) {
					wanted.push_back(std::addressof(entry));
				}
			}
			std::vector<NpcKey> orphans;
			for (const auto& slot : g_puppetSlots) {
				if (!a_states.contains(slot.first)) {
					orphans.push_back(slot.first);
				}
			}
			if (wanted.empty() || orphans.empty()) {
				return;
			}
			std::vector<NpcSyncPolicy::OrphanEdge> edges;
			for (std::size_t w = 0; w < wanted.size(); ++w) {
				const auto& state = wanted[w]->second;
				for (std::size_t o = 0; o < orphans.size(); ++o) {
					const auto& slot = g_puppetSlots.at(orphans[o]);
					if (slot.sourceFormId != state.sourceFormId) {
						continue;
					}
					const auto oldActor = slot.handle.get();
					if (!NpcSyncPolicy::IsOrphanAdoptable(state.isDead, slot.deathApplied, oldActor && oldActor->IsDead(true))) {
						continue;
					}
					float distance = 0.0F;
					if (!oldActor || slot.baseFormId != state.baseFormId) {
						continue;
					}
					if (state.sourceFormId == 0) {
						if (!NpcSyncPolicy::IsRuntimeAdoptScopeCompatible(slot.key.scope.cellId, wanted[w]->first.scope.cellId,
						        slot.key.scope.worldspaceId, wanted[w]->first.scope.worldspaceId)) {
							continue;
						}
						distance = (oldActor->GetPosition() - RE::NiPoint3{ state.x, state.y, state.z }).Length();
						if (!(distance <= kRuntimeAdoptDistance)) {
							continue;
						}
					}
					edges.push_back({ w, o, distance });
				}
			}
			for (const auto& edge : NpcSyncPolicy::AssignNearestOrphans(std::move(edges))) {
				const auto& key = wanted[edge.wanted]->first;
				const auto& state = wanted[edge.wanted]->second;
				auto node = g_puppetSlots.extract(orphans[edge.orphan]);
				node.key() = key;
				node.mapped().key = key;
				node.mapped().npcId = state.npcId;
				node.mapped().cellId = state.cellId;
				node.mapped().worldspaceId = state.worldspaceId;
				node.mapped().missingSince = {};
				NpcSyncPolicy::ResetAdoptedSlot(node.mapped(), state.isDead);
				g_puppetSlots.insert(std::move(node));
				MarkRegistryDirty();
			}
		}
	}

	struct LocalKillRetry
	{
		std::uint8_t attempts{};
		Clock::time_point lastAttempt{};
	};

	std::map<NpcKey, PuppetSlot> g_puppetSlots;
	std::unordered_map<std::uint32_t, LocalKillRetry> g_localKillRetries;

	RE::ActorHandle GetActorHandle(RE::Actor& a_actor)
	{
		return RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(std::addressof(a_actor));
	}

	bool IsPuppetActor(std::uint32_t a_formId)
	{
		if (auto* pending = ResolvePendingSpawn(); pending && pending->GetFormID() == a_formId) {
			return true;
		}
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
		g_runtimeClaims.clear();
		g_localKillRetries.clear();
		DeletePendingSpawn();
	}

	void LogPuppetRenderDiagnostics(const NpcStates& a_states)
	{
		static Clock::time_point lastLog{};
		const auto now = Clock::now();
		if (lastLog != Clock::time_point{} && now - lastLog < std::chrono::seconds(5)) {
			return;
		}
		lastLog = now;

		for (const auto& [key, slot] : g_puppetSlots) {
			const auto actor = slot.handle.get();
			if (!actor) {
				F4T::DebugLogging::Log("NpcSync", "puppet render diag npcId={} source={:08X} actor=<unresolved>", key.npcId, slot.sourceFormId);
				continue;
			}
			const auto position = actor->GetPosition();
			const auto* parentCell = actor->GetParentCell();
			F4T::DebugLogging::Log(
				"NpcSync",
				"puppet render diag npcId={} source={:08X} actor={:08X} base={:08X} stateActive={} position=({:.2f}, {:.2f}, {:.2f}) parentCell={} Get3D={} GetFullyLoaded3D={} GetCurrent3D={} IsVisible={} alpha={:.2f} disabled={} deleted={}",
				key.npcId,
				slot.sourceFormId,
				actor->GetFormID(),
				F4T::NpcSync::StableBaseFormId(*actor),
				a_states.contains(key),
				position.x,
				position.y,
				position.z,
				parentCell ? std::format("{:08X}", parentCell->GetFormID()) : std::string{ "unavailable" },
				actor->Get3D() != nullptr,
				actor->GetFullyLoaded3D() != nullptr,
				actor->GetCurrent3D() != nullptr,
				actor->IsVisible(),
				actor->GetAlpha(),
				actor->IsDisabled(),
				actor->IsDeleted());
		}
	}

	namespace
	{
		void LogPuppetDelete(const NpcKey& a_key, const PuppetSlot& a_slot, const NpcStates& a_states)
		{
			const NpcKey* replacement = nullptr;
			for (const auto& entry : a_states) {
				const auto& candidate = entry.first;
				const bool sameNpc = a_slot.sourceFormId != 0
				                         ? entry.second.sourceFormId == a_slot.sourceFormId && entry.second.baseFormId == a_slot.baseFormId
				                         : candidate.npcId == a_key.npcId && entry.second.baseFormId == a_slot.baseFormId;
				if (sameNpc && candidate.scope.cellId == a_key.scope.cellId && candidate != a_key) {
					replacement = std::addressof(candidate);
					break;
				}
			}
			const auto actor = a_slot.handle.get();
			F4T::DebugLogging::Log(
				"NpcSync",
				"puppet delete npcId={} actor={:08X} source={:08X} oldHost={} oldEpoch={} reason={} newHost={} newEpoch={} alpha={:.2f} visible={}",
				a_key.npcId,
				actor ? actor->GetFormID() : 0U,
				a_slot.sourceFormId,
				a_key.hostPlayerId,
				a_key.epoch,
				replacement ? "host-or-epoch-changed" : "state-gone",
				replacement ? replacement->hostPlayerId : 0U,
				replacement ? replacement->epoch : 0U,
				actor ? actor->GetAlpha() : 0.0F,
				actor && actor->IsVisible());
		}

		bool PuppetNeedsRedraw(RE::Actor& a_actor, const RE::PlayerCharacter& a_player, float a_alpha, bool a_disabled)
		{
			const auto* root = a_actor.Get3D();
			return a_alpha < 1.0F || a_disabled || (root && root->GetAppCulled() && IsSameLocation(a_actor, a_player));
		}

		void RedrawPuppetIfHidden(RE::Actor& a_actor, PuppetSlot& a_slot, const NpcState& a_state, const RE::PlayerCharacter& a_player)
		{
			const auto now = Clock::now();
			const bool skip = a_state.isDead || a_actor.IsDeleted() || g_suppressedActors.contains(a_actor.GetFormID());
			const bool disabled = !skip && a_actor.IsDisabled();
			const float alpha = (skip || !a_actor.Get3D()) ? 1.0F : a_actor.GetAlpha();
			if (skip || !PuppetNeedsRedraw(a_actor, a_player, alpha, disabled)) {
				const bool neverNeeded = a_slot.lastNeeded == Clock::time_point{};
				if (NpcSyncPolicy::IsRedrawEpisodeSettled(neverNeeded, std::chrono::duration_cast<std::chrono::milliseconds>(now - a_slot.lastNeeded))) {
					a_slot.lastRedraw = {};
					a_slot.lastNeeded = {};
					a_slot.redrawCount = 0;
				}
				return;
			}
			a_slot.lastNeeded = now;
			const bool neverRedrawn = a_slot.lastRedraw == Clock::time_point{};
			const auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - a_slot.lastRedraw);
			const auto decision = NpcSyncPolicy::DecidePuppetRedraw(neverRedrawn, sinceLast, a_slot.redrawCount);
			if (!decision.redraw) {
				if (a_slot.redrawCount == NpcSyncPolicy::kMaximumPuppetRedraws && sinceLast >= NpcSyncPolicy::kPuppetRedrawInterval) {
					++a_slot.redrawCount;
					F4T::DebugLogging::Log(
						"NpcSync",
						"puppet redraw stopped npcId={} actor={:08X} host={} epoch={} alpha={:.2f} visible={} disabled={}",
						a_slot.key.npcId,
						a_actor.GetFormID(),
						a_slot.key.hostPlayerId,
						a_slot.key.epoch,
						alpha,
						a_actor.IsVisible(),
						disabled);
				}
				return;
			}
			a_slot.lastRedraw = now;
			++a_slot.redrawCount;
			if (decision.firstRedraw) {
				F4T::DebugLogging::Log(
					"NpcSync",
					"puppet redraw npcId={} actor={:08X} host={} epoch={} alpha={:.2f} visible={} disabled={}",
					a_slot.key.npcId,
					a_actor.GetFormID(),
					a_slot.key.hostPlayerId,
					a_slot.key.epoch,
					alpha,
					a_actor.IsVisible(),
					disabled);
			}
			if (disabled) {
				a_actor.Enable(false);
			}
			auto* root = a_actor.Get3D();
			if (!root) {
				return;
			}
			if (root->GetAppCulled()) {
				root->SetAppCulled(false);
			}
			a_actor.UpdateAlpha();
			a_actor.SetAlpha(1.0F);
			a_actor.Update3DPosition(true);
			constexpr auto frustrum = std::to_underlying(RE::ACTOR_VISIBILITY_MASK::kFrustrum);
			a_actor.visFlags = static_cast<std::uint32_t>(a_actor.visFlags | frustrum);
		}
	}

	void UpdatePuppets(RE::PlayerCharacter& a_player)
	{
		LoadedActorIndex loadedActors;
		bool loadedActorsReady = false;
		NpcStates states;
		std::unordered_map<std::uint32_t, NpcKey> sources;
		const auto* localCell = a_player.GetParentCell();
		const auto localCellId = localCell ? FormatFormId(localCell->GetFormID()) : std::string{};
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		for (const auto& scoped : F4T::RemoteNpcState::GetSnapshot()) {
			const auto authority = F4T::Networking::GetNpcAuthorityForScope(scoped.scope.cellId, scoped.scope.worldspaceId);
			if (!authority || authority->playerId != scoped.hostPlayerId || authority->epoch != scoped.epoch) {
				continue;
			}
			const auto& state = scoped.state;
			const NpcKey key{scoped.scope, scoped.hostPlayerId, scoped.epoch, state.npcId};
			if (!IsStateIdentityUsable(state) || !IsStateInLocalLocation(state, a_player)) {
				g_runtimeClaims.erase(key);
				continue;
			}
			if (!loadedActorsReady) {
				loadedActors = BuildLoadedActorIndex();
				loadedActorsReady = true;
			}
			if (!NpcSyncPolicy::AcceptRemoteNpcState(scoped.hostPlayerId, localPlayerId, LocalHostSendsState(key, state, a_player, loadedActors))) {
				g_runtimeClaims.erase(key);
				continue;
			}
			if (state.isDead && !F4T::NpcSync::IsAliasedRefScanReady()) {
				continue;
			}
			if (state.isDead && !F4T::NpcSync::IsUniqueOrAliasedPlacedRef(state.sourceFormId)) {
				auto* local = FindMatchingSourceActor(key, state, a_player, loadedActors);
				if (local && !local->IsDead(true)) {
					if (IsBusyWithScriptedAction(*local)) {
						continue;
					}
					const auto killNow = Clock::now();
					auto& retry = g_localKillRetries[local->GetFormID()];
					if (NpcSyncPolicy::IsDeathRetryDue(
							std::chrono::duration_cast<std::chrono::milliseconds>(killNow - retry.lastAttempt),
							retry.attempts)) {
						if (retry.attempts < NpcSyncPolicy::kMaximumDeathBackoffStep) {
							++retry.attempts;
						}
						retry.lastAttempt = killNow;
						if (KillSyncedNpc(*local)) {
							g_localKillRetries.erase(local->GetFormID());
						}
					}
				}
			}
			if (F4T::NpcSync::IsUniqueOrAliasedPlacedRef(state.sourceFormId)) {
				auto* alias = FindMatchingSourceActor(key, state, a_player, loadedActors);
				if (alias && !alias->IsDead(true)) {
					const RE::NiPoint3 target{ state.x, state.y, state.z };
					if (!state.isDead && IsCellInStateLocation(alias->GetParentCell(), state) &&
						state.sourceFormId != F4T::NpcSync::kCodsworthPlacedRefFormId &&
						!IsBusyWithScriptedAction(*alias) &&
						(alias->GetPosition() - target).Length() > kAliasCorrectionDistance) {
						alias->SetPosition(target, true);
					}
					continue;
				}
			}

			if (HasLocalFollowerMatch(state, a_player, loadedActors)) {
				g_runtimeClaims.erase(key);
				continue;
			}

			if (state.isDead &&
				!g_puppetSlots.contains(key) &&
				!HasMatchingAliveLocalNpc(key, state, a_player, loadedActors)) {
				g_runtimeClaims.erase(key);
				continue;
			}
			if (!g_puppetSlots.contains(key) && !RE::TESForm::GetFormByID<RE::TESNPC>(state.baseFormId)) {
				g_runtimeClaims.erase(key);
				continue;
			}

			const auto previous = state.sourceFormId == 0 ? sources.end() : sources.find(state.sourceFormId);
			if (previous != sources.end()) {
				if (key.scope.cellId != localCellId || previous->second.scope.cellId == localCellId) {
					g_runtimeClaims.erase(key);
					continue;
				}
				states.erase(previous->second);
				previous->second = key;
			} else if (state.sourceFormId != 0) {
				sources.emplace(state.sourceFormId, key);
			}
			states.emplace(key, state);
		}

		DropDuplicateRuntimeStates(states, localCellId);
		std::erase_if(g_runtimeClaims, [&](const auto& a_entry) { return !states.contains(a_entry.first); });
		std::erase_if(g_localKillRetries, [&](const auto& a_entry) {
			const auto local = loadedActors.find(a_entry.first);
			return local == loadedActors.end() || local->second->IsDead(true) || IsBusyWithScriptedAction(*local->second);
		});

		AdoptOrphanSlots(states);

		SuppressDuplicateLocalNpcs(states, a_player, loadedActors);

		F4T::NpcSyncSweep::SweepTally staleTally{};
		for (auto iterator = g_puppetSlots.begin(); iterator != g_puppetSlots.end();) {
			if (!states.contains(iterator->first)) {
				if (iterator->second.missingSince == Clock::time_point{}) {
					iterator->second.missingSince = Clock::now();
					++iterator;
				} else if (Clock::now() - iterator->second.missingSince >= kMissingNpcGrace) {
					LogPuppetDelete(iterator->first, iterator->second, states);
					staleTally.Count(DeletePuppetIfLive(iterator->second.handle.get()));
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

		if (const auto flushed = g_staleSweepLog.Take(staleTally, NowMs(), 2000)) {
			F4T::DebugLogging::Log(
				"NpcSync",
				"stale puppet sweep: deleted={} alreadyDeleted={} unresolved={} remainingSlots={}",
				flushed->deleted,
				flushed->alreadyDeleted,
				flushed->unresolved,
				g_puppetSlots.size());
		}

		UpdatePendingSpawn(states);
		LogPuppetRenderDiagnostics(states);
		for (const auto& [key, state] : states) {
			auto slot = g_puppetSlots.find(key);
			if (slot == g_puppetSlots.end()) {
				if (!g_pendingSpawn && BeginSpawn(key, state, a_player, loadedActors)) {
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
			RedrawPuppetIfHidden(*actor, slot->second, state, a_player);
			const auto current = actor->GetPosition();
			const RE::NiPoint3 target{ state.x, state.y, state.z };
			const auto next = current + (target - current) * kPositionLerpAlpha;
			actor->SetPosition(next, true);
			actor->SetHeading(state.angleZ);

			const auto deathNow = Clock::now();
			if (state.isDead && !slot->second.deathApplied &&
				NpcSyncPolicy::IsDeathRetryDue(
					std::chrono::duration_cast<std::chrono::milliseconds>(deathNow - slot->second.lastDeathAttempt),
					slot->second.deathAttempts)) {
				if (slot->second.deathAttempts < NpcSyncPolicy::kMaximumDeathBackoffStep) {
					++slot->second.deathAttempts;
				}
				slot->second.lastDeathAttempt = deathNow;
				slot->second.deathApplied = ApplyPuppetDeath(*actor, a_player);
				if (slot->second.deathAttempts == 1 || slot->second.deathApplied) {
					F4T::DebugLogging::Log(
						"NpcSync",
						"puppet death npcId={} actor={:08X} attempt={} applied={}",
						state.npcId,
						actor->GetFormID(),
						slot->second.deathAttempts,
						slot->second.deathApplied);
					F4T::IntroQuestProbe::LogNpcDecision(
						slot->second.deathApplied ? "puppet-death-applied" : "puppet-death-pending",
						state.sourceFormId,
						state.baseFormId);
				}
			}
			F4T::NpcSync::ApplyAuthorityHealth(*actor, state);
		}
	}
}
