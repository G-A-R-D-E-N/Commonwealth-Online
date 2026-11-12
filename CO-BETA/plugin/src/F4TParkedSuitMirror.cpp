#include "pch.h"

#include "F4TParkedSuitMirror.h"

#include "F4TPowerArmorMirror.h"
#include "F4TPowerArmorScript.h"
#include "F4TProxySaveSafety.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace F4T::ParkedSuitMirror
{
	namespace
	{
		constexpr float kMirrorReachSquared = 2048.0F * 2048.0F;

		// Constraint: g_mirrors is touched only on the game thread; DestroyAll queues itself there when called from another thread.
		std::unordered_map<std::uint32_t, F4T::PowerArmorSync::MirrorFrame> g_mirrors;
		std::atomic<bool> g_mirrorsPresent{};

		void SyncPresence(std::uint32_t a_occupiedRef)
		{
			g_mirrorsPresent.store(std::any_of(g_mirrors.begin(), g_mirrors.end(), [&](const auto& a_entry) { return a_entry.second.refFormId != a_occupiedRef; }), std::memory_order_release);
		}

		bool InScope(const MirrorRequest& a_request, RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			if (!cell) {
				return false;
			}
			const bool inCell = a_request.cellId && cell->GetFormID() == *a_request.cellId;
			const bool inSpace = a_request.worldspaceId && cell->worldSpace && cell->worldSpace->GetFormID() == *a_request.worldspaceId;
			if (!inCell && !inSpace) {
				return false;
			}
			const auto position = a_player.GetPosition();
			const auto dx = a_request.x - position.x;
			const auto dy = a_request.y - position.y;
			const auto dz = a_request.z - position.z;
			return dx * dx + dy * dy + dz * dz <= kMirrorReachSquared;
		}

		bool Stale(const F4T::PowerArmorSync::MirrorFrame& a_mirror, const MirrorRequest& a_request)
		{
			return a_mirror.reference && (a_mirror.baseFormId != a_request.frameBaseFormId || a_mirror.cellId != a_request.cellId.value_or(0) || a_mirror.worldspaceId != a_request.worldspaceId.value_or(0) || a_mirror.x != a_request.x || a_mirror.y != a_request.y || a_mirror.z != a_request.z || a_mirror.angleZ != a_request.angleZ || (a_mirror.piecesApplied && !(a_mirror.appliedPieces == a_request.powerArmor)));
		}

		void DestroyAllOnGameThread()
		{
			if (g_mirrors.empty()) {
				return;
			}
			std::optional<F4T::PowerArmorSync::OccupiedFrame> occupied;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				occupied = F4T::PowerArmorSync::CaptureOccupiedFrame(*player);
			}
			const auto occupiedRef = occupied ? occupied->refFormId : 0U;
			for (auto mirror = g_mirrors.begin(); mirror != g_mirrors.end();) {
				if (occupiedRef != 0 && mirror->second.refFormId == occupiedRef) {
					++mirror;
					continue;
				}
				F4T::PowerArmorSync::DestroyMirrorFrame(mirror->second);
				mirror = g_mirrors.erase(mirror);
			}
			SyncPresence(occupiedRef);
		}
	}

	// TODO-verify: mirror frames are placed and destroyed from the game-thread publish tick; source:CodeNotes-GARDEN power-armor-sync.
	void Reconcile(RE::PlayerCharacter& a_player, const std::vector<MirrorRequest>& a_requests)
	{
		if (F4T::ProxySaveSafety::IsProxyTickBlocked()) {
			return;
		}
		const auto occupied = F4T::PowerArmorSync::CaptureOccupiedFrame(a_player);
		const auto occupiedRef = occupied ? occupied->refFormId : 0U;
		for (auto mirror = g_mirrors.begin(); mirror != g_mirrors.end();) {
			const auto request = std::find_if(a_requests.begin(), a_requests.end(), [&](const MirrorRequest& a_candidate) { return a_candidate.suitId == mirror->first; });
			const bool keep = request != a_requests.end() && InScope(*request, a_player) && !Stale(mirror->second, *request);
			if (keep || (occupiedRef != 0 && mirror->second.refFormId == occupiedRef)) {
				++mirror;
				continue;
			}
			F4T::PowerArmorSync::DestroyMirrorFrame(mirror->second);
			mirror = g_mirrors.erase(mirror);
		}
		for (const auto& request : a_requests) {
			if (!InScope(request, a_player)) {
				continue;
			}
			auto& mirror = g_mirrors[request.suitId];
			if (F4T::PowerArmorSync::EnsureMirrorFrame(mirror, request.frameBaseFormId, request.cellId.value_or(0), request.worldspaceId.value_or(0), request.x, request.y, request.z, request.angleZ) != F4T::PowerArmorSync::MirrorStatus::kReady) {
				continue;
			}
			if (!F4T::PowerArmorSync::ApplyMirrorPieces(mirror, request.powerArmor)) {
				continue;
			}
			const bool locked = F4T::PowerArmorScript::IsFrameLockedByUs(mirror.refFormId);
			if (request.mine && locked) {
				(void)F4T::PowerArmorScript::SetFrameLocked(*mirror.reference, false);
				(void)F4T::PowerArmorScript::SetFrameOwner(*mirror.reference, a_player.GetNPC());
			} else if (!request.mine && !locked) {
				(void)F4T::PowerArmorScript::SetFrameLocked(*mirror.reference, true);
			}
		}
		SyncPresence(occupiedRef);
	}

	std::optional<std::uint32_t> SuitForFrame(std::uint32_t a_refFormId)
	{
		for (const auto& entry : g_mirrors) {
			if (entry.second.refFormId == a_refFormId) {
				return entry.first;
			}
		}
		return std::nullopt;
	}

	void Graduate(std::uint32_t a_suitId)
	{
		g_mirrors.erase(a_suitId);
		SyncPresence(0);
	}

	bool DestroyAll()
	{
		if (F4T::ProxySaveSafety::IsOnRecordedGameThread()) {
			DestroyAllOnGameThread();
			return false;
		}
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface || !g_mirrorsPresent.load(std::memory_order_acquire)) {
			return false;
		}
		taskInterface->AddTask([]() { DestroyAllOnGameThread(); });
		return true;
	}
}
