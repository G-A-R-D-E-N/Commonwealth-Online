#include "pch.h"

#include "F4TDownedGlueInternal.h"
#include "F4TDeathBagGrant.h"
#include "F4TDeathBagLocal.h"
#include "F4TDeathBagView.h"
#include "F4TNetworkingInternal.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;

	bool SendDeathBagAttempt(const nlohmann::json& a_packet, std::uint64_t a_generation)
	{
		return F4T::Networking::Detail::SendPacketBytesForGeneration(a_packet.dump(), a_generation);
	}
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
			if ((event.kind == EventKind::kDownedState || event.kind == EventKind::kPlayerDied) &&
				!F4T::RemotePlayerState::IsAssignedPlayerId(event.playerId)) {
				const auto state = a_packet.find("serverDowned");
				const auto incarnation = a_packet.find("playerIncarnation");
				if (state != a_packet.end() && state->is_object() && incarnation != a_packet.end() && incarnation->is_number_unsigned()) {
					const auto revision = state->find("revision");
					const auto downed = state->find("downed");
					const auto dead = state->find("dead");
					if (revision != state->end() && revision->is_number_unsigned() && downed != state->end() && downed->is_boolean() &&
						dead != state->end() && dead->is_boolean()) {
						F4T::RemotePlayerState::SetRemotePlayerAuthoritativeDowned(event.playerId, incarnation->get<std::uint64_t>(), revision->get<std::uint64_t>(), downed->get<bool>(), dead->get<bool>());
						return true;
					}
				}
			}
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
			F4T::DeathBagLocal::AbandonPending();
			F4T::DeathBagView::Reset();
			EndSession();
			return;
		}
		if (CanTouchWorld() && g_connectionResetPending.exchange(false)) {
			ResetSession();
		}

		ApplySessionFlags(true);
		ApplyPendingEvents(now);
		UpdateBleedoutRecovery(now);
		RecordLastBed();
		HandleEngineLethalDamage(now);

		if (g_state.Tick(now)) {
			EnterDeadPhase(now);
			ShowRespawnMenu();
		}
		RetryPendingRespawn(now);
		ReapplyDownedPose(now);
		if (!g_state.IsIncapacitated()) {
			ReleaseStaleControlBlock();
			SampleLocalCamera();
		}
		UpdateSelfRevive(now);

		if (!CanTouchWorld()) {
			return;
		}
		F4T::DeathBagView::Tick(now);
		F4T::DeathBagGrant::ApplyReconciliation(SendDeathBagAttempt);
		const auto snapshot = F4T::RemotePlayerState::GetRemotePlayerSnapshot();
		UpdateRemotePoses(snapshot, now);
		UpdateReviveInteraction(snapshot, now);
		UpdateReviveChannel(snapshot, now);
		UpdateOverlay(snapshot, now);
	}
}

