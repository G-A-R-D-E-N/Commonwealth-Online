#include "pch.h"

#include "F4TDownedGlueInternal.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;
}

namespace F4T::Downed::GlueInternal
{
	void EnterDownedPhase(double a_nowSeconds)
	{
		if (!g_state.EnterDowned(a_nowSeconds)) {
			return;
		}
		g_nextPoseRetrySeconds = 0.0;
		ReapplyDownedPose(a_nowSeconds);
		SyncLocalCanStimpak(true);
		F4T::DebugLogging::Log(
			kLogComponent,
			"local player downed (down #{}) with {:.1f}s on the local fallback timer",
			g_state.GetDownCount(),
			g_state.GetConfig().downedSeconds);
	}

	void EnterDeadPhase(double a_nowSeconds)
	{
		auto* player = LocalPlayer();
		const auto* health = HealthInfo();
		if (player && health && CanTouchWorld()) {
			(void)SetHealth(*player, *health, 0.0F);
			SetControlBlocked(*player, true);
		}
		SyncLocalCanStimpak(false);
		F4T::DebugLogging::Log(kLogComponent, "local player died at {:.1f}; waiting for respawn", a_nowSeconds);
	}

	void LeaveIncapacitated(bool a_restoreHealth)
	{
		auto* player = LocalPlayer();
		if (player && CanTouchWorld()) {
			SetPose(*player, false);
			SetControlBlocked(*player, false);
			if (a_restoreHealth) {
				RestoreLocalPlayerHealth();
			}
		}
		SyncLocalCanStimpak(false);
		g_poseApplied = false;
	}

	void HideOverlay()
	{
		g_lastOverlayRemainingMs.reset();
		g_lastOverlayRescuerNearby.reset();
		if (!g_overlayVisible) {
			return;
		}
		g_overlayVisible = false;
		if (auto* host = g_uiHost.load()) {
			host->HideDownedOverlay();
		}
	}

	void HideRespawnMenu()
	{
		if (!g_respawnMenuVisible) {
			return;
		}
		g_respawnMenuVisible = false;
		if (auto* host = g_uiHost.load()) {
			host->HideRespawnMenu();
		}
	}

	void SyncLocalCanStimpak(bool a_downed)
	{
		if (!F4T::Downed::ShouldApplyCanStimpak(g_localCanStimpak, a_downed)) {
			return;
		}
		auto* player = LocalPlayer();
		if (!player || !CanTouchWorld()) {
			return;
		}
		if (SetCanStimpak(*player, a_downed)) {
			g_localCanStimpak = a_downed;
		}
	}

	void ReapplyDownedPose(double a_nowSeconds)
	{
		if (g_state.GetPhase() != Phase::kDowned || g_poseApplied || a_nowSeconds < g_nextPoseRetrySeconds) {
			return;
		}
		auto* player = LocalPlayer();
		if (!player || !CanTouchWorld()) {
			return;
		}
		g_nextPoseRetrySeconds = a_nowSeconds + kRemotePoseRetrySeconds;
		SetControlBlocked(*player, true);
		g_poseApplied = SetPose(*player, true);
	}

	void HandleEngineLethalDamage(double a_nowSeconds)
	{
		auto* player = LocalPlayer();
		const auto* health = HealthInfo();
		if (!player || !health || !CanTouchWorld()) {
			return;
		}
		if (player->IsDead(true)) {
			if (g_state.GetPhase() == Phase::kDead) {
				return;
			}
			player->Resurrect(false, true);
			LogInfo("local player was killed by the engine; resurrected into the downed state.");
			EnterDownedPhase(a_nowSeconds);
			return;
		}
		const auto current = player->GetActorValue(*health);
		if (!std::isfinite(current)) {
			return;
		}
		if (g_state.GetPhase() == Phase::kDowned) {
			const auto sliver = F4T::Downed::ComputeSliverHealth(GetMaximumHealth(player, *health), g_state.GetConfig());
			if (current < sliver) {
				(void)SetHealth(*player, *health, sliver);
			}
			return;
		}
		if (current <= 0.0F) {
			EnterDownedPhase(a_nowSeconds);
		}
	}

	void EndSession()
	{
		if (g_state.IsIncapacitated() && !CanTouchWorld()) {
			return;
		}
		ResetSession();
		(void)DrainEvents();
	}

	void ResetSession()
	{
		if (g_state.IsIncapacitated()) {
			if (auto* player = LocalPlayer(); player && CanTouchWorld() && player->IsDead(true)) {
				player->Resurrect(false, true);
			}
			LeaveIncapacitated(true);
			LogInfo("session ended while incapacitated; local player restored to alive.");
		}
		HideOverlay();
		HideRespawnMenu();
		g_state.Reset();
		g_pendingRespawnPoint.reset();
		g_pendingRespawnFriendId.reset();
		g_respawnRequestAttempts = 0;
		g_checkpoint.reset();
		F4T::Downed::ResetReviveHold(g_reviveHold);
		ClearReviveChannel();
		g_remotePoses.clear();
		g_remoteCanStimpak.clear();
		ApplySessionFlags(false);
	}

	class PluginTransport final : public F4T::Downed::IDownedTransport
	{
	public:
		bool Send(std::string_view a_packet) override
		{
			if (!F4T::Networking::IsConnectedToServer()) {
				return false;
			}
			return F4T::Networking::Detail::SendPacketBytes(a_packet);
		}
	};

	PluginTransport g_pluginTransport;

	void InstallTransport()
	{
		if (g_transportInstalled) {
			return;
		}
		g_transportInstalled = true;
		F4T::Downed::SetDownedTransport(&g_pluginTransport);
	}
}

namespace F4T::Downed
{
	void SetDownedUiHost(IDownedUiHost* a_host)
	{
		g_uiHost.store(a_host);
		F4T::DebugLogging::Log(kLogComponent, "respawn UI host set: {}", a_host != nullptr);
	}

	bool HasDownedUiHost()
	{
		return g_uiHost.load() != nullptr;
	}

	void SetDownedTransport(IDownedTransport* a_transport)
	{
		g_transport.store(a_transport);
	}
}

