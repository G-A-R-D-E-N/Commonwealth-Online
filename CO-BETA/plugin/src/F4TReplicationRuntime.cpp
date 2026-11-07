// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TReplicationRuntime.h"
#include "F4THookThreadTable.h"

#include "F4TAdminItems.h"
#include "F4TDebugLogging.h"
#include "F4TContainerChangedHub.h"
#include "F4TDownedGlue.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDifficultyReportClient.h"
#include "F4TCampRegistration.h"
#include "F4TConsoleGuard.h"
#include "F4TUIPrefs.h"
#include "F4TInventorySync.h"
#include "F4TInventoryGrant.h"
#include "F4TDropMirrorRefs.h"
#include "F4TFactionInviteOffer.h"
#include "F4TWorkshopReplicationClient.h"
#include "F4TInteractWheel.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TGnsVoice.h"
#include "F4TObjectSync.h"
#include "F4TNetworking.h"
#include "F4TNameplateHud.h"
#include "F4TNetStatsReporter.h"
#include "F4TNpcSync.h"
#include "F4TExplosionSync.h"
#include "F4TFurnitureSeatSync.h"
#include "F4TPipboyUnpause.h"
#include "F4TPlayerAnimEventSync.h"
#include "F4TProjectileAim.h"
#include "F4TProxyActorController.h"
#include "F4TQuestFormKey.h"
#include "F4TQuestLogic.h"
#include "F4TQuestSync.h"
#include "F4TQuestUI.h"
#include "F4TRemotePlayerState.h"
#include "F4TSaveLineageSession.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionWorldState.h"
#include "F4TSurvivalEnforcer.h"
#include "F4TSurvivalNeedsClient.h"
#include "F4TSleepClock.h"
#include "F4TSleepController.h"
#include "F4TSleepProxy.h"
#include "F4TTradeClient.h"
#include "F4TVendorTradeClient.h"
#include "F4TWorldPolicyEnforcer.h"
#include "F4TWorldStateSync.h"
#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESQuest.h"
#include "RE/U/UI.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace F4T::ReplicationRuntime
{
	namespace
	{
		std::atomic<std::uint64_t> g_connectionEpoch{ 0 };
		Coordinator g_runtime;

		constexpr std::string_view kWelcomeQuestEditorId = "CO_WelcomeQuest";
		constexpr std::uint16_t kCampKitCompleteStage = 20;
		constexpr std::uint16_t kObjectiveStage = 10;
		constexpr std::chrono::seconds kWelcomeQuestRetryInterval{ 1 };
		constexpr std::chrono::seconds kWelcomeQuestBackoff{ 5 };

		void OnContainerChanged(const F4T::ContainerChangedHub::ContainerChange& a_change)
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_change.itemCount <= 0 || a_change.newContainer != player->GetFormID()) {
				return;
			}
			const auto key = F4T::QuestKey::FromFormId(a_change.baseObj, F4T::QuestFormKey::GetLoadOrder());
			if (!key || !F4T::WorkshopReplicationClient::IsCampKitForm({ key->plugin, key->localId })) {
				return;
			}
			auto* quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ kWelcomeQuestEditorId });
			if (!quest || quest->currentStage >= kCampKitCompleteStage) {
				return;
			}
			if (!quest->SetStage(kCampKitCompleteStage)) {
				REX::WARN("Camp kit collected, but CO_WelcomeQuest did not advance to stage {}.", kCampKitCompleteStage);
				return;
			}
			F4T::DebugLogging::Log("CampKit", "camp kit collected: CO_WelcomeQuest set to stage {}", kCampKitCompleteStage);
		}

		bool IsInWorld()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (ui && (ui->GetMenuOpen<RE::LoadingMenu>() || ui->GetMenuOpen<RE::MainMenu>())) {
				return false;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->GetParentCell();
		}

		bool IsSurvivalWorldReady()
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player && F4T::SessionWorldState::IsGameplayWorldReady(*player);
		}

		struct WelcomeQuestState
		{
			RE::TESQuest* quest{};
			bool startIssued{};
			bool failureLogged{};
			std::chrono::steady_clock::time_point nextAttempt{};
		};
		WelcomeQuestState g_welcome;

		void ResetWelcomeQuest()
		{
			g_welcome = WelcomeQuestState{};
		}

		void EnsureWelcomeQuest()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now < g_welcome.nextAttempt || !IsInWorld()) {
				return;
			}
			g_welcome.nextAttempt = now + kWelcomeQuestRetryInterval;
			if (!g_welcome.quest) {
				g_welcome.quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ kWelcomeQuestEditorId });
			}
			auto* quest = g_welcome.quest;
			if (!quest || quest->currentStage >= kCampKitCompleteStage) {
				return;
			}
			if (!g_welcome.startIssued) {
				F4T::ConsoleGuard::ExecutePluginCommand("startquest CO_WelcomeQuest");
				g_welcome.startIssued = true;
				return;
			}
			if (quest->currentStage < kObjectiveStage && !quest->SetStage(kObjectiveStage)) {
				if (!g_welcome.failureLogged) {
					REX::WARN("CO_WelcomeQuest did not reach its objective stage {}.", kObjectiveStage);
					g_welcome.failureLogged = true;
				}
				g_welcome.nextAttempt = now + kWelcomeQuestBackoff;
				return;
			}
		}
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
		F4T::SurvivalNeedsClient::FlushPendingSends();
		F4T::ContainerChangedHub::Install();
		F4T::ContainerChangedHub::Drain(OnContainerChanged);
		EnsureWelcomeQuest();
		F4T::CampRegistration::Tick();
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
			F4T::SleepController::OnDisconnect();
			F4T::DifficultyReportClient::OnDisconnect();
			F4T::QuestSync::Reset();
			F4T::QuestSync::BeginSettle();
			ResetWelcomeQuest();
			F4T::CampRegistration::Reset();
		}

		F4T::Networking::UpdateConnectionMaintenance();
		F4T::NetStatsReporter::Tick();
		F4T::PipboyUnpause::Tick();
		F4T::Downed::Tick();
		F4T::FactionInviteOffer::Tick();
		F4T::InteractWheel::Tick();
		F4T::SettlementCaptureClient::Tick();
		F4T::ExplosionSync::Tick(connected && IsSurvivalWorldReady());
		bool animFlushed = false;
		bool aimFlushed = false;
		bool explosionFlushed = false;
		if (connected) {
			const auto connection = F4T::Networking::GetConnectionGeneration();
			F4T::UIPrefs::SendPrivacyIfChanged(connection);
			const bool loaded = IsSurvivalWorldReady();
			bool attempted = false;
			bool allowed = false;
			F4T::Networking::TryRunForConnectionGeneration(connection, [&] {
				if (CurrentGeneration() != g_runtime.CurrentGeneration()) return;
				attempted = true;
				allowed = F4T::SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), loaded, [&] {
					F4T::FurnitureSeatSync::PublishLocal();
					F4T::PlayerAnimEventSync::FlushOutbound();
					animFlushed = true;
					F4T::ProjectileAim::FlushOutbound();
					aimFlushed = true;
					F4T::ExplosionSync::FlushOutbound();
					explosionFlushed = true;
					F4T::RemotePlayerState::ApplyPendingCombatHits();
					SendPendingCombatHits(g_runtime);
					PollLocalPlayerTransform(g_runtime.Session(), g_runtime.Capture());
					F4T::WorldStateSync::PollHostCapture();
					F4T::WorldStateSync::ApplyPendingState();
					F4T::AdminItems::ApplyPendingAdminItems();
				});
			});
			if (attempted && loaded && !allowed) F4T::SurvivalEnforcer::NotifyUnavailable(connection);
			if (attempted && allowed) F4T::SurvivalEnforcer::ClearNotice();
		} else F4T::SurvivalEnforcer::ClearNotice();
		F4T::DifficultyReportClient::Tick(connected);
		if (!F4T::Networking::IsConnectedToServer()) F4T::WorldStateSync::ApplyPendingState();
		F4T::InventorySync::Tick(static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
		F4T::TradeClient::Tick();
		F4T::VendorTradeClient::Tick();
		F4T::DropMirrorRefs::Tick();
		F4T::WorkshopReplicationClient::Tick();
		const auto nowMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		const auto runGameplay = [&](const std::function<void()>& a_work) {
			if (!connected) return;
			const auto connection = F4T::Networking::GetConnectionGeneration();
			F4T::Networking::TryRunForConnectionGeneration(connection, [&] {
				if (CurrentGeneration() != g_runtime.CurrentGeneration()) return;
				F4T::SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), IsSurvivalWorldReady(), a_work);
			});
		};
		F4T::ObjectSync::Tick(nowMs, runGameplay);
		char hookReport[1024]{};
		if (F4T::HookThread::TakeReport(nowMs, F4T::HookThread::kReportRepeatIntervalMs, hookReport, sizeof(hookReport))) {
			F4T::DebugLogging::Log("HookThread", "{}", hookReport);
		}
		if (connected) {
			F4T::GnsVoice::PumpControl(static_cast<double>(nowMs) / 1000.0);
		}
		F4T::WorldPolicyEnforcer::Tick(nowMs, g_runtime.CurrentGeneration());
		F4T::NpcSync::Update(runGameplay);
		F4T::SleepProxy::Tick();
		F4T::ProxyActorController::Update();
		F4T::NameplateHud::Tick();
		F4T::PlayerAnimEventSync::Tick(animFlushed);
		F4T::FurnitureSeatSync::Tick();
		if (!aimFlushed) F4T::ProjectileAim::DiscardOutbound();
		if (!explosionFlushed) F4T::ExplosionSync::DiscardOutbound();
		F4T::SleepClock::Tick();
		F4T::SleepController::Tick();
		F4T::ConsoleGuard::Tick();
		F4T::SessionLaunch::Update();
		F4T::SaveLineageSession::TickPendingRecord();
		F4T::DeathBagGrantLedger::TickPendingSave();
		F4T::SaveLineageSession::TickFirstJoinRecovery();
		F4T::InventoryGrant::Tick();
		F4T::SessionSaveGuard::Tick();
		F4T::QuestSync::Tick();
		F4T::QuestUI::Tick();
	}
}
