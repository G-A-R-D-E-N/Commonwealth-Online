#include "pch.h"

#include "F4TDownedGlueInternal.h"
#include "F4TSocialNameplate.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;
}

namespace F4T::Downed::GlueInternal
{
	namespace
	{
		inline constexpr double kPromptProgressStep = 0.05;

		std::optional<std::uint32_t> g_promptTarget;
		double g_lastPromptProgress{ -1.0 };

		double ReviveHoldProgress(std::uint32_t a_target, double a_nowSeconds)
		{
			if (!g_reviveHold.armed || g_reviveHold.target != a_target || !std::isfinite(a_nowSeconds)) {
				return 0.0;
			}
			const auto holdSeconds = g_state.GetConfig().reviveHoldSeconds;
			if (!std::isfinite(holdSeconds) || holdSeconds <= 0.0) {
				return 0.0;
			}
			return (std::clamp)((a_nowSeconds - g_reviveHold.armedAtSeconds) / holdSeconds, 0.0, 1.0);
		}

		void HideRevivePrompt()
		{
			if (!g_promptTarget) {
				return;
			}
			g_promptTarget.reset();
			g_lastPromptProgress = -1.0;
			if (auto* host = g_uiHost.load()) {
				host->HideRevivePrompt();
			}
		}

		void UpdateRevivePrompt(std::uint32_t a_target, const RemoteSnapshot& a_snapshot, double a_nowSeconds)
		{
			const auto progress = ReviveHoldProgress(a_target, a_nowSeconds);
			if (g_promptTarget == a_target && std::fabs(progress - g_lastPromptProgress) < kPromptProgressStep) {
				return;
			}
			auto* host = g_uiHost.load();
			if (!host) {
				return;
			}
			g_promptTarget = a_target;
			g_lastPromptProgress = progress;
			host->ShowRevivePrompt(TargetName(a_target, a_snapshot), progress);
		}
	}

	bool IsRescuerNearby(const RemoteSnapshot& a_snapshot)
	{
		auto* player = LocalPlayer();
		if (!player) {
			return false;
		}
		const auto playerPosition = player->GetPosition();
		for (const auto& remote : a_snapshot) {
			if (!remote.vitals.has_value() || remote.vitals->isDowned || remote.vitals->isDead) {
				continue;
			}
			auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(remote.playerId);
			if (proxy && DistanceBetween(playerPosition, proxy->GetPosition()) <= g_state.GetConfig().reviveRange) {
				return true;
			}
		}
		return false;
	}

	std::optional<std::uint32_t> FindReviveTarget(const RemoteSnapshot& a_snapshot)
	{
		auto* player = LocalPlayer();
		if (!player) {
			return std::nullopt;
		}
		const auto playerPosition = player->GetPosition();
		const auto heading = player->GetHeading();

		std::vector<F4T::Downed::ReviveCandidate> candidates;
		for (const auto& remote : a_snapshot) {
			if (!F4T::Downed::IsReviveCandidate(
					remote.vitals.has_value(),
					remote.vitals && remote.vitals->isDowned,
					remote.vitals && remote.vitals->isDead)) {
				continue;
			}
			auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(remote.playerId);
			if (!proxy) {
				continue;
			}
			const auto distance = DistanceBetween(playerPosition, proxy->GetPosition());
			candidates.push_back(F4T::Downed::ReviveCandidate{
				remote.playerId,
				distance,
				FacingCosine(playerPosition, heading, proxy->GetPosition())
			});
		}
		return F4T::Downed::SelectReviveTarget(candidates, g_state.GetConfig());
	}

	void UpdateReviveInteraction(const RemoteSnapshot& a_snapshot, double a_nowSeconds)
	{
		if (!F4T::Downed::CanReviveOthers(g_state.GetPhase())) {
			F4T::Downed::ResetReviveHold(g_reviveHold);
			HideRevivePrompt();
			return;
		}
		const auto target = FindReviveTarget(a_snapshot);
		const auto activateHeld = IsActivateHeld();
		const auto holdCompleted = F4T::Downed::AdvanceReviveHold(g_reviveHold, target, activateHeld, a_nowSeconds, g_state.GetConfig());
		if (!target || g_channel.IsLive()) {
			HideRevivePrompt();
		} else {
			UpdateRevivePrompt(*target, a_snapshot, a_nowSeconds);
		}
		if (!holdCompleted || !target) {
			return;
		}
		const auto stimpaks = CountStimpaks();
		if (!F4T::Downed::HasStimpakForRevive(stimpaks)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_revive_no_stimpak",
				"No Stimpak left to revive with; the claim was not sent.");
			return;
		}
		F4T::DebugLogging::Log(
			kLogComponent,
			"revive hold completed on playerId={} with {} Stimpak(s)",
			*target,
			stimpaks.value_or(0));
		if (!F4T::Downed::SendReviveRequest(*target, stimpaks)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_revive_request",
				"Could not send Commonwealth Online reviveRequest.");
		}
	}

	void UpdateSelfRevive(double a_nowSeconds)
	{
		if (g_selfRevivePendingAt && a_nowSeconds - *g_selfRevivePendingAt > kSelfReviveRequestTimeoutSeconds) {
			g_selfRevivePendingAt.reset();
		}
		const bool wanted = g_selfReviveRequested.exchange(false);
		if (!wanted) {
			return;
		}
		const auto localId = F4T::RemotePlayerState::GetAssignedPlayerId();
		if (g_state.GetPhase() != Phase::kDowned || !localId || !F4T::Networking::IsConnectedToServer() ||
			g_channel.IsLive() || g_selfRevivePendingAt || !CanTouchWorld()) {
			return;
		}
		const auto stimpaks = CountStimpaks();
		if (!F4T::Downed::HasStimpakForRevive(stimpaks)) {
			return;
		}
		F4T::DebugLogging::Log(kLogComponent, "self revive requested with {} Stimpak(s)", stimpaks.value_or(0));
		if (!F4T::Downed::SendReviveRequest(*localId, stimpaks)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_self_revive_request",
				"Could not send the Commonwealth Online self revive request.");
			return;
		}
		g_selfRevivePendingAt = a_nowSeconds;
	}

	std::string TargetName(std::uint32_t a_playerId, const RemoteSnapshot& a_snapshot)
	{
		for (const auto& remote : a_snapshot) {
			if (remote.playerId == a_playerId) {
				return F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName);
			}
		}
		return {};
	}

	void ClearReviveChannel()
	{
		HideRevivePrompt();
		if (!g_channel.IsLive()) {
			return;
		}
		g_channel.Clear();
		g_lastChannelProgress = -1.0;
		if (auto* host = g_uiHost.load()) {
			host->HideReviveChannel();
		}
	}

	void UpdateReviveChannel(const RemoteSnapshot& a_snapshot, double a_nowSeconds)
	{
		if (!g_channel.IsLive()) {
			return;
		}
		const auto target = g_channel.GetChannel().targetPlayerId;
		auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(target);
		auto* player = LocalPlayer();
		const bool inRange = proxy && player &&
			DistanceBetween(player->GetPosition(), proxy->GetPosition()) <= g_state.GetConfig().reviveRange;

		bool targetDowned = true;
		for (const auto& remote : a_snapshot) {
			if (remote.playerId != target || !remote.vitals.has_value()) {
				continue;
			}
			targetDowned = remote.vitals->isDowned && !remote.vitals->isDead;
		}

		const auto step = g_channel.Tick(
			a_nowSeconds,
			IsActivateHeld(),
			proxy != nullptr,
			targetDowned,
			inRange,
			CountStimpaks());

		switch (step.outcome) {
		case F4T::Downed::ReviveChannelOutcome::kContinue:
			break;
		case F4T::Downed::ReviveChannelOutcome::kResolved:
			if (step.spend) {
				const auto spent = F4T::Downed::SpendStimpaks(step.stimpaks, [] { return SpendStimpak(); });
				F4T::DebugLogging::Log(kLogComponent, "revive on playerId={} spent {} of {} Stimpak(s)", target, spent, step.stimpaks);
			}
			F4T::DebugLogging::Log(kLogComponent, "revive channel on playerId={} landed", target);
			ClearReviveChannel();
			return;
		default:
			F4T::DebugLogging::Log(
				kLogComponent,
				"revive channel on playerId={} ended: {}",
				target,
				step.reason);
			if (!F4T::Downed::SendReviveCancel(target, step.reason)) {
				F4T::Networking::Detail::LogThrottledWarning(
					"downed_revive_cancel",
					"Could not send Commonwealth Online reviveCancel.");
			}
			ClearReviveChannel();
			return;
		}

		const auto progress = step.progress;
		auto* host = g_uiHost.load();
		if (host && std::fabs(progress - g_lastChannelProgress) >= kChannelProgressStep) {
			g_lastChannelProgress = progress;
			host->ShowReviveChannel(TargetName(target, a_snapshot), progress);
		}
	}

	void UpdateRemotePoses(const RemoteSnapshot& a_snapshot, double a_nowSeconds)
	{
		for (const auto& remote : a_snapshot) {
			const bool downed = remote.vitals && (remote.vitals->isDowned || remote.vitals->isDead);
			auto& pose = g_remotePoses[remote.playerId];
			if (!F4T::Downed::ShouldApplyRemotePose(pose, downed, a_nowSeconds)) {
				continue;
			}
			auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(remote.playerId);
			if (!proxy) {
				pose.retryPending = true;
				pose.nextAttemptSeconds = a_nowSeconds + kRemotePoseRetrySeconds;
				continue;
			}
			const bool hasKeyword = g_remoteCanStimpak[remote.playerId];
			if (F4T::Downed::ShouldApplyCanStimpak(hasKeyword, downed) && SetCanStimpak(*proxy, downed)) {
				g_remoteCanStimpak[remote.playerId] = downed;
			}
			const auto posed = SetPose(*proxy, downed);
			SetControlBlocked(*proxy, downed);
			pose.retryPending = !posed;
			pose.nextAttemptSeconds = posed ? 0.0 : a_nowSeconds + kRemotePoseRetrySeconds;
			if (posed) {
				pose.downed = downed;
			}
		}

		for (auto it = g_remotePoses.begin(); it != g_remotePoses.end();) {
			const auto present = std::any_of(
				a_snapshot.begin(),
				a_snapshot.end(),
				[playerId = it->first](const F4T::RemotePlayerState::RemotePlayerState& a_remote) {
					return a_remote.playerId == playerId;
				});
			if (!present) {
				g_remoteCanStimpak.erase(it->first);
			}
			it = present ? std::next(it) : g_remotePoses.erase(it);
		}
	}

	void UpdateOverlay(const RemoteSnapshot& a_snapshot, double a_nowSeconds)
	{
		if (g_state.GetPhase() != Phase::kDowned) {
			HideOverlay();
			return;
		}
		auto* host = g_uiHost.load();
		if (!host) {
			return;
		}
		const auto rescuerNearby = IsRescuerNearby(a_snapshot);
		const auto remainingMs = g_state.GetRemainingMs(a_nowSeconds);
		const auto stimpaks = CountStimpaks().value_or(0);
		if (!F4T::Downed::ShouldPushDownedOverlay(
				remainingMs,
				rescuerNearby,
				g_lastOverlayRemainingMs,
				g_lastOverlayRescuerNearby,
				stimpaks,
				g_lastOverlayStimpaks)) {
			return;
		}
		g_lastOverlayRemainingMs = remainingMs;
		g_lastOverlayRescuerNearby = rescuerNearby;
		g_lastOverlayStimpaks = stimpaks;
		g_overlayVisible = true;
		host->ShowDownedOverlay(remainingMs, rescuerNearby, stimpaks);
	}

	void ApplyReviveStartedEvent(const DownedEvent& a_event, double a_nowSeconds)
	{
		if (!F4T::RemotePlayerState::IsAssignedPlayerId(a_event.byPlayerId)) {
			return;
		}
		if (a_event.channelMs == 0) {
			const auto spent = F4T::Downed::SpendStimpaks(a_event.stimpaksSpent, [] { return SpendStimpak(); });
			F4T::DebugLogging::Log(kLogComponent, "instant revive on playerId={} spent {} of {} Stimpak(s)", a_event.playerId, spent, a_event.stimpaksSpent);
			return;
		}
		g_channel.Begin(a_event.playerId, a_event.channelMs, a_nowSeconds, a_event.stimpaksSpent);
		g_lastChannelProgress = -1.0;

		auto* player = LocalPlayer();
		auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(a_event.playerId);
		if (player && proxy && CanTouchWorld()) {
			if (SetCanStimpak(*proxy, true)) {
				g_remoteCanStimpak[a_event.playerId] = true;
			}
			if (!PlayStimpakReviveIdle(*player, *proxy)) {
				F4T::Networking::Detail::LogThrottledWarning(
					"downed_revive_idle",
					"The engine refused the Stimpak revive idle on the downed player.");
			}
		}
		F4T::DebugLogging::Log(
			kLogComponent,
			"revive channel started on playerId={} for {} ms",
			a_event.playerId,
			a_event.channelMs);
	}

	void ApplyReviveCancelledEvent(const DownedEvent& a_event)
	{
		if (!g_channel.IsLive() || g_channel.GetChannel().targetPlayerId != a_event.playerId) {
			return;
		}
		F4T::DebugLogging::Log(
			kLogComponent,
			"revive channel on playerId={} cancelled by the server: {}",
			a_event.playerId,
			a_event.reason);
		ClearReviveChannel();
	}
}

namespace F4T::Downed
{
	bool SendReviveRequest(std::uint32_t a_targetPlayerId, std::optional<std::uint32_t> a_stimpaks)
	{
		const auto request = EncodeReviveRequest(a_targetPlayerId, ClampReportedStimpaks(a_stimpaks));
		auto* transport = g_transport.load();
		if (!request || !transport) {
			return false;
		}
		return transport->Send(request->dump());
	}

	bool SendReviveCancel(std::uint32_t a_targetPlayerId, std::string_view a_reason)
	{
		const auto request = EncodeReviveCancel(a_targetPlayerId, a_reason);
		auto* transport = g_transport.load();
		if (!request || !transport) {
			return false;
		}
		return transport->Send(request->dump());
	}

	void OnSelfReviveChosen()
	{
		g_selfReviveRequested.store(true);
	}
}

