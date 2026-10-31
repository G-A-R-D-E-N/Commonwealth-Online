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
			return;
		}
		const auto target = FindReviveTarget(a_snapshot);
		const auto activateHeld = IsActivateHeld();
		if (!F4T::Downed::AdvanceReviveHold(g_reviveHold, target, activateHeld, a_nowSeconds, g_state.GetConfig()) || !target) {
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
				(void)SpendStimpak();
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
		if (!F4T::Downed::ShouldPushDownedOverlay(
				remainingMs,
				rescuerNearby,
				g_lastOverlayRemainingMs,
				g_lastOverlayRescuerNearby)) {
			return;
		}
		g_lastOverlayRemainingMs = remainingMs;
		g_lastOverlayRescuerNearby = rescuerNearby;
		g_overlayVisible = true;
		host->ShowDownedOverlay(remainingMs, rescuerNearby);
	}

	void ApplyReviveStartedEvent(const DownedEvent& a_event, double a_nowSeconds)
	{
		if (!F4T::RemotePlayerState::IsAssignedPlayerId(a_event.byPlayerId)) {
			return;
		}
		g_channel.Begin(a_event.playerId, a_event.channelMs, a_nowSeconds);
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
		const auto request = EncodeReviveRequest(a_targetPlayerId, a_stimpaks);
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
}

