#include "pch.h"

#include "CommonwealthOnlineUI_API.h"
#include "DownedUiCallbacks.h"
#include "F4TAlertSystem.h"
#include "F4TCaptureHud.h"
#include "F4TInteractPromptHud.h"
#include "F4TStructureHud.h"
#include "F4TChatView.h"
#include "F4TFactionView.h"
#include "F4TAdminView.h"
#include "F4TLocalAdminState.h"
#include "F4TInfoViews.h"
#include "F4TInteractWheelView.h"
#include "F4TCharacterSelectView.h"
#include "F4TJoinOverlayBridge.h"
#include "F4TNeedsHud.h"
#include "F4TNameplateView.h"
#include "F4TParkedSuitView.h"
#include "F4TPauseMenuMap.h"
#include "F4TPrismaUI.h"
#include "F4TMultiplayerProfile.h"
#include "F4TNetStats.h"
#include "F4TQuestView.h"
#include "F4TRecentServers.h"
#include "F4TTradeView.h"
#include "F4TResourceViewHost.h"
#include "F4TServerBrowserBridge.h"
#include "F4TUiServerLive.h"
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

	bool Api_OpenWorldFactionWindow()
	{
		return F4T::FactionView::OpenWorld("WorldOpen");
	}

	bool Api_GetMultiplayerPreferences(COMMONWEALTH_ONLINE_UI_API::MultiplayerPreferences* a_out)
	{
		if (!a_out || a_out->size != sizeof(COMMONWEALTH_ONLINE_UI_API::MultiplayerPreferences)) {
			return false;
		}
		const auto prefs = F4T::MultiplayerProfile::GetPreferences();
		a_out->onlineStatus = prefs.onlineStatus;
		a_out->allowFriendJoin = prefs.allowFriendJoin;
		a_out->mapMarkerSize = prefs.mapMarkerSize;
		a_out->friendRequests = prefs.friendRequests;
		a_out->regionPreference = prefs.regionPreference;
		a_out->interpolationQuality = prefs.interpolationQuality;
		a_out->showLocationToFriends = prefs.showLocationToFriends;
		a_out->showPlayerMarkers = prefs.showPlayerMarkers;
		a_out->showFriendMarkers = prefs.showFriendMarkers;
		a_out->showNameplates = prefs.showNameplates;
		a_out->networkSmoothing = prefs.networkSmoothing;
		a_out->showPing = prefs.showPing;
		a_out->networkStatsOverlay = prefs.networkStatsOverlay;
		return true;
	}

	void Api_SetAdminCallbacks(const COMMONWEALTH_ONLINE_UI_API::AdminCallbacks* a_callbacks)
	{
		F4T::AdminView::SetCallbacks(a_callbacks);
	}

	void Api_NotifyAdminStateChanged()
	{
		F4T::AdminView::NotifyStateChanged();
	}

	void Api_OpenAdminWindow()
	{
		F4T::AdminView::Open("PublicAPI");
	}

	void Api_CloseAdminWindow()
	{
		F4T::AdminView::QueueClose("PublicAPI");
	}

	bool Api_IsAdminWindowVisible()
	{
		return F4T::AdminView::IsVisible();
	}

	void Api_SetAdminAvailable(bool a_available)
	{
		F4T::LocalAdmin::Set(a_available);
		F4T::AdminView::SetAvailable(a_available);
	}

	void Api_HandlePauseMenuMapKey(bool a_allowed)
	{
		F4T::PrismaUI::HandlePauseMenuMapKey(a_allowed);
	}

	void Api_SetReportCallbacks(const COMMONWEALTH_ONLINE_UI_API::ReportCallbacks* a_callbacks)
	{
		F4T::InfoViews::SetReportCallbacks(a_callbacks);
	}

	void Api_PushReportEvent(const char* a_eventJson)
	{
		F4T::InfoViews::PushReportEvent(a_eventJson);
	}

	std::uint64_t Api_GetViewHandle(const char* a_view)
	{
		return F4T::PrismaUI::GetViewHandle(a_view ? a_view : "");
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

	void Api_PushStructureHud(const char* a_stateJson)
	{
		F4T::StructureHud::PushStateJson(a_stateJson);
	}

	void Api_PushNeedsState(const char* a_stateJson)
	{
		F4T::NeedsHud::PushStateJson(a_stateJson);
	}

	void Api_PushNameplates(const char* a_json)
	{
		F4T::NameplateView::PushStateJson(a_json);
	}

	void Api_PushInteractPrompt(const char* a_json)
	{
		F4T::InteractPromptHud::PushStateJson(a_json);
	}

	void Api_SetTeleportBeganCallback(COMMONWEALTH_ONLINE_UI_API::TeleportBeganFn a_callback)
	{
		F4T::PauseMenuMap::SetTeleportBeganCallback(a_callback);
	}

	void Api_PushNetStats(const char* a_json)
	{
		F4T::NetStats::SetLatestJson(a_json ? a_json : "");
	}

	void Api_PushStateBag(const char* a_stateJson)
	{
		F4T::ResourceViewHost::PushStateBag(a_stateJson);
	}

	bool Api_OpenResourceView(const char* a_resource, const char* a_view)
	{
		return F4T::ResourceViewHost::OpenResourceView(a_resource, a_view);
	}

	void Api_CloseResourceView(const char* a_resource)
	{
		F4T::ResourceViewHost::CloseResourceView(a_resource);
	}

	void Api_SetResourceMessageCallback(COMMONWEALTH_ONLINE_UI_API::ResourceMessageFn a_callback)
	{
		F4T::ResourceViewHost::SetResourceMessageCallback(a_callback);
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
		F4T::UiServerLive::SetCallbacks(a_callbacks);
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

	void Api_SetParkedSuitCallbacks(const COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks* a_callbacks)
	{
		F4T::ParkedSuitView::SetCallbacks(a_callbacks);
	}

	void Api_OpenParkedSuitMenu()
	{
		F4T::ParkedSuitView::Open("PublicAPI");
	}

	void Api_CloseParkedSuitMenu()
	{
		F4T::ParkedSuitView::QueueClose("PublicAPI");
	}

	void Api_NotifyParkedSuitStateChanged()
	{
		F4T::ParkedSuitView::NotifyStateChanged();
	}

	bool Api_IsParkedSuitMenuVisible()
	{
		return F4T::ParkedSuitView::IsVisible();
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

	void Api_SetTradeActionCallback(COMMONWEALTH_ONLINE_UI_API::TradeActionFn a_callback)
	{
		F4T::TradeView::SetActionCallback(a_callback);
	}

	void Api_NotifyTradeStateChanged(const char* a_stateJson)
	{
		F4T::TradeView::NotifyStateChanged(a_stateJson);
	}

	bool Api_IsTradeWindowVisible()
	{
		return F4T::TradeView::IsCurrentlyVisible();
	}

	void Api_OpenChatWindow()
	{
		F4T::ChatView::Open("plugin-api");
	}

	void Api_CloseChatWindow()
	{
		F4T::ChatView::Close("plugin-api");
	}

	bool Api_IsChatVisible()
	{
		return F4T::ChatView::IsOpen();
	}

	bool Api_IsSlotScreenVisible()
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
		Api_PushStateBag,
		Api_OpenResourceView,
		Api_CloseResourceView,
		Api_SetResourceMessageCallback
	};
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI17 g_interfaceV17{
		Api_SetInteractWheelCallbacks,
		Api_OpenInteractWheel,
		Api_CloseInteractWheel,
		Api_NotifyInteractWheelStateChanged,
		Api_IsInteractWheelVisible,
		Api_IsChatVisible,
		Api_IsSlotScreenVisible
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI18 g_interfaceV18{
		Api_SetAdminCallbacks,
		Api_NotifyAdminStateChanged,
		Api_OpenAdminWindow,
		Api_CloseAdminWindow,
		Api_IsAdminWindowVisible,
		Api_SetAdminAvailable
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI19 g_interfaceV19{ Api_HandlePauseMenuMapKey };

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI20 g_interfaceV20{
		Api_SetTradeActionCallback,
		Api_NotifyTradeStateChanged,
		Api_IsTradeWindowVisible,
		Api_OpenChatWindow,
		Api_CloseChatWindow
	};

	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI21 g_interfaceV21{ Api_SetReportCallbacks, Api_PushReportEvent };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI22 g_interfaceV22{ Api_GetViewHandle };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI23 g_interfaceV23{ Api_PushStructureHud };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI24 g_interfaceV24{ Api_PushNeedsState };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI25 g_interfaceV25{ Api_OpenWorldFactionWindow };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI26 g_interfaceV26{ Api_GetMultiplayerPreferences, Api_PushNetStats };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI27 g_interfaceV27{ Api_PushNameplates };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI28 g_interfaceV28{ Api_SetTeleportBeganCallback };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI29 g_interfaceV29{ Api_PushInteractPrompt };
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI30 g_interfaceV30{
		Api_SetParkedSuitCallbacks,
		Api_OpenParkedSuitMenu,
		Api_CloseParkedSuitMenu,
		Api_NotifyParkedSuitStateChanged,
		Api_IsParkedSuitMenuVisible
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
		case InterfaceVersion::V17:
			return a_visit(g_interfaceV17);
		case InterfaceVersion::V18:
			return a_visit(g_interfaceV18);
		case InterfaceVersion::V19:
			return a_visit(g_interfaceV19);
		case InterfaceVersion::V20:
			return a_visit(g_interfaceV20);
		case InterfaceVersion::V21:
			return a_visit(g_interfaceV21);
		case InterfaceVersion::V22:
			return a_visit(g_interfaceV22);
		case InterfaceVersion::V23:
			return a_visit(g_interfaceV23);
		case InterfaceVersion::V24:
			return a_visit(g_interfaceV24);
		case InterfaceVersion::V25:
			return a_visit(g_interfaceV25);
		case InterfaceVersion::V26:
			return a_visit(g_interfaceV26);
		case InterfaceVersion::V27:
			return a_visit(g_interfaceV27);
		case InterfaceVersion::V28:
			return a_visit(g_interfaceV28);
		case InterfaceVersion::V29:
			return a_visit(g_interfaceV29);
		case InterfaceVersion::V30:
			return a_visit(g_interfaceV30);
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
			"{} Refused interface V{}: the caller expects layout {:#010x}, this DLL has {:#010x}. "
			"The UI code and the plugin code in this DLL disagree on the interface layout.",
			COUI::Session::GetLogPrefix(),
			static_cast<unsigned>(Map::version),
			a_layout,
			Map::layout);
		return nullptr;
	});
}
