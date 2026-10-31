#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr float kPlaceAtMeProxyMaxSettleSeconds = 12.0F;

	enum class ProxyLookupResult
	{
		kReferenceEditorId,
		kBaseActorEditorId,
		kFallbackFormId
	};
	bool g_proxyResolvedLogged = false;
	bool g_runtimeProxyEnableSkippedLogged = false;
	bool g_stage37CreateReferenceSkippedLogged = false;

	bool HasProxyBaseActorEditorId(const RE::Actor& a_actor)
	{
		const auto* baseActor = a_actor.GetNPC();
		return baseActor && IsCustomPlayerProxyBase(*baseActor);
	}

	RE::Actor* TryGetCachedProxy(const RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		if (!g_proxyHandle) {
			return nullptr;
		}

		const auto proxyRef = g_proxyHandle.get();
		auto* proxy = proxyRef ? proxyRef->As<RE::Actor>() : nullptr;
		if (proxy && IsValidProxyActor(*proxy, a_currentCell, a_player)) {
			return proxy;
		}

		g_proxyHandle.reset();
		g_proxyResolvedLogged = false;
		return nullptr;
	}

	RE::Actor* TryLookupProxyByEditorId(
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		ProxyLookupResult& a_lookupResult)
	{
		auto* proxy = RE::TESForm::GetFormByEditorID<RE::Actor>(RE::BSFixedString(kProxyEditorId));
		if (!proxy) {
			auto* proxyRef = RE::TESForm::GetFormByEditorID<RE::TESObjectREFR>(RE::BSFixedString(kProxyEditorId));
			proxy = proxyRef ? proxyRef->As<RE::Actor>() : nullptr;
		}

		if (!proxy || !IsValidProxyActor(*proxy, a_currentCell, a_player)) {
			return nullptr;
		}

		a_lookupResult = ProxyLookupResult::kReferenceEditorId;
		return proxy;
	}

	RE::Actor* TryFindProxyInCurrentCell(
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		ProxyLookupResult& a_lookupResult)
	{
		RE::Actor* proxy = nullptr;

		a_currentCell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			if (IsRuntimeProxyRef(*a_ref)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			auto* actor = a_ref->As<RE::Actor>();
			if (!actor || !IsValidProxyActor(*actor, a_currentCell, a_player)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			if (HasEditorId(*a_ref, kProxyEditorId)) {
				proxy = actor;
				a_lookupResult = ProxyLookupResult::kReferenceEditorId;
				return RE::BSContainer::ForEachResult::kStop;
			}

			if (HasProxyBaseActorEditorId(*actor)) {
				LogThrottledInfo("proxy_base_actor_skipped_for_placed_fallback", std::format(
					"Skipping proxy-base actor {:08X} during placed fallback scan; Stage 3 only accepts the placed reference {} or its ESP-local form ID as fallback.",
					actor->GetFormID(),
					kProxyEditorId));
			}

			return RE::BSContainer::ForEachResult::kContinue;
		});

		return proxy;
	}

	RE::Actor* TryLookupProxyByFallbackFormId(
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		ProxyLookupResult& a_lookupResult)
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return nullptr;
		}

		auto* proxy = dataHandler->LookupForm<RE::Actor>(kProxyFallbackLocalFormId, kTestPluginName);
		if (!proxy || !IsValidProxyActor(*proxy, a_currentCell, a_player)) {
			return nullptr;
		}

		a_lookupResult = ProxyLookupResult::kFallbackFormId;
		return proxy;
	}

	const char* GetLookupResultDescription(ProxyLookupResult a_lookupResult)
	{
		switch (a_lookupResult) {
		case ProxyLookupResult::kReferenceEditorId:
			return "placed reference editor ID";
		case ProxyLookupResult::kBaseActorEditorId:
			return "base actor editor ID";
		case ProxyLookupResult::kFallbackFormId:
			return "fallback ESP-local form ID";
		default:
			return "unknown lookup path";
		}
	}
}

namespace F4T::ProxyActorController::detail
{
	bool MaintainSlotPlaceAtMeSpawn(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		const RemotePlayer* a_remotePlayer)
	{
		if (a_slot.spawnFailed) {
			return false;
		}

		if (a_slot.proxyHandle) {
			return false;
		}

		if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kPromoted) {
			a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
			a_slot.spawnSucceeded = false;
			a_slot.spawnAttemptInProgress = false;
			a_slot.spawnAttempted = false;
		}

		if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kNotStarted) {
			RemotePlayer fallbackRemote{};
			fallbackRemote.playerId = a_slot.remotePlayerId;
			const auto& remotePlayer = a_remotePlayer ? *a_remotePlayer : fallbackRemote;
			if (!TryBeginSlotPlaceAtMeSpawn(a_slot, a_currentCell, a_player, remotePlayer)) {
				if (!g_runtimeProxySpawnAttemptPlayerId) {
					MarkSlotSpawnFailed(a_slot);
				}
				return false;
			}
			return true;
		}

		if (g_runtimeProxySpawnAttemptPlayerId && *g_runtimeProxySpawnAttemptPlayerId != a_slot.remotePlayerId) {
			return false;
		}

		if (IsPlaceAtMeBackedProxyAttempting(a_slot.spawnState)) {
			std::size_t newCandidateCount = 0;
			if (auto* actor = TryIsolateSlotPlaceAtMeCandidate(a_slot, a_currentCell, a_player, newCandidateCount)) {
				StartSlotPlaceAtMeSettle(a_slot, *actor, newCandidateCount);
				return true;
			}

			const auto elapsedSeconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - a_slot.candidateSearchStartTime).count();
			if (elapsedSeconds < kPlaceAtMeProxyCandidateSearchSeconds) {
				LogThrottledInfo(
					"stage4_placeatme_proxy_no_new_candidate_yet_" + std::to_string(a_slot.remotePlayerId),
					std::format(
						"Stage 4 PlaceAtMe-backed proxy isolation: no new actor found yet for remote player {}, base={:08X}; continuing search.",
						a_slot.remotePlayerId,
						a_slot.spawnBaseFormId),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
				return true;
			}

			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE timeout player={} base={:08X} scans={} suppressed={} candidate ids recorded={} elapsedSeconds={:.2f} thread={}",
				a_slot.remotePlayerId,
				a_slot.spawnBaseFormId,
				g_slotWindowScans,
				g_slotWindowSuppressedLogs,
				g_slotWindowLoggedCandidates.size(),
				elapsedSeconds,
				REX::W32::GetCurrentThreadId());
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 4 PlaceAtMe-backed proxy spawn failed for remote player {}: no new actor could be isolated for base={:08X}.",
				a_slot.remotePlayerId,
				a_slot.spawnBaseFormId));
			if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting &&
				ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
				if (TryBeginSlotCodsworthFallback(a_slot, a_currentCell, a_player)) {
					return true;
				}
			}

			MarkSlotSpawnFailed(a_slot);
			return false;
		}

		if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling) {
			auto* actor = a_slot.pendingSpawnHandle.get().get();
			if (!actor || !IsValidProxyActor(*actor, a_currentCell, a_player)) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Stage 4 PlaceAtMe-backed runtime proxy settle failed for remote player {}: pending actor handle no longer resolves or failed validation.",
					a_slot.remotePlayerId));
				MarkSlotSpawnFailed(a_slot);
				return false;
			}

			const auto elapsedSeconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - a_slot.settleStartTime).count();
			const auto remainingSeconds = std::max(0.0F, kPlaceAtMeProxySettleSeconds - elapsedSeconds);
			LogThrottledInfo(
				"stage4_placeatme_proxy_movement_deferred_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Stage 4 runtime proxy for remote player {} is settling; movement is deferred. settleRemaining={:.1f}s.",
					a_slot.remotePlayerId,
					remainingSeconds),
				kRuntimeProxyVisibilityDiagnosticLogInterval);
			if (ShouldLog("stage4_placeatme_proxy_settle_diagnostic_" + std::to_string(a_slot.remotePlayerId), kRuntimeProxyVisibilityDiagnosticLogInterval)) {
				LogPlaceAtMeBackedProxySettleDiagnostic(*actor, a_player, elapsedSeconds, remainingSeconds);
			}

			if (elapsedSeconds < kPlaceAtMeProxySettleSeconds) {
				return true;
			}

			if (!Proxy3DReadyForPresentation(*actor)) {
				if (elapsedSeconds < kPlaceAtMeProxyMaxSettleSeconds) {
					LogThrottledInfo(
						"stage4_placeatme_proxy_waiting_for_loaded_3d_" + std::to_string(a_slot.remotePlayerId),
						std::format(
							"Stage 4 runtime proxy for remote player {} is waiting for loaded 3D before promotion: Get3D={}, GetFullyLoaded3D={}, IsVisible={}, settleElapsed={:.1f}s.",
							a_slot.remotePlayerId,
							actor->Get3D() != nullptr,
							actor->GetFullyLoaded3D() != nullptr,
							actor->IsVisible(),
							elapsedSeconds),
						kRuntimeProxyVisibilityDiagnosticLogInterval);
					return true;
				}

				LogWarningWithLocalPlayerPrefix(std::format(
					"Stage 4 runtime proxy for remote player {} exceeded max settle wait ({:.1f}s) without loaded 3D; promoting anyway.",
					a_slot.remotePlayerId,
					kPlaceAtMeProxyMaxSettleSeconds));
			}

			PromoteSlotPlaceAtMeProxy(a_slot, *actor);
			return false;
		}

		return false;
	}

	RE::Actor* ResolveProxy(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		if (auto* cachedProxy = TryGetCachedProxy(a_currentCell, a_player)) {
			return cachedProxy;
		}

		auto lookupResult = ProxyLookupResult::kReferenceEditorId;
		auto* proxy = TryLookupProxyByEditorId(a_currentCell, a_player, lookupResult);
		if (!proxy) {
			proxy = TryFindProxyInCurrentCell(a_currentCell, a_player, lookupResult);
		}
		if (!proxy) {
			proxy = TryLookupProxyByFallbackFormId(a_currentCell, a_player, lookupResult);
		}

		if (!proxy) {
			LogThrottledWarning(
				"proxy_missing",
				"Proxy actor controller could not find F4TProxyRemotePlayer01REF in F4TTestCell01.");
			return nullptr;
		}

		g_proxyHandle = proxy->GetHandle();
		if (!g_proxyResolvedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Proxy actor controller resolved {} by {} as form {:08X}.",
				kProxyEditorId,
				GetLookupResultDescription(lookupResult),
				proxy->GetFormID()));
			g_proxyResolvedLogged = true;
		}

		return proxy;
	}
}
