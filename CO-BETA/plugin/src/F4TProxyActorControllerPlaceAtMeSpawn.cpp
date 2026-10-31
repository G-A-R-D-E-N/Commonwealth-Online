// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TConsoleGuard.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr bool kDebugPlaceAtMeSpawnUseCustomProxyBase = true;
	constexpr bool kDebugPlaceAtMeSpawnFallbackToCodsworth = true;
	RE::ObjectRefHandle g_placeAtMeBackedProxyPendingHandle;
	PlaceAtMeBackedProxySpawnState g_placeAtMeBackedProxySpawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
	RuntimeActorBaseSource g_placeAtMeBackedProxySource = RuntimeActorBaseSource::kCustomProxy;
	std::uint32_t g_placeAtMeBackedProxyBaseFormId = 0;
	std::unordered_set<std::uint32_t> g_placeAtMeBackedProxyPreexistingCandidateRefs;
	std::size_t g_placeAtMeWindowScans = 0;
	std::size_t g_placeAtMeWindowCandidateLogs = 0;
	std::size_t g_placeAtMeWindowSuppressedLogs = 0;
	std::size_t g_placeAtMeSkippedNonCreatedCandidates = 0;
	auto g_placeAtMeBackedProxyCandidateSearchStartTime = std::chrono::steady_clock::time_point{};
	auto g_placeAtMeBackedProxySettleStartTime = std::chrono::steady_clock::time_point{};
	bool g_stage37OlderDiagnosticsDisabledLogged = false;
	bool g_stage37ResultLogged = false;

	std::string FormatPlaceAtMeBackedProxyCandidateSnapshot()
	{
		if (g_placeAtMeBackedProxyPreexistingCandidateRefs.empty()) {
			return "[]";
		}

		std::vector<std::uint32_t> sortedCandidateRefs(
			g_placeAtMeBackedProxyPreexistingCandidateRefs.begin(),
			g_placeAtMeBackedProxyPreexistingCandidateRefs.end());
		std::ranges::sort(sortedCandidateRefs);

		std::string candidates = "[";
		for (std::size_t index = 0; index < sortedCandidateRefs.size(); ++index) {
			if (index > 0) {
				candidates += ", ";
			}
			candidates += std::format("{:08X}", sortedCandidateRefs[index]);
		}
		candidates += "]";
		return candidates;
	}

	bool IsPlaceAtMeBackedProxyCandidateActor(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player)
	{
		if (std::addressof(a_actor) == std::addressof(a_player)) {
			return false;
		}

		const auto* baseActor = a_actor.GetNPC();
		if (!baseActor || baseActor->GetFormID() != g_placeAtMeBackedProxyBaseFormId) {
			return false;
		}

		if (!a_actor.IsCreated()) {
			++g_placeAtMeSkippedNonCreatedCandidates;
			if (F4T::ProxySaveSafety::ShouldLogPeriodic(g_placeAtMeSkippedNonCreatedCandidates)) {
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"[RT-VERIFY 408-C] PlaceAtMe window candidate skipped: actor={:08X} base={:08X} isCreated=false occurrence={}",
					a_actor.GetFormID(),
					baseActor->GetFormID(),
					g_placeAtMeSkippedNonCreatedCandidates);
			}
			return false;
		}

		return true;
	}

	void SnapshotPreexistingPlaceAtMeBackedProxyCandidates(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		g_placeAtMeBackedProxyPreexistingCandidateRefs.clear();
		const auto playerPosition = a_player.GetPosition();
		a_currentCell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			auto* actor = a_ref->As<RE::Actor>();
			if (!actor || !IsPlaceAtMeBackedProxyCandidateActor(*actor, a_player)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto position = actor->GetPosition();
			const auto* parentCell = actor->GetParentCell();
			const auto distanceFromLocalPlayer = GetDistance(position, playerPosition);
			g_placeAtMeBackedProxyPreexistingCandidateRefs.insert(actor->GetFormID());
			LogInfoWithLocalPlayerPrefix(std::format(
				"Stage 3.7 pre-PlaceAtMe proxy candidate: actor={:08X}, base={:08X}, position={}, parentCell={}, distanceFromLocalPlayer={:.2f}, Get3D={}, GetFullyLoaded3D={}, GetCurrent3D={}, IsVisible={}.",
				actor->GetFormID(),
				actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
				FormatPosition(position),
				parentCell ? std::format("{:08X}", parentCell->GetFormID()) : "unavailable",
				distanceFromLocalPlayer,
				actor->Get3D() != nullptr,
				actor->GetFullyLoaded3D() != nullptr,
				actor->GetCurrent3D() != nullptr,
				actor->IsVisible()));

			return RE::BSContainer::ForEachResult::kContinue;
		});

		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 3.7 pre-PlaceAtMe proxy candidate snapshot: base={:08X}, count={}, candidates={}.",
			g_placeAtMeBackedProxyBaseFormId,
			g_placeAtMeBackedProxyPreexistingCandidateRefs.size(),
			FormatPlaceAtMeBackedProxyCandidateSnapshot()));
	}

	void BeginPlaceAtMeBackedProxySpawnAttempt(
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		std::uint32_t a_baseFormId,
		RuntimeActorBaseSource a_source)
	{
		g_placeAtMeBackedProxyBaseFormId = a_baseFormId;
		g_placeAtMeBackedProxySource = a_source;
		g_runtimeProxyActorBaseSource = a_source;
		g_runtimeProxySpawnAttempted = true;
		g_runtimeProxySpawnAttemptInProgress = true;
		g_runtimeProxySpawnFailed = false;
		g_placeAtMeBackedProxySpawnState =
			a_source == RuntimeActorBaseSource::kCustomProxy ?
				PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting :
				PlaceAtMeBackedProxySpawnState::kCodsworthFallbackAttempting;

		SnapshotPreexistingPlaceAtMeBackedProxyCandidates(a_currentCell, a_player);
		g_placeAtMeWindowScans = 0;
		g_placeAtMeWindowCandidateLogs = 0;
		g_placeAtMeWindowSuppressedLogs = 0;
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"[RT-VERIFY 408-C] PlaceAtMe window OPEN base={:08X} source={} preexisting={} thread={}",
			a_baseFormId,
			GetPlaceAtMeBackedProxySourceLabel(a_source),
			g_placeAtMeBackedProxyPreexistingCandidateRefs.size(),
			REX::W32::GetCurrentThreadId());
		const auto command = std::format("player.placeatme {:08X} 1", a_baseFormId);
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 3.7 executing PlaceAtMe-backed proxy spawn command once: {}.",
			command));
		// TODO-verify(#407): the placeatme ref is visible to the window scan as soon as ExecuteCommand returns source:rt-briefs/thread-408.md (comment 20722)
		F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-C] PlaceAtMe command returned base={:08X} scans={} thread={}",
			a_baseFormId,
			g_placeAtMeWindowScans,
			REX::W32::GetCurrentThreadId());
		g_placeAtMeBackedProxyCandidateSearchStartTime = std::chrono::steady_clock::now();
	}

	bool TryBeginCodsworthPlaceAtMeBackedProxyFallback(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		if (!ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
			return false;
		}

		auto* codsworthBase = ResolveCodsworthBaseByAbsoluteFormId();
		if (!codsworthBase) {
			LogWarningWithLocalPlayerPrefix(
				"Stage 3.7 Codsworth fallback unavailable: diagnostic base 000179FF did not resolve as TESNPC.");
			return false;
		}

		BeginPlaceAtMeBackedProxySpawnAttempt(
			a_currentCell,
			a_player,
			codsworthBase->GetFormID(),
			RuntimeActorBaseSource::kVanillaDiagnosticNpc);
		return true;
	}

	void MarkPlaceAtMeBackedProxySpawnFailed()
	{
		g_placeAtMeBackedProxySpawnState = PlaceAtMeBackedProxySpawnState::kFailed;
		g_runtimeProxySpawnAttemptInProgress = false;
		g_runtimeProxySpawnFailed = true;
	}

	bool TryBeginPlaceAtMeBackedProxySpawn(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		if (ShouldUseCustomProxyBaseForPlaceAtMeBackedSpawn()) {
			if (auto* customProxyBase = ResolveProxyActorBaseByPluginLocalFormId()) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 3.7 PlaceAtMe-backed proxy spawn base resolved: custom COPlayerProxy, formId={:08X}.",
					customProxyBase->GetFormID()));
				BeginPlaceAtMeBackedProxySpawnAttempt(
					a_currentCell,
					a_player,
					customProxyBase->GetFormID(),
					RuntimeActorBaseSource::kCustomProxy);
				return true;
			}
		}

		if (ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
			LogWarningWithLocalPlayerPrefix(
				"Stage 3.7 custom proxy base unavailable; falling back to Codsworth diagnostic base 000179FF.");
			return TryBeginCodsworthPlaceAtMeBackedProxyFallback(a_currentCell, a_player);
		}

		LogWarningWithLocalPlayerPrefix(
			"Stage 3.7 PlaceAtMe-backed proxy spawn failed: custom proxy base unavailable and Codsworth fallback is disabled.");
		return false;
	}

	RE::Actor* TryIsolatePlaceAtMeBackedProxyCandidate(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player, std::size_t& a_newCandidateCount)
	{
		++g_placeAtMeWindowScans;
		RE::Actor* nearestNewCandidate = nullptr;
		auto nearestNewDistance = std::numeric_limits<float>::max();
		const auto playerPosition = a_player.GetPosition();

		a_currentCell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			auto* actor = a_ref->As<RE::Actor>();
			if (!actor || !IsPlaceAtMeBackedProxyCandidateActor(*actor, a_player)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto position = actor->GetPosition();
			const auto distanceFromLocalPlayer = GetDistance(position, playerPosition);
			const auto inPreSpawnSnapshot =
				g_placeAtMeBackedProxyPreexistingCandidateRefs.find(actor->GetFormID()) !=
				g_placeAtMeBackedProxyPreexistingCandidateRefs.end();
			const auto acceptedAsNew =
				!inPreSpawnSnapshot &&
				distanceFromLocalPlayer <= kDebugPlaceAtMeSpawnDetectionRadius;

			if (g_placeAtMeWindowCandidateLogs < F4T::ProxySaveSafety::kMaximumWindowCandidateLogLines) {
				++g_placeAtMeWindowCandidateLogs;
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 3.7 PlaceAtMe-backed proxy candidate considered: actor={:08X}, base={:08X}, inPreSpawnSnapshot={}, acceptedAsNew={}, position={}, distanceFromLocalPlayer={:.2f}, Get3D={}, GetFullyLoaded3D={}, GetCurrent3D={}, IsVisible={}.",
					actor->GetFormID(),
					actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
					inPreSpawnSnapshot,
					acceptedAsNew,
					FormatPosition(position),
					distanceFromLocalPlayer,
					actor->Get3D() != nullptr,
					actor->GetFullyLoaded3D() != nullptr,
					actor->GetCurrent3D() != nullptr,
					actor->IsVisible()));
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"[RT-VERIFY 408-C] PlaceAtMe window candidate actor={:08X} inPreSpawnSnapshot={} acceptedAsNew={} scan={}",
					actor->GetFormID(),
					inPreSpawnSnapshot,
					acceptedAsNew,
					g_placeAtMeWindowScans);
			} else {
				++g_placeAtMeWindowSuppressedLogs;
			}
			if (acceptedAsNew) {
				++a_newCandidateCount;
			}
			if (acceptedAsNew && distanceFromLocalPlayer < nearestNewDistance) {
				nearestNewDistance = distanceFromLocalPlayer;
				nearestNewCandidate = actor;
			}

			return RE::BSContainer::ForEachResult::kContinue;
		});

		if (F4T::ProxySaveSafety::ShouldLogPeriodic(g_placeAtMeWindowScans)) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] PlaceAtMe window scan={} newCandidates={} nearest={:08X} thread={}",
				g_placeAtMeWindowScans,
				a_newCandidateCount,
				nearestNewCandidate ? nearestNewCandidate->GetFormID() : 0u,
				REX::W32::GetCurrentThreadId());
		}
		return nearestNewCandidate;
	}

	void StartPlaceAtMeBackedProxySettle(RE::Actor& a_actor, std::size_t a_newCandidateCount)
	{
		// TODO-verify(#407): lone foreign 0x179FF candidate source:rt-briefs/thread-408.md (comment 20722)
		if (F4T::ProxySaveSafety::IsUniqueStandInCandidate(a_newCandidateCount)) {
			F4T::ProxySaveSafety::RecordStandIn(a_actor.GetFormID());
		} else {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 3.7 PlaceAtMe-backed proxy actor={:08X} was not recorded as a CO stand-in: {} new base candidates appeared in the window.",
				a_actor.GetFormID(),
				a_newCandidateCount));
		}
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-C] PlaceAtMe window CLOSE settle actor={:08X} base={:08X} newCandidates={} scans={} suppressed={} candidate lines recorded={} thread={}",
			a_actor.GetFormID(),
			g_placeAtMeBackedProxyBaseFormId,
			a_newCandidateCount,
			g_placeAtMeWindowScans,
			g_placeAtMeWindowSuppressedLogs,
			g_placeAtMeWindowCandidateLogs,
			REX::W32::GetCurrentThreadId());
		g_placeAtMeBackedProxyPendingHandle = a_actor.GetHandle();
		g_placeAtMeBackedProxySettleStartTime = std::chrono::steady_clock::now();
		g_placeAtMeBackedProxySpawnState = PlaceAtMeBackedProxySpawnState::kSettling;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 3.7 new PlaceAtMe-backed proxy isolated: actor={:08X}, base={:08X}, source={}. Waiting {:.1f} seconds before remote assignment.",
			a_actor.GetFormID(),
			g_placeAtMeBackedProxyBaseFormId,
			GetPlaceAtMeBackedProxySourceLabel(g_placeAtMeBackedProxySource),
			kPlaceAtMeProxySettleSeconds));
	}

	void PromotePlaceAtMeBackedProxy(RE::Actor& a_actor)
	{
		const auto has3D = a_actor.Get3D() != nullptr;
		const auto isVisible = a_actor.IsVisible();
		if (!has3D || !isVisible) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 3.7 PlaceAtMe-backed runtime proxy settled with partial visibility: Get3D={}, IsVisible={}. Continuing to remote movement for diagnostic testing.",
				has3D,
				isVisible));
		}

		g_runtimeProxyHandle = a_actor.GetHandle();
		g_runtimeProxySpawnSucceeded = true;
		g_runtimeProxySpawnAttemptInProgress = false;
		g_runtimeProxySpawnFailed = false;
		g_placeAtMeBackedProxySpawnState = PlaceAtMeBackedProxySpawnState::kPromoted;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 3.7 PlaceAtMe-backed runtime proxy ready: actor={:08X}. Promoting to active proxy representation.",
			a_actor.GetFormID()));

		if (!g_stage37ResultLogged) {
			if (g_placeAtMeBackedProxySource == RuntimeActorBaseSource::kCustomProxy) {
				LogInfoWithLocalPlayerPrefix(
					"Stage 3.7 result: PASS. PlaceAtMe-backed custom runtime proxy spawned, isolated, settled, and promoted to active proxy.");
			} else {
				LogWarningWithLocalPlayerPrefix(
					"Stage 3.7 result: PARTIAL. PlaceAtMe-backed Codsworth fallback spawned and became active, but custom F4T_RemotePlayerProxy did not. Investigate the custom proxy base in Creation Kit.");
			}
			g_stage37ResultLogged = true;
		}
	}

	bool MaintainPlaceAtMeBackedRuntimeProxySpawn(RE::TESObjectCELL& a_currentCell, const RE::PlayerCharacter& a_player)
	{
		if (!g_stage37OlderDiagnosticsDisabledLogged) {
			LogInfoWithLocalPlayerPrefix(
				"Stage 3.7 active: older Stage 3.4/3.6 standalone diagnostics are disabled to avoid interfering with proxy spawn.");
			g_stage37OlderDiagnosticsDisabledLogged = true;
		}

		if (g_placeAtMeBackedProxySpawnState == PlaceAtMeBackedProxySpawnState::kPromoted ||
			g_placeAtMeBackedProxySpawnState == PlaceAtMeBackedProxySpawnState::kFailed) {
			return false;
		}

		if (g_placeAtMeBackedProxySpawnState == PlaceAtMeBackedProxySpawnState::kNotStarted) {
			if (!TryBeginPlaceAtMeBackedProxySpawn(a_currentCell, a_player)) {
				MarkPlaceAtMeBackedProxySpawnFailed();
				return false;
			}
			return true;
		}

		if (IsPlaceAtMeBackedProxyAttempting(g_placeAtMeBackedProxySpawnState)) {
			std::size_t newCandidateCount = 0;
			if (auto* actor = TryIsolatePlaceAtMeBackedProxyCandidate(a_currentCell, a_player, newCandidateCount)) {
				StartPlaceAtMeBackedProxySettle(*actor, newCandidateCount);
				return true;
			}

			const auto elapsedSeconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - g_placeAtMeBackedProxyCandidateSearchStartTime).count();
			if (elapsedSeconds < kPlaceAtMeProxyCandidateSearchSeconds) {
				LogThrottledInfo(
					"stage37_placeatme_proxy_no_new_candidate_yet",
					std::format(
						"Stage 3.7 PlaceAtMe-backed proxy isolation: no new actor found yet for base={:08X}; continuing search.",
						g_placeAtMeBackedProxyBaseFormId),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
				return true;
			}

			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] PlaceAtMe window CLOSE timeout base={:08X} scans={} suppressed={} candidate lines elapsedSeconds={:.2f} thread={}",
				g_placeAtMeBackedProxyBaseFormId,
				g_placeAtMeWindowScans,
				g_placeAtMeWindowSuppressedLogs,
				elapsedSeconds,
				REX::W32::GetCurrentThreadId());
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 3.7 PlaceAtMe-backed proxy spawn failed: no new actor could be isolated for base={:08X}.",
				g_placeAtMeBackedProxyBaseFormId));
			if (g_placeAtMeBackedProxySpawnState == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting &&
				ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
				LogWarningWithLocalPlayerPrefix(
					"Stage 3.7 custom proxy base unavailable; falling back to Codsworth diagnostic base 000179FF.");
				if (TryBeginCodsworthPlaceAtMeBackedProxyFallback(a_currentCell, a_player)) {
					return true;
				}
			}

			MarkPlaceAtMeBackedProxySpawnFailed();
			return false;
		}

		if (g_placeAtMeBackedProxySpawnState == PlaceAtMeBackedProxySpawnState::kSettling) {
			const auto ref = g_placeAtMeBackedProxyPendingHandle.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor || !IsValidProxyActor(*actor, a_currentCell, a_player)) {
				LogWarningWithLocalPlayerPrefix(
					"Stage 3.7 PlaceAtMe-backed runtime proxy settle failed: pending actor handle no longer resolves or failed validation.");
				MarkPlaceAtMeBackedProxySpawnFailed();
				return false;
			}

			const auto elapsedSeconds = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - g_placeAtMeBackedProxySettleStartTime).count();
			const auto remainingSeconds = std::max(0.0F, kPlaceAtMeProxySettleSeconds - elapsedSeconds);
			LogThrottledInfo(
				"stage37_placeatme_proxy_movement_deferred",
				std::format(
					"Stage 3.7 PlaceAtMe-backed runtime proxy is settling; normal proxy movement is deferred. settleRemaining={:.1f}s.",
					remainingSeconds),
				kRuntimeProxyVisibilityDiagnosticLogInterval);
			if (ShouldLog("stage37_placeatme_proxy_settle_diagnostic", kRuntimeProxyVisibilityDiagnosticLogInterval)) {
				LogPlaceAtMeBackedProxySettleDiagnostic(*actor, a_player, elapsedSeconds, remainingSeconds);
			}

			if (elapsedSeconds < kPlaceAtMeProxySettleSeconds) {
				return true;
			}

			PromotePlaceAtMeBackedProxy(*actor);
			return false;
		}

		return false;
	}
}

namespace F4T::ProxyActorController::detail
{
	const char* GetPlaceAtMeBackedProxySourceLabel(RuntimeActorBaseSource a_source)
	{
		switch (a_source) {
		case RuntimeActorBaseSource::kCustomProxy:
			return "custom COPlayerProxy base";
		case RuntimeActorBaseSource::kVanillaDiagnosticNpc:
			return "vanilla Codsworth fallback";
		default:
			return "unknown";
		}
	}

	bool IsPlaceAtMeBackedProxyAttempting(PlaceAtMeBackedProxySpawnState a_state)
	{
		return a_state == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting ||
		       a_state == PlaceAtMeBackedProxySpawnState::kCodsworthFallbackAttempting;
	}

	bool ShouldUseCustomProxyBaseForPlaceAtMeBackedSpawn()
	{
		return kDebugPlaceAtMeSpawnUseCustomProxyBase;
	}

	bool ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()
	{
		return kDebugPlaceAtMeSpawnFallbackToCodsworth;
	}

	void LogPlaceAtMeBackedProxySettleDiagnostic(
		RE::Actor& a_actor,
		const RE::PlayerCharacter& a_player,
		float a_settleElapsedSeconds,
		float a_settleRemainingSeconds)
	{
		const auto* parentCell = a_actor.GetParentCell();
		const auto position = a_actor.GetPosition();
		const auto playerPosition = a_player.GetPosition();
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 3.7 PlaceAtMe-backed proxy settle diagnostic: actor={:08X}, base={:08X}, source={}, position={}, localPlayerPosition={}, distanceFromLocalPlayer={:.2f}, parentCell={}, Get3D={}, GetFullyLoaded3D={}, GetCurrent3D={}, IsVisible={}, settleElapsed={:.1f}s, settleRemaining={:.1f}s.",
			a_actor.GetFormID(),
			a_actor.GetNPC() ? a_actor.GetNPC()->GetFormID() : 0,
			GetPlaceAtMeBackedProxySourceLabel(g_placeAtMeBackedProxySource),
			FormatPosition(position),
			FormatPosition(playerPosition),
			GetDistance(position, playerPosition),
			parentCell ? std::format("{:08X}", parentCell->GetFormID()) : "unavailable",
			a_actor.Get3D() != nullptr,
			a_actor.GetFullyLoaded3D() != nullptr,
			a_actor.GetCurrent3D() != nullptr,
			a_actor.IsVisible(),
			a_settleElapsedSeconds,
			a_settleRemainingSeconds));
	}
}
