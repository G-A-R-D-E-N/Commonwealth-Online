#include "pch.h"

#include "CommonwealthOnlineUI_API.h"
#include "DownedUiCallbacks.h"
#include "F4TAlertSystem.h"
#include "F4TCaptureHud.h"
#include "F4TChatView.h"
#include "F4TFactionView.h"
#include "F4TInteractWheelView.h"
#include "F4TCharacterSelectView.h"
#include "F4TJoinOverlayBridge.h"
#include "F4TPrismaUI.h"
#include "F4TMultiplayerProfile.h"
#include "F4TQuestView.h"
#include "F4TRecentServers.h"
#include "F4TServerBrowserBridge.h"
#include "F4TServerLive.h"
#include "F4TSteamActions.h"
#include "F4TVoiceBridge.h"
#include "F4TWorldMode.h"
#include "SessionCallbacks.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

namespace
{
	struct RetryContext
	{
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn onComplete{ nullptr };
		void* userData{ nullptr };
	};

	void CopyError(char* a_errorBuf, std::size_t a_errorBufSize, const std::string& a_error)
	{
		if (!a_errorBuf || a_errorBufSize == 0) {
			return;
		}
		const auto copyLen = (std::min)(a_error.size(), a_errorBufSize - 1);
		std::memcpy(a_errorBuf, a_error.data(), copyLen);
		a_errorBuf[copyLen] = '\0';
	}

	void Api_OnGameDataReady()
	{
		F4T::PrismaUI::OnGameDataReady();
	}

	void Api_ShowMainMenu()
	{
		F4T::PrismaUI::ShowMainMenu();
	}

	void Api_HideMainMenu()
	{
		F4T::PrismaUI::HideMainMenu();
	}

	bool Api_IsMainMenuVisible()
	{
		return F4T::PrismaUI::IsMainMenuVisible();
	}

	void Api_ShowServerBrowser()
	{
		F4T::PrismaUI::ShowServerBrowser();
	}

	void Api_HideServerBrowser()
	{
		F4T::PrismaUI::HideServerBrowser();
	}

	bool Api_IsBrowserVisible()
	{
		return F4T::PrismaUI::IsBrowserVisible();
	}

	void Api_DispatchMainMenuControllerInput(const char* a_button, const char* a_phase)
	{
		F4T::PrismaUI::DispatchMainMenuControllerInput(a_button, a_phase);
	}

	bool Api_IsCustomMenuInputCaptured()
	{
		return F4T::PrismaUI::IsCustomMenuInputCaptured();
	}

	void Api_SetSessionCallbacks(const COMMONWEALTH_ONLINE_UI_API::SessionCallbacks* a_callbacks)
	{
		COUI::Session::SetCallbacks(a_callbacks);
	}

	void Api_SetWorldMode(const char* a_mode)
	{
		COUI::WorldMode::Set(a_mode ? a_mode : "");
	}

	void Api_PushConnectionStatus(const char* a_status)
	{
		if (a_status && a_status[0]) {
			(void)a_status;
		}
		F4T::ServerBrowser::ServerBrowserBridge::PushConnectionStatus();
	}

	bool Api_ShowAlert(
		const COMMONWEALTH_ONLINE_UI_API::AlertRequest* a_request,
		COMMONWEALTH_ONLINE_UI_API::AlertResponseFn     a_onResponse,
		void*                                           a_userData,
		char*                                           a_idOut,
		std::size_t                                     a_idOutSize)
	{
		if (!a_request) {
			return false;
		}

		F4T::Alerts::AlertRequest request{};
		request.id = a_request->id;
		request.category = a_request->category;
		request.title = a_request->title;
		request.message = a_request->message;
		request.tone = a_request->tone;
		request.durationMs = a_request->durationMs;
		request.actions = reinterpret_cast<const F4T::Alerts::AlertAction*>(a_request->actions);
		request.actionCount = a_request->actionCount;

		return F4T::Alerts::Show(
			request,
			a_onResponse,
			a_userData,
			a_idOut,
			a_idOutSize);
	}

	void Api_DismissAlert(const char* a_alertId)
	{
		F4T::Alerts::Dismiss(a_alertId);
	}

	void Api_DismissAllAlerts()
	{
		F4T::Alerts::DismissAll();
	}

	void Api_SetRemotePlayersMapJsonCallback(COMMONWEALTH_ONLINE_UI_API::RemotePlayersMapJsonFn a_callback)
	{
		COUI::Session::SetRemotePlayersMapJsonCallback(a_callback);
	}

	void Api_ShowSessionLockout(const char* a_code, const char* a_reason)
	{
		F4T::PrismaUI::ShowSessionLockout(a_code, a_reason);
	}

	bool Api_HasLastJoin()
	{
		return COUI::Session::HasLastJoin();
	}

	void Api_RetryComplete(bool a_success, const char* a_error, void* a_userData)
	{
		std::unique_ptr<RetryContext> context(static_cast<RetryContext*>(a_userData));
		if (a_success) {
			if (const auto last = COUI::Session::GetLastJoin()) {
				F4T::ServerBrowser::ServerEntry entry{};
				const auto& recent = F4T::ServerBrowser::RecentServersStore::Get();
				const auto found = std::find_if(recent.begin(), recent.end(), [&](const auto& candidate) {
					if (!last->id.empty() && candidate.id == last->id) {
						return true;
					}
					return candidate.host == last->host && candidate.port == last->port;
				});
				if (found != recent.end()) {
					entry = *found;
				}
				if (!last->id.empty()) {
					entry.id = last->id;
				}
				if (!last->name.empty()) {
					entry.name = last->name;
				}
				entry.host = last->host;
				entry.port = last->port;
				entry.passworded = last->passworded;
				F4T::ServerBrowser::RecentServersStore::Add(entry);
				F4T::ServerBrowser::ServerBrowserBridge::SetCurrentServer(entry);
			}
			F4T::PrismaUI::HideServerBrowser();
		}
		F4T::ServerBrowser::ServerBrowserBridge::PushConnectionStatus();
		if (context && context->onComplete) {
			context->onComplete(a_success, a_error ? a_error : "", context->userData);
		}
	}

	bool Api_RetryLastJoin(
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn a_onComplete,
		void*                                      a_userData,
		char*                                      a_errorBuf,
		std::size_t                                a_errorBufSize)
	{
		auto context = std::make_unique<RetryContext>();
		context->onComplete = a_onComplete;
		context->userData = a_userData;
		std::string error;
		auto* rawContext = context.release();
		if (!COUI::Session::RetryLastJoin(&Api_RetryComplete, rawContext, error)) {
			std::unique_ptr<RetryContext> cleanup(rawContext);
			CopyError(a_errorBuf, a_errorBufSize, error.empty() ? "Retry failed." : error);
			return false;
		}
		return true;
	}

	void Api_SetFactionCallbacks(const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks* a_callbacks)
	{
		F4T::FactionView::SetCallbacks(a_callbacks);
		F4T::CharacterSelectView::SetCallbacks(a_callbacks);
	}

	void Api_NotifyFactionStateChanged()
	{
		F4T::FactionView::NotifyStateChanged();
		F4T::CharacterSelectView::NotifyStateChanged();
	}

	void Api_OpenFactionWindow()
	{
		F4T::FactionView::Open("PublicAPI");
	}

	void Api_CloseFactionWindow()
	{
		F4T::FactionView::QueueClose("PublicAPI");
	}

	bool Api_IsFactionWindowVisible()
	{
		return F4T::FactionView::IsVisible();
	}

	void Api_SetChatCallbacks(const COMMONWEALTH_ONLINE_UI_API::ChatCallbacks* a_callbacks)
	{
		F4T::ChatView::SetCallbacks(a_callbacks);
	}

	void Api_PushChatEvent(const char* a_eventJson)
	{
		F4T::ChatView::PushEvent(a_eventJson);
	}

	void Api_SetPasswordJoinCallback(COMMONWEALTH_ONLINE_UI_API::PasswordJoinFn a_callback)
	{
		COUI::Session::SetPasswordJoinCallback(a_callback);
	}

	void Api_SetRespawnChosenCallback(F4T::DownedUi::RespawnChosenFn a_callback)
	{
		F4T::DownedUi::SetRespawnChosenCallback(a_callback);
	}

	void Api_PushDownedState(const char* a_stateJson)
	{
		F4T::DownedUi::PushStateJson(a_stateJson);
	}

	void Api_PushJoinOverlay(const char* a_stateJson)
	{
		F4T::JoinOverlay::PushStateJson(a_stateJson);
	}

	void Api_PushCaptureHud(const char* a_stateJson)
	{
		F4T::CaptureHud::PushStateJson(a_stateJson);
	}

	void Api_SetSteamCallbacks(const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks* a_callbacks)
	{
		F4T::Steam::SetCallbacks(a_callbacks);
	}

	void Api_PushSteamEvent(const char* a_eventJson)
	{
		F4T::Steam::PushEvent(a_eventJson);
	}

	void Api_SetServerLiveCallbacks(const COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks* a_callbacks)
	{
		F4T::ServerLive::SetCallbacks(a_callbacks);
	}

	void Api_PushSessionState(const char* a_stateJson)
	{
		F4T::ServerBrowser::ServerBrowserBridge::PushSessionState(a_stateJson);
	}

	void Api_SetVoiceCallbacks(const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks* a_callbacks)
	{
		F4T::VoiceBridge::SetCallbacks(a_callbacks);
	}

	void Api_PushVoiceEvent(const char* a_eventJson)
	{
		F4T::VoiceBridge::PushEvent(a_eventJson);
	}

	void Api_SetQuestCallbacks(const COMMONWEALTH_ONLINE_UI_API::QuestCallbacks* a_callbacks)
	{
		F4T::QuestView::SetCallbacks(a_callbacks);
	}

	void Api_NotifyQuestStateChanged()
	{
		F4T::QuestView::NotifyStateChanged();
	}

	void Api_OpenQuestWindow()
	{
		F4T::QuestView::Open("PublicAPI");
	}

	void Api_CloseQuestWindow()
	{
		F4T::QuestView::QueueClose("PublicAPI");
	}

	bool Api_IsQuestWindowVisible()
	{
		return F4T::QuestView::IsVisible();
	}

	void Api_SetInteractWheelCallbacks(const COMMONWEALTH_ONLINE_UI_API::InteractWheelCallbacks* a_callbacks)
	{
		F4T::InteractWheelView::SetCallbacks(a_callbacks);
	}

	void Api_OpenInteractWheel()
	{
		F4T::InteractWheelView::Open("PublicAPI");
	}

	void Api_CloseInteractWheel()
	{
		F4T::InteractWheelView::QueueClose("PublicAPI");
	}

	void Api_NotifyInteractWheelStateChanged()
	{
		F4T::InteractWheelView::NotifyStateChanged();
	}

	bool Api_IsInteractWheelVisible()
	{
		return F4T::InteractWheelView::IsVisible();
	}

	bool Api_IsChatVisible()
	{
		return F4T::ChatView::IsOpen();
	}

	bool Api_IsCharacterSelectVisible()
	{
		return F4T::CharacterSelectView::IsVisible();
	}

	void Api_NotifySessionExitSave(const char* a_state, const char* a_message)
	{
		F4T::PrismaUI::NotifySessionExitSave(a_state, a_message);
	}

	void Api_PushJoinProgress(const char* a_message)
	{
		F4T::ServerBrowser::ServerBrowserBridge::PushJoinProgress(a_message);
	}

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1 g_interfaceV1{
		Api_OnGameDataReady,
		Api_ShowMainMenu,
		Api_HideMainMenu,
		Api_IsMainMenuVisible,
		Api_ShowServerBrowser,
		Api_HideServerBrowser,
		Api_IsBrowserVisible,
		Api_DispatchMainMenuControllerInput,
		Api_IsCustomMenuInputCaptured,
		Api_SetSessionCallbacks,
		Api_PushConnectionStatus,
		Api_ShowAlert,
		Api_DismissAlert,
		Api_DismissAllAlerts,
		Api_SetRemotePlayersMapJsonCallback,
		Api_ShowSessionLockout
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI2 g_interfaceV2{
		Api_HasLastJoin,
		Api_RetryLastJoin
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI3 g_interfaceV3{
		Api_SetFactionCallbacks,
		Api_NotifyFactionStateChanged,
		Api_OpenFactionWindow,
		Api_CloseFactionWindow,
		Api_IsFactionWindowVisible,
		Api_SetChatCallbacks,
		Api_PushChatEvent,
		Api_SetPasswordJoinCallback
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI4 g_interfaceV4{
		Api_SetRespawnChosenCallback,
		Api_PushDownedState
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI5 g_interfaceV5{
		Api_SetWorldMode
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI6 g_interfaceV6{
		Api_PushSessionState
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI7 g_interfaceV7{
		Api_SetSteamCallbacks,
		Api_PushSteamEvent
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI8 g_interfaceV8{ F4T::MultiplayerProfile::CopyDisplayName };

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI9 g_interfaceV9{
		Api_SetVoiceCallbacks,
		Api_PushVoiceEvent
	};
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI10 g_interfaceV10{ Api_NotifySessionExitSave };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI11 g_interfaceV11{
		Api_SetQuestCallbacks,
		Api_NotifyQuestStateChanged,
		Api_OpenQuestWindow,
		Api_CloseQuestWindow,
		Api_IsQuestWindowVisible
	};
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI12 g_interfaceV12{ Api_SetServerLiveCallbacks };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI13 g_interfaceV13{ Api_PushJoinProgress };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI14 g_interfaceV14{ Api_PushJoinOverlay };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI15 g_interfaceV15{ Api_PushCaptureHud };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16 g_interfaceV16{
		Api_SetInteractWheelCallbacks,
		Api_OpenInteractWheel,
		Api_CloseInteractWheel,
		Api_NotifyInteractWheelStateChanged,
		Api_IsInteractWheelVisible,
		Api_IsChatVisible,
		Api_IsCharacterSelectVisible
	};

	template <typename F>
	void* VisitInterface(COMMONWEALTH_ONLINE_UI_API::InterfaceVersion a_interfaceVersion, const char* a_export, F&& a_visit)
	{
		using COMMONWEALTH_ONLINE_UI_API::InterfaceVersion;

		switch (a_interfaceVersion) {
		case InterfaceVersion::V1:
			return a_visit(g_interfaceV1);
		case InterfaceVersion::V2:
			return a_visit(g_interfaceV2);
		case InterfaceVersion::V3:
			return a_visit(g_interfaceV3);
		case InterfaceVersion::V4:
			return a_visit(g_interfaceV4);
		case InterfaceVersion::V5:
			return a_visit(g_interfaceV5);
		case InterfaceVersion::V6:
			return a_visit(g_interfaceV6);
		case InterfaceVersion::V7:
			return a_visit(g_interfaceV7);
		case InterfaceVersion::V8:
			return a_visit(g_interfaceV8);
		case InterfaceVersion::V9:
			return a_visit(g_interfaceV9);
		case InterfaceVersion::V10:
			return a_visit(g_interfaceV10);
		case InterfaceVersion::V11:
			return a_visit(g_interfaceV11);
		case InterfaceVersion::V12:
			return a_visit(g_interfaceV12);
		case InterfaceVersion::V13:
			return a_visit(g_interfaceV13);
		case InterfaceVersion::V14:
			return a_visit(g_interfaceV14);
		case InterfaceVersion::V15:
			return a_visit(g_interfaceV15);
		case InterfaceVersion::V16:
			return a_visit(g_interfaceV16);
		default:
			REX::WARN("{} {}: unsupported interface version {}",
				COUI::Session::GetLogPrefix(),
				a_export,
				static_cast<unsigned>(a_interfaceVersion));
			return nullptr;
		}
	}
}

extern "C" __declspec(dllexport) void* F4SEAPI RequestPluginAPI(
	COMMONWEALTH_ONLINE_UI_API::InterfaceVersion a_interfaceVersion)
{
	return VisitInterface(a_interfaceVersion, "RequestPluginAPI", [](auto& a_table) -> void* { return &a_table; });
}

extern "C" __declspec(dllexport) void* F4SEAPI RequestPluginAPIChecked(
	COMMONWEALTH_ONLINE_UI_API::InterfaceVersion a_interfaceVersion,
	std::uint32_t a_layout)
{
	return VisitInterface(a_interfaceVersion, "RequestPluginAPIChecked", [&](auto& a_table) -> void* {
		using Map = COMMONWEALTH_ONLINE_UI_API::InterfaceVersionMap<std::remove_cvref_t<decltype(a_table)>>;
		if (auto* api = COMMONWEALTH_ONLINE_UI_API::AcceptInterface(&a_table, a_layout)) {
			return api;
		}
		REX::CRITICAL(
			"{} Refused interface V{}: CommonwealthOnline.dll expects layout {:#010x}, this CommonwealthOnlineUI.dll has {:#010x}. "
			"The two DLLs are from different builds; install both from the same release.",
			COUI::Session::GetLogPrefix(),
			static_cast<unsigned>(Map::version),
			a_layout,
			Map::layout);
		return nullptr;
	});
}
