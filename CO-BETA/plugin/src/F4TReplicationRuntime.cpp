// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TReplicationRuntime.h"
#include "F4THookThreadTable.h"

#include "F4TDebugLogging.h"
#include "F4TContainerChangedHub.h"
#include "F4TDownedGlue.h"
#include "F4TConsoleGuard.h"
#include "F4TCrashReport.h"
#include "F4TInventorySync.h"
#include "F4TDropMirrorRefs.h"
#include "F4TFactionInviteOffer.h"
#include "F4TGnsVoice.h"
#include "F4TObjectSync.h"
#include "F4TNetworking.h"
#include "F4TNpcSync.h"
#include "F4TPlayerAnimEventSync.h"
#include "F4TProjectileAim.h"
#include "F4TProxyActorController.h"
#include "F4TQuestSync.h"
#include "F4TQuestUI.h"
#include "F4TRemotePlayerState.h"
#include "F4TSaveLineageSession.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionSaveGuard.h"
#include "F4TWorldPolicyEnforcer.h"
#include "F4TWorldStateSync.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace F4T::ReplicationRuntime
{
	namespace
	{
		std::atomic<std::uint64_t> g_connectionEpoch{ 0 };
		Coordinator g_runtime;
	}

	void NoteConnectionEstablished() noexcept
	{
		g_connectionEpoch.fetch_add(1, std::memory_order_acq_rel);
	}

	Generation CurrentGeneration() noexcept
	{
		return g_runtime.GenerationFor(g_connectionEpoch.load(std::memory_order_acquire));
	}

	void Tick()
	{
		F4T::CrashReport::Tick();
		F4T::ContainerChangedHub::Install();
		F4T::ContainerChangedHub::Drain();
		const auto previousGeneration = g_runtime.CurrentGeneration();
		const bool connected =
			F4T::Networking::IsConnectedToServer() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value();
		g_runtime.SyncSession(g_connectionEpoch.load(std::memory_order_acquire), connected);
		if (g_runtime.CurrentGeneration() != previousGeneration) {
			F4T::DebugLogging::Log(
				"Replication",
				"session generation {} -> {}",
				previousGeneration,
				g_runtime.CurrentGeneration());
			F4T::QuestSync::Reset();
			F4T::QuestSync::BeginSettle();
		}

		F4T::Networking::UpdateConnectionMaintenance();
		F4T::Downed::Tick();
		F4T::FactionInviteOffer::Tick();
		F4T::RemotePlayerState::ApplyPendingCombatHits();
		SendPendingCombatHits(g_runtime);
		PollLocalPlayerTransform(g_runtime.Session(), g_runtime.Capture());
		F4T::WorldStateSync::PollHostCapture();
		F4T::WorldStateSync::ApplyPendingState();
		F4T::InventorySync::Tick(static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
		F4T::DropMirrorRefs::Tick();
		const auto nowMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		F4T::ObjectSync::Tick(nowMs);
		char hookReport[1024]{};
		if (F4T::HookThread::TakeReport(nowMs, F4T::HookThread::kReportRepeatIntervalMs, hookReport, sizeof(hookReport))) {
			F4T::DebugLogging::Log("HookThread", "{}", hookReport);
		}
		if (connected) {
			F4T::GnsVoice::PumpControl(static_cast<double>(nowMs) / 1000.0);
		}
		F4T::WorldPolicyEnforcer::Tick(nowMs, g_runtime.CurrentGeneration());
		F4T::NpcSync::Update();
		F4T::ProxyActorController::Update();
		F4T::PlayerAnimEventSync::Tick();
		F4T::ProjectileAim::Tick();
		F4T::ConsoleGuard::Tick();
		F4T::SessionLaunch::Update();
		F4T::SaveLineageSession::TickPendingRecord();
		F4T::SessionSaveGuard::Tick();
		F4T::QuestSync::Tick();
		F4T::QuestUI::Tick();
	}
}
