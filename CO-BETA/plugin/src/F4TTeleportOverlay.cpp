#include "pch.h"

#include "F4TTeleportOverlay.h"

#include "F4TDebugLogging.h"
#include "F4TJoinFlow.h"
#include "F4TSessionWorldState.h"
#include "F4TTeleportOverlayState.h"

#include "CommonwealthOnlineUI_API.h"

#include "RE/B/BSScriptUtil.h"
#include "RE/G/GameScript.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UIMessageQueue.h"

#include <chrono>
#include <string_view>

namespace F4T::TeleportOverlay
{
	namespace
	{
		constexpr std::string_view kLogComponent{ "TeleportOverlay" };
		constexpr const char* kShowJson{ R"({"state":"LoadingWorld","status":"Travelling...","visible":true})" };
		constexpr const char* kHideJson{ R"({"state":"Ready","visible":false})" };

		State g_state{};
		FaderWatch g_faderWatch{};

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

		void PushOverlay(const char* a_json)
		{
			auto* api = OverlayApi();
			if (!api || !api->PushJoinOverlay) {
				return;
			}
			api->PushJoinOverlay(a_json);
		}

		void SendFaderHide()
		{
			auto* queue = RE::UIMessageQueue::GetSingleton();
			if (!queue) {
				return;
			}
			queue->AddMessage(RE::BSFixedString{ "FaderMenu" }, RE::UI_MESSAGE_TYPE::kHide);
		}

		void ReleaseFader()
		{
			ArmFaderWatch(g_faderWatch, NowSeconds());
			SendFaderHide();
		}

		void FadeInGame()
		{
			const auto game = RE::GameVM::GetSingleton();
			const auto vm = game ? game->GetVM() : nullptr;
			if (!vm) {
				return;
			}
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
			if (!vm->DispatchStaticCall("Game", "FadeOutGame", callback, false, true, 0.0F, 0.5F, false)) {
				F4T::DebugLogging::Log(kLogComponent, "Game.FadeOutGame dispatch was refused");
			}
		}

		void RunFaderWatch()
		{
			if (!g_faderWatch.armed) {
				return;
			}
			const auto action = WatchFader(
				g_faderWatch,
				NowSeconds(),
				F4T::SessionWorldState::IsMenuOpen("FaderMenu"),
				F4T::SessionWorldState::IsMenuOpen("LoadingMenu"),
				F4T::SessionWorldState::IsMenuOpen("PauseMenu"));
			switch (action) {
			case FaderAction::RetryHide:
				F4T::DebugLogging::Log(kLogComponent, "FaderMenu still open after release; retrying kHide");
				SendFaderHide();
				break;
			case FaderAction::RetryFadeIn:
				F4T::DebugLogging::Log(kLogComponent, "FaderMenu still open after kHide retry; calling Game.FadeOutGame(false, true, 0.0, 0.5)");
				FadeInGame();
				break;
			case FaderAction::GiveUp:
				F4T::DebugLogging::Log(kLogComponent, "FaderMenu still open after both retries; giving up");
				break;
			case FaderAction::None:
				break;
			}
		}

		bool PlayerLoaded()
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->Get3D() && player->GetParentCell();
		}

		void OnUiTeleportBegan()
		{
			Begin();
		}
	}

	void Begin()
	{
		if (F4T::JoinFlow::IsActive()) {
			return;
		}
		if (Begin(g_state, NowSeconds()) == Action::Show) {
			g_faderWatch = FaderWatch{};
			PushOverlay(kShowJson);
		}
		F4T::DebugLogging::Log(
			kLogComponent,
			"teleport began LoadingMenu={} FaderMenu={} playerLoaded={}",
			F4T::SessionWorldState::IsMenuOpen("LoadingMenu"),
			F4T::SessionWorldState::IsMenuOpen("FaderMenu"),
			PlayerLoaded());
	}

	void Tick()
	{
		if (F4T::JoinFlow::IsActive()) {
			if (Cancel(g_state) == Action::Hide) {
				SendFaderHide();
			}
			return;
		}
		if (!g_state.active) {
			RunFaderWatch();
			return;
		}

		const auto loadingMenuOpen = F4T::SessionWorldState::IsMenuOpen("LoadingMenu");
		const auto faderMenuOpen = F4T::SessionWorldState::IsMenuOpen("FaderMenu");
		const auto playerLoaded = PlayerLoaded();
		const auto action = Tick(g_state, NowSeconds(), loadingMenuOpen, playerLoaded);
		F4T::DebugLogging::Log(
			kLogComponent,
			"teleport tick LoadingMenu={} FaderMenu={} playerLoaded={} action={}",
			loadingMenuOpen,
			faderMenuOpen,
			playerLoaded,
			static_cast<int>(action));
		if (action == Action::Hide) {
			PushOverlay(kHideJson);
			ReleaseFader();
		}
	}

	void AttachUi()
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI28>();
		if (!ui || !ui->SetTeleportBeganCallback) {
			F4T::DebugLogging::Log(kLogComponent, "the V28 teleport API is unavailable; pause-menu travel shows no overlay");
			return;
		}
		ui->SetTeleportBeganCallback(OnUiTeleportBegan);
	}
}
