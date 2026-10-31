#include "pch.h"

#include "F4TJoinFlow.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TJoinState.h"
#include "F4TFlyOver.h"
#include "F4TFlyOverRestore.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionWorldState.h"

#include "CommonwealthOnlineUI_API.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <string>

namespace F4T::JoinFlow
{
	namespace
	{
		constexpr double kMinimumFlyOverSeconds = 3.0;
		constexpr double kFadeOutSeconds = 0.45;
		constexpr double kFadeInSeconds = 0.35;
		constexpr double kSpawnSettleSeconds = 0.40;
		constexpr double kLoadingWorldTimeoutSeconds = 700.0;
		constexpr double kReturningLoadingTimeoutSeconds = 60.0;
		constexpr double kConnectingTimeoutSeconds = 60.0;
		constexpr std::string_view kVaultStartCellEditorId = "Vault111Cryo";

		enum class Fade : std::uint8_t
		{
			None,
			Out,
			In
		};

		JoinState::Machine g_machine{};
		bool              g_active{ false };
		bool              g_firstTime{ false };
		Fade              g_fade{ Fade::None };
		double            g_fadeStartSeconds{ 0.0 };
		double            g_spawnStartSeconds{ 0.0 };
		double            g_loadingStartSeconds{ 0.0 };

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

		bool InVaultStartCell(const RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			const auto* editorId = cell ? cell->GetFormEditorID() : nullptr;
			return editorId && kVaultStartCellEditorId == editorId;
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
			g_machine.Advance(a_event, false);
			HideOverlay();
			g_active = false;
			g_firstTime = false;
			g_fade = Fade::None;
			LogInfo(std::format("Join flow ended ({})", a_reason));
		}

		void BeginConnectingOnGameThread()
		{
			if (g_active) {
				return;
			}
			g_machine = JoinState::Machine{ JoinState::State::Connecting };
			g_active = true;
			g_firstTime = false;
			g_fade = Fade::None;
			g_loadingStartSeconds = NowSeconds();
			PushOverlay(nullptr);
		}

		void NotifyAuthenticatedOnGameThread()
		{
			if (!g_active) {
				BeginConnectingOnGameThread();
			}
			if (g_machine.Current() == JoinState::State::Connecting) {
				g_machine.Advance(JoinState::Event::Connected, false);
			}
			if (g_machine.Current() == JoinState::State::Authenticating) {
				g_machine.Advance(JoinState::Event::Authenticated, false);
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
	}

	void BeginConnecting()
	{
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([]() {
				BeginConnectingOnGameThread();
			});
			return;
		}
		BeginConnectingOnGameThread();
	}

	void NotifyAuthenticated(bool a_firstTime)
	{
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([a_firstTime]() {
				NotifyAuthenticatedOnGameThread();
				if (a_firstTime) {
					g_firstTime = true;
				}
			});
			return;
		}
		NotifyAuthenticatedOnGameThread();
		g_firstTime = g_firstTime || a_firstTime;
	}

	bool IsActive()
	{
		return g_active;
	}

	bool IsReady()
	{
		return g_active && g_machine.Current() == JoinState::State::Ready;
	}

	bool CanPublishGameplayTransform()
	{
		return !g_active || JoinState::AllowsGameplayPublish(g_machine.Current());
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
			if (InVaultStartCell(*player) && !g_firstTime) {
				g_firstTime = true;
				g_loadingStartSeconds = NowSeconds();
				HideOverlay();
			}
			if (!F4T::Networking::IsConnectedToServer() && !F4T::CharacterSlotsClient::IsReconnecting()) {
				FinishFlow(JoinState::Event::Cancelled, "connection ended", FlyOver::ExitReason::Disconnect);
				return;
			}
			const auto ready = g_firstTime ?
			                       (F4T::SessionWorldState::IsGameplayWorldReady(*player) &&
			                        F4T::SessionLaunch::CanApplyServerCorrection()) :
			                       PlayerWorldLoaded(*player);
			if (!ready) {
				if (F4T::CharacterSlotsClient::NeedsSelection() || F4T::CharacterSlotsClient::IsReconnecting()) {
					g_loadingStartSeconds = NowSeconds();
					return;
				}
				const auto budget = g_firstTime ? kLoadingWorldTimeoutSeconds : kReturningLoadingTimeoutSeconds;
				if (NowSeconds() - g_loadingStartSeconds >= budget) {
					FinishFlow(JoinState::Event::Failed, "timed out waiting for the world", FlyOver::ExitReason::Failed);
				}
				return;
			}
			const auto eligible = WorldIsExterior(*player) && FlyOver::HasPathForCurrentWorld();
			g_machine.Advance(JoinState::Event::WorldLoaded, eligible);
			if (g_machine.Current() == JoinState::State::FlyOver) {
				if (FlyOver::Start()) {
					PushOverlay(nullptr);
				} else {
					g_machine = JoinState::Machine{ JoinState::State::Spawning };
					EnterSpawning();
				}
			} else {
				EnterSpawning();
			}
			return;
		}

		if (g_machine.Current() == JoinState::State::FlyOver) {
			if (!F4T::Networking::IsConnectedToServer()) {
				FinishFlow(JoinState::Event::Cancelled, "connection ended", FlyOver::ExitReason::Disconnect);
				return;
			}
			if (FlyOver::Tick()) {
				g_machine.Advance(JoinState::Event::FlyOverTimeout, false);
				EnterSpawning();
				return;
			}
			if (g_fade == Fade::None && FlyOver::ElapsedSeconds() >= kMinimumFlyOverSeconds &&
				GateSatisfied(*player)) {
				FlyOver::RequestExit();
				g_fade = Fade::Out;
				g_fadeStartSeconds = NowSeconds();
				PushOverlay("out");
				return;
			}
			if (g_fade == Fade::Out && NowSeconds() - g_fadeStartSeconds >= kFadeOutSeconds) {
				FlyOver::Restore(FlyOver::ExitReason::Ready);
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
			g_firstTime = false;
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
		const auto state = g_machine.Current();
		if (g_active && (state == JoinState::State::FlyOver || state == JoinState::State::Spawning)) {
			FinishFlow(JoinState::Event::Cancelled, "world reset", reason);
		}
	}

	void Shutdown()
	{
		FlyOver::Shutdown();
		g_active = false;
	}
}
