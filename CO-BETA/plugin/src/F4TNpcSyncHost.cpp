#include "pch.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4TNpcSync.h"

#include "F4TNpcSyncHealth.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemoteNpcState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::NpcSync::Detail
{
	namespace
	{
		constexpr std::size_t kMaxSyncedNpcs = 16;
		constexpr auto kHostSendInterval = 100ms;
		constexpr float kMaximumHostDistance = 8000.0F;

		struct HostCandidate
		{
			RE::Actor* actor{};
			float distance{};
		};

		float Distance(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right)
		{
			const auto delta = a_left - a_right;
			return delta.Length();
		}

		std::vector<NpcState> CaptureHostSnapshot(
			RE::PlayerCharacter& a_player,
			std::string_view a_authorityCellId,
			std::string_view a_authorityWorldspaceId)
		{
			std::vector<HostCandidate> candidates;
			const auto playerPosition = a_player.GetPosition();
			ForEachLoadedActor([&](RE::Actor& a_actor) {
				if (std::addressof(a_actor) == std::addressof(a_player) ||
					IsPuppetActor(a_actor.GetFormID()) ||
					IsRemotePlayerProxyActor(a_actor) ||
					!IsSameLocation(a_actor, a_player)) {
					return;
				}

				const auto distance = Distance(a_actor.GetPosition(), playerPosition);
				if (!std::isfinite(distance) || distance > kMaximumHostDistance) {
					return;
				}

				if (F4T::RemoteNpcState::IsRuntimeFormId(a_actor.GetFormID()) ||
					F4T::NpcSync::StableBaseFormId(a_actor) == 0) {
					return;
				}
				candidates.push_back({ std::addressof(a_actor), distance });
			});

			std::ranges::sort(candidates, {}, &HostCandidate::distance);
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
				auto [idEntry, inserted] = g_hostNpcIdsByActorFormId.try_emplace(actor.GetFormID(), 0);
				if (inserted) {
					MarkRegistryDirty();
					idEntry->second = g_nextNpcId++;
					if (g_nextNpcId == 0) {
						g_nextNpcId = 1;
					}
				}

				const auto position = actor.GetPosition();
				RE::NiPoint3 velocity{};
				actor.GetLinearVelocity(velocity);
				const auto speed = velocity.Length();

				states.push_back(NpcState{
					idEntry->second,
					baseFormId,
					actor.GetFormID(),
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

	std::unordered_map<std::uint32_t, std::uint32_t> g_hostNpcIdsByActorFormId;
	std::uint32_t g_nextNpcId{ 1 };

	Clock::time_point g_lastHostSend{};
	Clock::time_point g_lastSummaryLog{};

	void ClearHostNpcIds()
	{
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
			if (g_lastSummaryLog == Clock::time_point{} || now - g_lastSummaryLog >= 5s) {
				F4T::DebugLogging::Log("NpcSync", "host snapshot sent nearbyNpcs={}", snapshot.size());
				g_lastSummaryLog = now;
			}
		}
	}
}
