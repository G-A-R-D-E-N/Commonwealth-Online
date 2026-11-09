#include "pch.h"

#include "F4TJoinFlow.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TConsoleGuard.h"
#include "F4TJoinState.h"
#include "F4TFlyOver.h"
#include "F4TFlyOverRestore.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionWorldState.h"

#include "CommonwealthOnlineUI_API.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESWorldSpace.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>

namespace F4T::JoinFlow
{
	namespace
	{
		constexpr double kVaultExitFlyOverSeconds = 26.0;
		constexpr double kVaultExitFlyOverRetrySeconds = 10.0;
		constexpr double kVaultExitSceneWaitSeconds = 30.0;
		constexpr std::uint32_t kMq102FormId = 0x0001CC2A;
		constexpr std::uint16_t kMq102PodExitStage = 2;
		constexpr std::uint16_t kMq102ControlsRestoredStage = 10;
		constexpr double kFadeOutSeconds = 0.45;
		constexpr double kFadeInSeconds = 0.35;
		constexpr double kSpawnSettleSeconds = 0.40;
		constexpr double kReturningLoadingTimeoutSeconds = 60.0;
		constexpr double kConnectingTimeoutSeconds = 60.0;

		enum class Fade : std::uint8_t
		{
			None,
			Out,
			In
		};

		JoinState::Machine g_machine{};
		bool              g_active{ false };
		bool              g_vaultExitArmed{ false };
		bool              g_vaultExitPan{ false };
		bool              g_vaultExitControlsDisabled{ false };
		bool              g_vaultExitRetryStarted{ false };
		double            g_vaultExitRetryStartSeconds{ 0.0 };
		bool              g_vaultExitSceneWaitStarted{ false };
		double            g_vaultExitSceneWaitStartSeconds{ 0.0 };
		std::uint32_t     g_vaultExitPathWorld{ 0 };
		bool              g_vaultExitPathAvailable{ false };
		Fade              g_fade{ Fade::None };
		double            g_fadeStartSeconds{ 0.0 };
		double            g_spawnStartSeconds{ 0.0 };
		double            g_loadingStartSeconds{ 0.0 };
		std::atomic<std::uint64_t> g_joinFlowEndEpoch{ 0 };
		std::atomic<std::uint64_t> g_vaultExitDisarmEpoch{ 0 };

		void LogInfo(std::string_view a_message)
		{
			REX::INFO("{} [JoinFlow] {}", F4T::Networking::GetLocalPlayerLogPrefix(), a_message);
		}

		double NowSeconds()
		{
			using clock = std::chrono::steady_clock;
			static const auto epoch = clock::now();
			return std::chrono::duration<double>(clock::now() - epoch).count();
		}

		void ResetVaultExitSceneWait()
		{
			g_vaultExitSceneWaitStarted = false;
			g_vaultExitSceneWaitStartSeconds = 0.0;
		}

		int Mq102Stage()
		{
			const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(kMq102FormId);
			return quest ? static_cast<int>(quest->currentStage) : -1;
		}

		void SetVaultExitControlsDisabled(bool a_disabled, std::string_view a_reason)
		{
			if (!F4T::SessionWorldState::IsPlayerInWorld()) {
				g_vaultExitControlsDisabled = false;
				return;
			}
			if (!a_disabled && !g_vaultExitControlsDisabled) {
				return;
			}
			g_vaultExitControlsDisabled = a_disabled;
			F4T::ConsoleGuard::ExecutePluginCommand(a_disabled ? "DisablePlayerControls" : "EnablePlayerControls");
			LogInfo(std::format(
				"Vault-exit fly-over {} player controls ({}).",
				a_disabled ? "disabled" : "enabled",
				a_reason));
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI14* OverlayApi()
		{
			static auto* api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI14>();
			return api;
		}

		void PushOverlay(const char* a_fade)
		{
			auto* api = OverlayApi();
			if (!api || !api->PushJoinOverlay) {
				return;
			}
			const auto state = g_machine.Current();
			nlohmann::json json{
				{ "state", std::string(JoinState::StateId(state)) },
				{ "visible", JoinState::AllowsOverlay(state) },
			};
			if (a_fade && a_fade[0]) {
				json["fade"] = a_fade;
			}
			const auto text = json.dump();
			api->PushJoinOverlay(text.c_str());
		}

		void HideOverlay()
		{
			auto* api = OverlayApi();
			if (!api || !api->PushJoinOverlay) {
				return;
			}
			const auto text = std::format(
				"{{\"state\":\"{}\",\"visible\":false}}",
				std::string(JoinState::StateId(g_machine.Current())));
			api->PushJoinOverlay(text.c_str());
		}

		bool PlayerWorldLoaded(const RE::PlayerCharacter& a_player)
		{
			if (!a_player.GetParentCell()) {
				return false;
			}
			if (F4T::SessionWorldState::IsLoadingScreenActive() || F4T::SessionWorldState::IsMainMenuOpen()) {
				return false;
			}
			return !F4T::SessionWorldState::IsPlaceholderSpawnPosition(a_player);
		}

		bool WorldIsExterior(const RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			return cell && cell->IsExterior() && cell->worldSpace;
		}

		bool GateSatisfied(const RE::PlayerCharacter& a_player)
		{
			if (!F4T::Networking::IsConnectedToServer()) {
				return false;
			}
			if (!F4T::RemotePlayerState::GetAssignedPlayerId().has_value()) {
				return false;
			}
			if (!PlayerWorldLoaded(a_player)) {
				return false;
			}
			return F4T::SessionLaunch::CanPublishGameplayTransform();
		}

		void EnterSpawning()
		{
			g_spawnStartSeconds = NowSeconds();
			PushOverlay("in");
		}

		void FinishFlow(JoinState::Event a_event, std::string_view a_reason, FlyOver::ExitReason a_reasonKind)
		{
			FlyOver::Restore(a_reasonKind);
			SetVaultExitControlsDisabled(false, a_reason);
			g_machine.Advance(a_event, false);
			HideOverlay();
			g_active = false;
			g_vaultExitDisarmEpoch.fetch_add(1);
			g_vaultExitArmed = false;
			g_vaultExitPan = false;
			g_vaultExitRetryStarted = false;
			g_vaultExitRetryStartSeconds = 0.0;
			g_vaultExitPathWorld = 0;
			g_vaultExitPathAvailable = false;
			ResetVaultExitSceneWait();
			g_fade = Fade::None;
			LogInfo(std::format("Join flow ended ({})", a_reason));
		}

		void DisarmVaultExitFlyOver(std::string_view a_reason)
		{
			g_vaultExitDisarmEpoch.fetch_add(1);
			if (!g_vaultExitArmed) {
				return;
			}
			g_vaultExitArmed = false;
			g_vaultExitRetryStarted = false;
			g_vaultExitRetryStartSeconds = 0.0;
			g_vaultExitPathWorld = 0;
			g_vaultExitPathAvailable = false;
			ResetVaultExitSceneWait();
			LogInfo(std::format("Vault-exit fly-over disarmed ({})", a_reason));
		}

		std::string_view WorldEditorId(const RE::TESWorldSpace* a_world)
		{
			const auto* editorId = a_world ? a_world->GetFormEditorID() : nullptr;
			return editorId ? std::string_view{ editorId } : std::string_view{ "<no editor id>" };
		}

		void NoteVaultExitCameraRefusal(std::string_view a_worldName)
		{
			if (g_vaultExitRetryStarted) {
				return;
			}
			g_vaultExitRetryStarted = true;
			g_vaultExitRetryStartSeconds = NowSeconds();
			LogInfo(std::format(
				"Vault-exit fly-over is waiting for the camera in world '{}'; retrying for {} s.",
				a_worldName,
				static_cast<int>(kVaultExitFlyOverRetrySeconds)));
		}

		bool TryStartVaultExitFlyOver()
		{
			if (g_active) {
				return false;
			}
			const bool connected = F4T::Networking::IsConnectedToServer() && !F4T::SessionSaveGuard::IsSessionExitPending();
			const auto* player = RE::PlayerCharacter::GetSingleton();
			const bool loaded = player && PlayerWorldLoaded(*player);
			const bool exterior = loaded && WorldIsExterior(*player);
			const auto* cell = exterior ? player->GetParentCell() : nullptr;
			const auto* world = cell ? cell->worldSpace : nullptr;
			const std::string_view worldName = WorldEditorId(world);
			const std::uint32_t worldId = world ? world->GetFormID() : 0;
			bool hasPath = false;
			if (exterior) {
				if (worldId != 0 && worldId == g_vaultExitPathWorld) {
					hasPath = g_vaultExitPathAvailable;
				} else {
					hasPath = FlyOver::HasPathForCurrentWorld();
					g_vaultExitPathWorld = worldId;
					g_vaultExitPathAvailable = hasPath;
				}
			}
			bool vanillaSceneHolding = false;
			if (exterior && hasPath) {
				const int stage = Mq102Stage();
				if (stage >= kMq102PodExitStage && stage < kMq102ControlsRestoredStage) {
					if (!g_vaultExitSceneWaitStarted) {
						g_vaultExitSceneWaitStarted = true;
						g_vaultExitSceneWaitStartSeconds = NowSeconds();
						LogInfo(std::format(
							"Vault-exit fly-over is waiting for vanilla controls (MQ102 stage {} of {}, up to {} s).",
							stage,
							static_cast<int>(kMq102ControlsRestoredStage),
							static_cast<int>(kVaultExitSceneWaitSeconds)));
					}
					if (NowSeconds() - g_vaultExitSceneWaitStartSeconds >= kVaultExitSceneWaitSeconds) {
						DisarmVaultExitFlyOver("vanilla controls were not restored before the wait expired");
						ReleaseVaultExitControls("vanilla pod-exit wait expired");
						return false;
					}
					vanillaSceneHolding = true;
				}
			} else {
				ResetVaultExitSceneWait();
			}
			const bool cameraRefused = exterior && hasPath && !vanillaSceneHolding && !FlyOver::CanTakeOverCamera();
			if (!cameraRefused) {
				g_vaultExitRetryStarted = false;
				g_vaultExitRetryStartSeconds = 0.0;
			}
			const bool retryExpired = g_vaultExitRetryStarted &&
			                          NowSeconds() - g_vaultExitRetryStartSeconds >= kVaultExitFlyOverRetrySeconds;
			const auto decision = JoinState::DecideFlyOver(JoinState::FlyOverInput{
				connected,
				loaded,
				exterior,
				hasPath,
				cameraRefused,
				retryExpired,
				vanillaSceneHolding,
			});

			if (decision == JoinState::FlyOverDecision::Wait) {
				if (cameraRefused) {
					NoteVaultExitCameraRefusal(worldName);
				}
				return false;
			}
			if (decision == JoinState::FlyOverDecision::Disarm) {
				if (!connected) {
					DisarmVaultExitFlyOver("session ended");
				} else if (cameraRefused && retryExpired) {
					LogInfo(std::format(
						"Vault-exit fly-over could not take the camera in world '{}' within {} s; no pan will play.",
						worldName,
						static_cast<int>(kVaultExitFlyOverRetrySeconds)));
					DisarmVaultExitFlyOver("camera refused past the retry limit");
				} else if (exterior && !hasPath) {
					LogInfo(std::format(
						"Vault-exit fly-over has no path for world '{}'; no pan will play.",
						worldName));
					DisarmVaultExitFlyOver("no path for the exterior world");
				} else {
					DisarmVaultExitFlyOver("the vault-exit pan cannot start");
				}
				return false;
			}

			if (!FlyOver::Start()) {
				if (!FlyOver::CanTakeOverCamera()) {
					NoteVaultExitCameraRefusal(worldName);
					return false;
				}
				LogInfo(std::format(
					"Vault-exit fly-over could not start in world '{}'; no pan will play.",
					worldName));
				DisarmVaultExitFlyOver("camera takeover failed");
				return false;
			}

			if (g_vaultExitSceneWaitStarted) {
				LogInfo(std::format(
					"Vault-exit fly-over waited {:.1f} s for vanilla controls (MQ102 stage {}).",
					NowSeconds() - g_vaultExitSceneWaitStartSeconds,
					Mq102Stage()));
			}
			ResetVaultExitSceneWait();
			g_vaultExitArmed = false;
			g_vaultExitRetryStarted = false;
			g_vaultExitRetryStartSeconds = 0.0;
			g_vaultExitPathWorld = 0;
			g_vaultExitPathAvailable = false;
			SetVaultExitControlsDisabled(true, "vault-exit pan");
			g_machine = JoinState::Machine{ JoinState::State::FlyOver };
			g_active = true;
			g_vaultExitPan = true;
			g_fade = Fade::None;
			PushOverlay(nullptr);
			LogInfo(std::format("Vault-exit fly-over started in world '{}'.", worldName));
			return true;
		}

		void BeginConnectingOnGameThread()
		{
			if (g_active) {
				return;
			}
			DisarmVaultExitFlyOver("a new join started");
			g_machine = JoinState::Machine{ JoinState::State::Connecting };
			g_active = true;
			g_vaultExitPan = false;
			g_fade = Fade::None;
			g_loadingStartSeconds = NowSeconds();
			PushOverlay(nullptr);
		}

		void NotifyAuthenticatedOnGameThread()
		{
			if (g_machine.Current() == JoinState::State::Connecting) {
				g_machine.Advance(JoinState::Event::Connected, false);
			}
			if (g_machine.Current() == JoinState::State::Authenticating) {
				g_machine.Advance(JoinState::Event::Authenticated, false);
			}
			if (!g_active) {
				return;
			}
			PushOverlay(nullptr);
		}

		void EndSessionJoinOnGameThread(std::string_view a_reason)
		{
			if (!g_active) {
				return;
			}
			FinishFlow(JoinState::Event::Failed, a_reason, FlyOver::ExitReason::Failed);
		}

		void ArmVaultExitFlyOverOnGameThread()
		{
			g_vaultExitArmed = true;
			g_vaultExitPan = false;
			g_vaultExitRetryStarted = false;
			g_vaultExitRetryStartSeconds = 0.0;
			g_vaultExitPathWorld = 0;
			g_vaultExitPathAvailable = false;
			ResetVaultExitSceneWait();
			LogInfo("Vault-exit fly-over armed; waiting for the first exterior world with a path.");
		}
	}

	void BeginConnecting()
	{
		const auto epoch = g_joinFlowEndEpoch.load();
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([epoch]() {
				if (JoinState::ShouldBeginConnecting(epoch, g_joinFlowEndEpoch.load())) {
					BeginConnectingOnGameThread();
				}
			});
			return;
		}
		BeginConnectingOnGameThread();
	}

	void NotifyAuthenticated()
	{
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([]() {
				NotifyAuthenticatedOnGameThread();
			});
			return;
		}
		NotifyAuthenticatedOnGameThread();
	}

	void EndForFirstTimeJoin()
	{
		g_joinFlowEndEpoch.fetch_add(1);
		HideOverlay();
		g_active = false;
		g_fade = Fade::None;
		SetVaultExitControlsDisabled(false, "first-time join released");
		LogInfo("First-time join released the join overlay; the vault-exit fly-over arms after the profile bind.");
	}

	void ReleaseVaultExitControls(std::string_view a_reason, bool a_onlyWhenDisabled)
	{
		if (!F4T::SessionWorldState::IsPlayerInWorld()) {
			g_vaultExitControlsDisabled = false;
			return;
		}
		if (a_onlyWhenDisabled && !g_vaultExitControlsDisabled) {
			return;
		}
		g_vaultExitControlsDisabled = false;
		F4T::ConsoleGuard::ExecutePluginCommand("EnablePlayerControls");
		LogInfo(std::format("Vault-exit fly-over released player controls ({}).", a_reason));
	}

	void ArmVaultExitFlyOver()
	{
		const auto epoch = g_vaultExitDisarmEpoch.load();
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([epoch]() {
				if (JoinState::ShouldArmVaultExit(epoch, g_vaultExitDisarmEpoch.load())) {
					ArmVaultExitFlyOverOnGameThread();
				}
			});
			return;
		}
		ArmVaultExitFlyOverOnGameThread();
	}

	bool IsActive()
	{
		return g_active;
	}

	bool HasLeftVault()
	{
		return Mq102Stage() >= kMq102ControlsRestoredStage;
	}

	bool CanPublishGameplayTransform()
	{
		return !g_active || g_vaultExitPan || JoinState::AllowsGameplayPublish(g_machine.Current());
	}

	bool IsFlyOverRunning()
	{
		return FlyOver::IsRunning();
	}

	std::string_view StateId()
	{
		return JoinState::StateId(g_machine.Current());
	}

	void Tick()
	{
		if (g_vaultExitArmed && TryStartVaultExitFlyOver()) {
			return;
		}
		if (!g_active) {
			return;
		}
		if (g_machine.Current() == JoinState::State::Connecting ||
			g_machine.Current() == JoinState::State::Authenticating) {
			if (F4T::Networking::IsConnectedToServer()) {
				NotifyAuthenticatedOnGameThread();
				return;
			}
			if (NowSeconds() - g_loadingStartSeconds >= kConnectingTimeoutSeconds) {
				FinishFlow(JoinState::Event::Failed, "timed out connecting", FlyOver::ExitReason::Failed);
			}
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		if (g_machine.Current() == JoinState::State::LoadingWorld) {
			if (!F4T::Networking::IsConnectedToServer() && !F4T::CharacterSlotsClient::IsReconnecting()) {
				FinishFlow(JoinState::Event::Cancelled, "connection ended", FlyOver::ExitReason::Disconnect);
				return;
			}
			if (!PlayerWorldLoaded(*player)) {
				if (F4T::CharacterSlotsClient::NeedsSelection() || F4T::CharacterSlotsClient::IsReconnecting()) {
					g_loadingStartSeconds = NowSeconds();
					return;
				}
				if (NowSeconds() - g_loadingStartSeconds >= kReturningLoadingTimeoutSeconds) {
					FinishFlow(JoinState::Event::Failed, "timed out waiting for the world", FlyOver::ExitReason::Failed);
				}
				return;
			}
			g_machine.Advance(JoinState::Event::WorldLoaded, false);
			EnterSpawning();
			return;
		}

		if (g_machine.Current() == JoinState::State::FlyOver) {
			if (!F4T::Networking::IsConnectedToServer()) {
				FinishFlow(JoinState::Event::Cancelled, "connection ended", FlyOver::ExitReason::Disconnect);
				return;
			}
			if (FlyOver::Tick()) {
				SetVaultExitControlsDisabled(false, "fly-over ended");
				g_machine.Advance(JoinState::Event::FlyOverTimeout, false);
				if (!F4T::SessionSaveGuard::IsSessionExitPending()) {
					EnterSpawning();
				}
				return;
			}
			if (g_fade == Fade::None && FlyOver::ElapsedSeconds() >= kVaultExitFlyOverSeconds &&
				GateSatisfied(*player)) {
				FlyOver::RequestExit();
				g_fade = Fade::Out;
				g_fadeStartSeconds = NowSeconds();
				PushOverlay("out");
				return;
			}
			if (g_fade == Fade::Out && NowSeconds() - g_fadeStartSeconds >= kFadeOutSeconds) {
				FlyOver::Restore(FlyOver::ExitReason::Ready);
				SetVaultExitControlsDisabled(false, "fly-over ended");
				g_machine = JoinState::Machine{ JoinState::State::Spawning };
				g_fade = Fade::In;
				g_fadeStartSeconds = NowSeconds();
				EnterSpawning();
			}
			return;
		}

		if (g_machine.Current() == JoinState::State::Spawning &&
			NowSeconds() - g_spawnStartSeconds >= kFadeInSeconds + kSpawnSettleSeconds) {
			g_machine.Advance(JoinState::Event::SpawnSettled, false);
			HideOverlay();
			g_active = false;
			g_vaultExitPan = false;
			g_fade = Fade::None;
			LogInfo("Join flow reached Ready");
		}
	}

	void EndSessionJoin(bool a_success, std::string_view a_reason)
	{
		if (a_success) {
			return;
		}
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([reason = std::string{ a_reason }]() {
				EndSessionJoinOnGameThread(reason);
			});
			return;
		}
		EndSessionJoinOnGameThread(a_reason);
	}

	void OnWorldReset(bool a_newWorld)
	{
		const auto reason = a_newWorld ? FlyOver::ExitReason::NewGame : FlyOver::ExitReason::LoadGame;
		FlyOver::Restore(reason);
		if (g_active) {
			ReleaseVaultExitControls("world reset during a join", true);
		} else {
			SetVaultExitControlsDisabled(false, "world reset");
		}
		const auto state = g_machine.Current();
		if (g_active && (state == JoinState::State::FlyOver || state == JoinState::State::Spawning)) {
			FinishFlow(JoinState::Event::Cancelled, "world reset", reason);
		}
		DisarmVaultExitFlyOver("world reset");
	}

	void Shutdown()
	{
		FlyOver::Shutdown();
		g_active = false;
	}
}
