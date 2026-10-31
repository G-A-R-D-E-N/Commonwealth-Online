#include "pch.h"

#include "F4TDownedGlueInternal.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;
}

namespace F4T::Downed
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet)
	{
		DownedEvent event;
		switch (TryDecodeDownedEvent(a_type, a_packet, event)) {
		case DecodeStatus::kUnknown:
			return false;
		case DecodeStatus::kMalformed:
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_packet_malformed",
				std::format("Ignoring malformed Commonwealth Online '{}' packet.", std::string{ a_type }));
			return true;
		case DecodeStatus::kHandled:
		default:
			QueueEvent(event);
			return true;
		}
	}

	void NoteWelcome()
	{
		(void)DrainEvents();
		g_connectionResetPending.store(true);
	}

	float ResolveLocalPlayerDamage(float a_healthBefore, float a_maximumHealth, float a_damage)
	{
		if (g_state.IsIncapacitated()) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"ignoring {:.1f} damage while incapacitated (phase {})",
				a_damage,
				static_cast<int>(g_state.GetPhase()));
			return 0.0F;
		}
		const auto outcome = ResolveLethalDamage(a_healthBefore, a_maximumHealth, a_damage, g_state.GetConfig());
		if (outcome.enteredDowned) {
			EnterDownedPhase(NowSeconds());
		}
		return outcome.appliedDamage;
	}

	bool IsLocalPlayerDowned()
	{
		return g_state.GetPhase() == Phase::kDowned;
	}

	bool IsLocalPlayerDead()
	{
		return g_state.GetPhase() == Phase::kDead;
	}

	void Tick()
	{
		const auto now = NowSeconds();
		InstallTransport();
		InstallReviveInput();

		if (!F4T::Networking::IsConnectedToServer()) {
			EndSession();
			return;
		}
		if (CanTouchWorld() && g_connectionResetPending.exchange(false)) {
			ResetSession();
		}

		ApplySessionFlags(true);
		ApplyPendingEvents(now);
		RecordLastBed();
		HandleEngineLethalDamage(now);

		if (g_state.Tick(now)) {
			EnterDeadPhase(now);
			ShowRespawnMenu();
		}
		RetryPendingRespawn(now);
		ReapplyDownedPose(now);

		if (!CanTouchWorld()) {
			return;
		}
		const auto snapshot = F4T::RemotePlayerState::GetRemotePlayerSnapshot();
		UpdateRemotePoses(snapshot, now);
		UpdateReviveInteraction(snapshot, now);
		UpdateReviveChannel(snapshot, now);
		UpdateOverlay(snapshot, now);
	}
}

