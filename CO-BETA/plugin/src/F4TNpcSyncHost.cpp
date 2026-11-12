#include "pch.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4TNpcSync.h"

#include "F4TNpcSyncHealth.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncLootInternal.h"
#include "F4TNpcSyncPolicy.h"
#include "F4TProxyActorController.h"
#include "F4TRemoteNpcState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace F4T::NpcSync::Detail
{
	namespace
	{
		constexpr std::size_t kMaxSyncedNpcs = 16;
		constexpr auto kHostSendInterval = 100ms;
		constexpr float kMaximumHostDistance = 8000.0F;
		constexpr auto kMissingHostNpcIdGrace = 5s;
		constexpr auto kFreshDeathWindow = NpcSyncPolicy::kFreshDeathWindow;
		std::unordered_map<std::uint32_t, Clock::time_point> g_hostDeadSince;

		struct HostCandidate
		{
			RE::Actor* actor{};
			float distance{};
			int rank{};
		};

		float Distance(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right)
		{
			const auto delta = a_left - a_right;
			return delta.Length();
		}

		std::uint32_t AllocateNpcId()
		{
			const auto npcId = g_nextNpcId++;
			if (g_nextNpcId == 0) {
				g_nextNpcId = 1;
			}
			return npcId;
		}

		HostNpcIdRecord& HostNpcIdRecordFor(std::uint32_t a_actorFormId, std::uint32_t a_baseFormId)
		{
			auto [entry, inserted] = g_hostNpcIdsByActorFormId.try_emplace(a_actorFormId);
			if (inserted) {
				MarkRegistryDirty();
				entry->second = HostNpcIdRecord{ AllocateNpcId(), a_baseFormId, {} };
			}
			return entry->second;
		}

		int HostRank(RE::Actor& a_actor, Clock::time_point a_now)
		{
			const auto actorFormId = a_actor.GetFormID();
			if (!a_actor.IsDead(true)) {
				g_hostDeadSince[actorFormId] = Clock::time_point{};
				return NpcSyncPolicy::HostRank(false, F4T::RemoteNpcState::IsRuntimeFormId(actorFormId), {});
			}
			auto& deadSince = g_hostDeadSince.try_emplace(actorFormId, a_now - kFreshDeathWindow).first->second;
			if (deadSince == Clock::time_point{}) {
				deadSince = a_now;
			}
			return NpcSyncPolicy::HostRank(true, false, std::chrono::duration_cast<std::chrono::milliseconds>(a_now - deadSince));
		}

		void SyncHostNpcIds(const std::unordered_map<std::uint32_t, std::uint32_t>& a_loadedRuntimeBases)
		{
			const auto now = Clock::now();
			for (auto entry = g_hostNpcIdsByActorFormId.begin(); entry != g_hostNpcIdsByActorFormId.end();) {
				const auto actorFormId = entry->first;
				auto& record = entry->second;
				if (!F4T::RemoteNpcState::IsRuntimeFormId(actorFormId)) {
					++entry;
					continue;
				}
				std::optional<std::uint32_t> loadedBaseFormId;
				if (const auto loaded = a_loadedRuntimeBases.find(actorFormId); loaded != a_loadedRuntimeBases.end()) {
					loadedBaseFormId = loaded->second;
				}
				const auto missingSince = record.missingSince == Clock::time_point{} ? std::nullopt : std::optional{ record.missingSince };
				const auto resolution = NpcSyncPolicy::ResolveHostNpcIdRecord(record.baseFormId, missingSince, loadedBaseFormId, now, kMissingHostNpcIdGrace);
				if (resolution.step == NpcSyncPolicy::HostNpcIdStep::kForget) {
					Loot::Detail::ForgetLootContainer(actorFormId);
					entry = g_hostNpcIdsByActorFormId.erase(entry);
					continue;
				}
				if (resolution.step == NpcSyncPolicy::HostNpcIdStep::kReissue) {
					Loot::Detail::ForgetLootContainer(actorFormId);
					record = HostNpcIdRecord{ AllocateNpcId(), *loadedBaseFormId, {} };
					MarkRegistryDirty();
				} else {
					const auto resolvedMissing = resolution.missingSince.value_or(Clock::time_point{});
					if (record.missingSince != resolvedMissing) {
						record.missingSince = resolvedMissing;
						MarkRegistryDirty();
					}
				}
				++entry;
			}
		}

		std::vector<NpcState> CaptureHostSnapshot(
			RE::PlayerCharacter& a_player,
			std::string_view a_authorityCellId,
			std::string_view a_authorityWorldspaceId)
		{
			std::vector<HostCandidate> candidates;
			std::unordered_map<std::uint32_t, std::uint32_t> loadedRuntimeBases;
			std::unordered_set<std::uint32_t> loadedFormIds;
			const auto playerPosition = a_player.GetPosition();
			const auto now = Clock::now();
			ForEachLoadedActor([&](RE::Actor& a_actor) {
				loadedFormIds.insert(a_actor.GetFormID());
				if (F4T::RemoteNpcState::IsRuntimeFormId(a_actor.GetFormID())) {
					loadedRuntimeBases.try_emplace(a_actor.GetFormID(), F4T::NpcSync::StableBaseFormId(a_actor));
				}
				if (std::addressof(a_actor) == std::addressof(a_player) ||
					IsPuppetActor(a_actor.GetFormID()) ||
					g_suppressedActors.contains(a_actor.GetFormID()) ||
					IsRemotePlayerProxyActor(a_actor) ||
					!IsSameLocation(a_actor, a_player)) {
					return;
				}

				const auto distance = Distance(a_actor.GetPosition(), playerPosition);
				if (!std::isfinite(distance) || distance > kMaximumHostDistance) {
					return;
				}

				const auto baseFormId = F4T::NpcSync::StableBaseFormId(a_actor);
				if (baseFormId == 0) {
					return;
				}
				candidates.push_back({ std::addressof(a_actor), distance, HostRank(a_actor, now) });
			});

			std::erase_if(g_hostDeadSince, [&](const auto& a_entry) { return !loadedFormIds.contains(a_entry.first); });
			SyncHostNpcIds(loadedRuntimeBases);
			std::ranges::sort(candidates, [](const HostCandidate& a_left, const HostCandidate& a_right) {
				return a_left.rank != a_right.rank ? a_left.rank < a_right.rank : a_left.distance < a_right.distance;
			});
			if (candidates.size() > kMaxSyncedNpcs) {
				candidates.resize(kMaxSyncedNpcs);
			}

			std::vector<NpcState> states;
			states.reserve(candidates.size());
			for (const auto& candidate : candidates) {
				auto& actor = *candidate.actor;
				const auto* cell = actor.GetParentCell();
				const auto baseFormId = F4T::NpcSync::StableBaseFormId(actor);
				if (!cell || baseFormId == 0) {
					continue;
				}
				auto& record = HostNpcIdRecordFor(actor.GetFormID(), baseFormId);

				const auto position = actor.GetPosition();
				RE::NiPoint3 velocity{};
				actor.GetLinearVelocity(velocity);
				const auto speed = velocity.Length();

				states.push_back(NpcState{
					record.npcId,
					baseFormId,
					F4T::NpcSync::WireSourceFormId(actor.GetFormID()),
					position.x,
					position.y,
					position.z,
					actor.GetHeading(),
					std::string{ a_authorityCellId },
					std::string{ a_authorityWorldspaceId },
					// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 C-5, w3-c5-isdead-kdying-{og,ng,ae} VERIFIED: IsDead(false) is also true at kEssentialDown, IsDead(true) is not)
					actor.IsDead(true),
					std::isfinite(speed) && speed > 5.0F,
					false,
					false,
					std::isfinite(speed) ? speed : 0.0F
				});
				F4T::NpcSync::CaptureHealth(actor, states.back());
			}
			return states;
		}
	}

	std::unordered_map<std::uint32_t, HostNpcIdRecord> g_hostNpcIdsByActorFormId;
	std::uint32_t g_nextNpcId{ 1 };
	std::unordered_set<std::uint32_t> g_lastHostSentSources;
	std::unordered_set<std::uint32_t> g_lastHostSentRuntimeNpcIds;

	Clock::time_point g_lastHostSend{};
	Clock::time_point g_lastSummaryLog{};

	void ClearHostNpcIds()
	{
		g_lastHostSentSources.clear();
		g_lastHostSentRuntimeNpcIds.clear();
		g_hostDeadSince.clear();
		if (!g_hostNpcIdsByActorFormId.empty()) {
			g_hostNpcIdsByActorFormId.clear();
			MarkRegistryDirty();
		}
	}

	bool IsSameLocation(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player)
	{
		const auto* actorCell = a_actor.GetParentCell();
		const auto* playerCell = a_player.GetParentCell();
		if (!actorCell || !playerCell) {
			return false;
		}
		if (actorCell == playerCell || actorCell->GetFormID() == playerCell->GetFormID()) {
			return true;
		}
		return actorCell->IsExterior() && playerCell->IsExterior() &&
		       actorCell->worldSpace && actorCell->worldSpace == playerCell->worldSpace;
	}

	std::string FormatFormId(std::uint32_t a_formId)
	{
		return std::format("{:08X}", a_formId);
	}

	bool IsRemotePlayerProxyActor(const RE::Actor& a_actor)
	{
		return F4T::ProxyActorController::IsPlayerProxyActor(a_actor) ||
		       F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(a_actor.GetFormID()).has_value();
	}

	void UpdateHost(
		RE::PlayerCharacter& a_player,
		std::uint64_t a_authorityEpoch,
		std::string_view a_authorityCellId,
		std::string_view a_authorityWorldspaceId)
	{
		const auto now = Clock::now();
		if (g_lastHostSend != Clock::time_point{} && now - g_lastHostSend < kHostSendInterval) {
			return;
		}

		auto snapshot = CaptureHostSnapshot(a_player, a_authorityCellId, a_authorityWorldspaceId);
		if (F4T::Networking::SendNpcStatePacket(
				snapshot,
				a_authorityEpoch,
				a_authorityCellId,
				a_authorityWorldspaceId)) {
			g_lastHostSend = now;
			g_lastHostSentSources.clear();
			g_lastHostSentRuntimeNpcIds.clear();
			for (const auto& state : snapshot) {
				if (state.sourceFormId != 0) {
					g_lastHostSentSources.insert(state.sourceFormId);
				} else {
					g_lastHostSentRuntimeNpcIds.insert(state.npcId);
				}
			}
			if (g_lastSummaryLog == Clock::time_point{} || now - g_lastSummaryLog >= 5s) {
				F4T::DebugLogging::Log("NpcSync", "host snapshot sent nearbyNpcs={}", snapshot.size());
				g_lastSummaryLog = now;
			}
		}
	}
}
