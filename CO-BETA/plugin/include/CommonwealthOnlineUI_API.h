#ifndef COMMONWEALTH_ONLINE_UI_API_H
#define COMMONWEALTH_ONLINE_UI_API_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
	#define NOMINMAX
#endif

#include <Windows.h>
#endif
#include <cstddef>
#include <cstdint>

namespace F4T::DownedUi
{
	using RespawnChosenFn = void (*)(const char* a_point, const char* a_friendId);
}

namespace COMMONWEALTH_ONLINE_UI_API
{
	constexpr const auto PluginName = "CommonwealthOnlineUI";

	enum class InterfaceVersion : std::uint8_t
	{
		V1 = 1,
		V2 = 2,
		V3 = 3,
		V4 = 4,
		V5 = 5,
		V6 = 6,
		V7 = 7,
		V8 = 8,
		V9 = 9,
		V10 = 10,
		V11 = 11,
		V12 = 12,
		V13 = 13,
		V14 = 14,
		V15 = 15,
		V16 = 16,
		V17 = 17,
		V18 = 18,
		V19 = 19,
		V20 = 20,
		V21 = 21,
		V22 = 22,
		V23 = 23,
		V24 = 24,
		V25 = 25,
		V26 = 26,
		V27 = 27,
		V28 = 28,
		V29 = 29,
		V30 = 30
	};

	struct JoinRequest
	{
		const char*   id{ nullptr };
		const char*   name{ nullptr };
		const char*   host{ nullptr };
		std::uint16_t port{ 7777 };
		bool          passworded{ false };
	};

	struct AlertAction
	{
		const char* id{ nullptr };
		const char* label{ nullptr };
		const char* hotkey{ nullptr };
	};

	struct AlertRequest
	{
		const char*        id{ nullptr };
		const char*        category{ nullptr };
		const char*        title{ nullptr };
		const char*        message{ nullptr };
		const char*        tone{ nullptr };  // info | success | warning | error | invite
		std::uint32_t      durationMs{ 8000 };
		const AlertAction* actions{ nullptr };
		std::size_t        actionCount{ 0 };
	};

	using JoinCompleteFn = void (*)(bool a_success, const char* a_error, void* a_userData);
	using AlertResponseFn = void (*)(const char* a_alertId, const char* a_actionId, void* a_userData);

	struct SessionCallbacks
	{
		bool (*JoinSession)(
			const JoinRequest* a_request,
			JoinCompleteFn     a_onComplete,
			void*              a_userData,
			char*              a_errorBuf,
			std::size_t        a_errorBufSize){ nullptr };

		void (*Disconnect)(){ nullptr };

		bool (*ConnectLocal)(char* a_errorBuf, std::size_t a_errorBufSize){ nullptr };

		bool (*IsConnected)(){ nullptr };

		bool (*ScanLocalServers)(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize){ nullptr };
	};

	using RemotePlayersMapJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	struct IVCommonwealthOnlineUI1
	{
		void (*OnGameDataReady)(){ nullptr };
		void (*ShowMainMenu)(){ nullptr };
		void (*HideMainMenu)(){ nullptr };
		bool (*IsMainMenuVisible)(){ nullptr };
		void (*ShowServerBrowser)(){ nullptr };
		void (*HideServerBrowser)(){ nullptr };
		bool (*IsBrowserVisible)(){ nullptr };
		void (*DispatchMainMenuControllerInput)(const char* a_button, const char* a_phase){ nullptr };
		bool (*IsCustomMenuInputCaptured)(){ nullptr };
		void (*SetSessionCallbacks)(const SessionCallbacks* a_callbacks){ nullptr };
		void (*PushConnectionStatus)(const char* a_status){ nullptr };

		bool (*ShowAlert)(
			const AlertRequest* a_request,
			AlertResponseFn     a_onResponse,
			void*               a_userData,
			char*               a_idOut,
			std::size_t         a_idOutSize){ nullptr };
		void (*DismissAlert)(const char* a_alertId){ nullptr };
		void (*DismissAllAlerts)(){ nullptr };

		void (*SetRemotePlayersMapJsonCallback)(RemotePlayersMapJsonFn a_callback){ nullptr };

		void (*ShowSessionLockout)(const char* a_code, const char* a_reason){ nullptr };
	};

	struct IVCommonwealthOnlineUI2
	{
		bool (*HasLastJoin)(){ nullptr };
		bool (*RetryLastJoin)(
			JoinCompleteFn a_onComplete,
			void*          a_userData,
			char*          a_errorBuf,
			std::size_t    a_errorBufSize){ nullptr };
	};

	using FactionStateJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	using FactionActionFn = void (*)(const char* a_actionJson);

	struct FactionCallbacks
	{
		FactionStateJsonFn GetStateJson{ nullptr };
		FactionActionFn    SendAction{ nullptr };
	};

	using ChatActionFn = void (*)(const char* a_actionJson);

	struct ChatCallbacks
	{
		ChatActionFn SendAction{ nullptr };
	};

	using QuestStateJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	using QuestActionFn = void (*)(const char* a_actionJson);

	struct QuestCallbacks
	{
		QuestStateJsonFn GetStateJson{ nullptr };
		QuestActionFn    SendAction{ nullptr };
	};

	using PasswordJoinFn = bool (*)(const JoinRequest*, const char* a_password, JoinCompleteFn, void*, char* a_errorBuf, std::size_t a_errorBufSize);

	struct IVCommonwealthOnlineUI3
	{
		void (*SetFactionCallbacks)(const FactionCallbacks* a_callbacks){ nullptr };
		void (*NotifyFactionStateChanged)(){ nullptr };
		void (*OpenFactionWindow)(){ nullptr };
		void (*CloseFactionWindow)(){ nullptr };
		bool (*IsFactionWindowVisible)(){ nullptr };
		void (*SetChatCallbacks)(const ChatCallbacks* a_callbacks){ nullptr };
		void (*PushChatEvent)(const char* a_eventJson){ nullptr };
		void (*SetPasswordJoinCallback)(PasswordJoinFn a_callback){ nullptr };
	};

	struct IVCommonwealthOnlineUI4
	{
		void (*SetRespawnChosenCallback)(F4T::DownedUi::RespawnChosenFn a_callback){ nullptr };
		void (*PushDownedState)(const char* a_stateJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI5
	{
		void (*SetWorldMode)(const char* a_mode){ nullptr };
	};

	struct IVCommonwealthOnlineUI6
	{
		void (*PushSessionState)(const char* a_stateJson){ nullptr };
	};

	struct SteamCallbacks
	{
		void (*HostStart)(const char* a_name, const char* a_password, std::uint32_t a_maxPlayers){ nullptr };
		void (*HostStop)(){ nullptr };
		void (*Join)(const char* a_friendCode, const char* a_password){ nullptr };
		void (*Invite)(){ nullptr };
		void (*HostEnter)(){ nullptr };
	};

	struct IVCommonwealthOnlineUI7
	{
		void (*SetSteamCallbacks)(const SteamCallbacks* a_callbacks){ nullptr };
		void (*PushSteamEvent)(const char* a_eventJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI8
	{
		bool (*GetMultiplayerDisplayName)(char* a_buffer, std::size_t a_capacity){ nullptr };
	};

	using VoiceActionFn = void (*)(const char* a_actionJson);

	struct VoiceCallbacks
	{
		VoiceActionFn SendAction{ nullptr };
	};

	struct IVCommonwealthOnlineUI9
	{
		void (*SetVoiceCallbacks)(const VoiceCallbacks* a_callbacks){ nullptr };
		void (*PushVoiceEvent)(const char* a_eventJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI10
	{
		void (*NotifySessionExitSave)(const char* a_state, const char* a_message){ nullptr };
	};

	struct IVCommonwealthOnlineUI11
	{
		void (*SetQuestCallbacks)(const QuestCallbacks* a_callbacks){ nullptr };
		void (*NotifyQuestStateChanged)(){ nullptr };
		void (*OpenQuestWindow)(){ nullptr };
		void (*CloseQuestWindow)(){ nullptr };
		bool (*IsQuestWindowVisible)(){ nullptr };
	};

	struct ServerLiveCallbacks
	{
		bool (*ApplyServerSettings)(const char* a_requestJson, char* a_resultBuffer, std::size_t a_resultBufferSize){ nullptr };
	};

	struct IVCommonwealthOnlineUI12
	{
		void (*SetServerLiveCallbacks)(const ServerLiveCallbacks* a_callbacks){ nullptr };
	};

	struct IVCommonwealthOnlineUI13
	{
		void (*PushJoinProgress)(const char* a_message){ nullptr };
	};

	struct IVCommonwealthOnlineUI14
	{
		void (*PushJoinOverlay)(const char* a_stateJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI15
	{
		void (*PushCaptureHud)(const char* a_stateJson){ nullptr };
	};

	using ResourceMessageFn = void (*)(const char* a_resource, const char* a_json);

	struct AdminCallbacks
	{
		FactionStateJsonFn GetStateJson{ nullptr };
		FactionActionFn    SendAction{ nullptr };
	};

	struct IVCommonwealthOnlineUI16
	{
		void (*PushStateBag)(const char* a_json){ nullptr };
		bool (*OpenResourceView)(const char* a_resource, const char* a_view){ nullptr };
		void (*CloseResourceView)(const char* a_resource){ nullptr };
		void (*SetResourceMessageCallback)(ResourceMessageFn a_callback){ nullptr };
	};

	using InteractWheelStateJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	using InteractWheelActionFn = void (*)(const char* a_actionJson);

	struct InteractWheelCallbacks
	{
		InteractWheelStateJsonFn GetStateJson{ nullptr };
		InteractWheelActionFn    SendAction{ nullptr };
	};

	struct IVCommonwealthOnlineUI17
	{
		void (*SetInteractWheelCallbacks)(const InteractWheelCallbacks* a_callbacks){ nullptr };
		void (*OpenInteractWheel)(){ nullptr };
		void (*CloseInteractWheel)(){ nullptr };
		void (*NotifyInteractWheelStateChanged)(){ nullptr };
		bool (*IsInteractWheelVisible)(){ nullptr };
		bool (*IsChatVisible)(){ nullptr };
		bool (*IsSlotScreenVisible)(){ nullptr };
	};

	struct IVCommonwealthOnlineUI18
	{
		void (*SetAdminCallbacks)(const AdminCallbacks* a_callbacks){ nullptr };
		void (*NotifyAdminStateChanged)(){ nullptr };
		void (*OpenAdminWindow)(){ nullptr };
		void (*CloseAdminWindow)(){ nullptr };
		bool (*IsAdminWindowVisible)(){ nullptr };
		void (*SetAdminAvailable)(bool a_available){ nullptr };
	};

	struct IVCommonwealthOnlineUI19
	{
		void (*HandlePauseMenuMapKey)(bool a_allowed){ nullptr };
	};

	using TradeActionFn = void (*)(const char* a_actionJson);

	struct IVCommonwealthOnlineUI20
	{
		void (*SetTradeActionCallback)(TradeActionFn a_callback){ nullptr };
		void (*NotifyTradeStateChanged)(const char* a_stateJson){ nullptr };
		bool (*IsTradeWindowVisible)(){ nullptr };
		void (*OpenChatWindow)(){ nullptr };
		void (*CloseChatWindow)(){ nullptr };
	};

	using ReportStateJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	using ReportActionFn = void (*)(const char* a_actionJson);

	struct ReportCallbacks
	{
		ReportStateJsonFn GetStateJson{ nullptr };
		ReportActionFn    SendAction{ nullptr };
	};

	struct IVCommonwealthOnlineUI21
	{
		void (*SetReportCallbacks)(const ReportCallbacks* a_callbacks){ nullptr };
		void (*PushReportEvent)(const char* a_eventJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI22
	{
		std::uint64_t (*GetViewHandle)(const char* a_view){ nullptr };
	};

	struct IVCommonwealthOnlineUI23
	{
		void (*PushStructureHud)(const char* a_stateJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI24
	{
		void (*PushNeedsState)(const char* a_stateJson){ nullptr };
	};

	struct IVCommonwealthOnlineUI25
	{
		bool (*OpenWorldFactionWindow)(){ nullptr };
	};

	struct MultiplayerPreferences
	{
		std::uint32_t size{ sizeof(MultiplayerPreferences) };
		std::uint8_t  onlineStatus{ 0 };
		std::uint8_t  allowFriendJoin{ 0 };
		std::uint8_t  mapMarkerSize{ 0 };
		std::uint8_t  friendRequests{ 0 };
		std::uint8_t  regionPreference{ 0 };
		std::uint8_t  interpolationQuality{ 0 };
		std::uint8_t  showLocationToFriends{ 0 };
		std::uint8_t  showPlayerMarkers{ 0 };
		std::uint8_t  showFriendMarkers{ 0 };
		std::uint8_t  showNameplates{ 0 };
		std::uint8_t  networkSmoothing{ 0 };
		std::uint8_t  showPing{ 0 };
		std::uint8_t  networkStatsOverlay{ 0 };
		std::uint8_t  reserved{ 0 };
	};

	struct IVCommonwealthOnlineUI26
	{
		bool (*GetMultiplayerPreferences)(MultiplayerPreferences* a_out){ nullptr };
		void (*PushNetStats)(const char* a_json){ nullptr };
	};

	struct IVCommonwealthOnlineUI27
	{
		void (*PushNameplates)(const char* a_json){ nullptr };
	};

	using TeleportBeganFn = void (*)();

	struct IVCommonwealthOnlineUI28
	{
		void (*SetTeleportBeganCallback)(TeleportBeganFn a_callback){ nullptr };
	};

	using ParkedSuitStateJsonFn = bool (*)(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	using ParkedSuitActionFn = void (*)(const char* a_actionJson);

	struct ParkedSuitCallbacks
	{
		ParkedSuitStateJsonFn GetStateJson{ nullptr };
		ParkedSuitActionFn    SendAction{ nullptr };
	};

	struct IVCommonwealthOnlineUI29
	{
		void (*PushInteractPrompt)(const char* a_json){ nullptr };
	};

	struct IVCommonwealthOnlineUI30
	{
		void (*SetParkedSuitCallbacks)(const ParkedSuitCallbacks* a_callbacks){ nullptr };
		void (*OpenParkedSuitMenu)(){ nullptr };
		void (*CloseParkedSuitMenu)(){ nullptr };
		void (*NotifyParkedSuitStateChanged)(){ nullptr };
		bool (*IsParkedSuitMenuVisible)(){ nullptr };
	};

	using RequestPluginAPICheckedFunc = void* (*)(InterfaceVersion a_interfaceVersion, std::uint32_t a_layout);
	using InterfaceRefusedFn = void (*)(InterfaceVersion a_interfaceVersion, std::uint32_t a_layout);

	inline InterfaceRefusedFn OnInterfaceRefused{ nullptr };

	template <typename T>
	struct InterfaceVersionMap;

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI1>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V1;
		static constexpr std::uint32_t    layout = 0x7C3A1E01;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI2>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V2;
		static constexpr std::uint32_t    layout = 0x51D90B02;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI3>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V3;
		static constexpr std::uint32_t    layout = 0x2E86F403;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI4>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V4;
		static constexpr std::uint32_t    layout = 0x9A4C7204;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI5>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V5;
		static constexpr std::uint32_t    layout = 0x63F1D805;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI6>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V6;
		static constexpr std::uint32_t    layout = 0x0DB52906;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI7>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V7;
		static constexpr std::uint32_t    layout = 0x3D82E907;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI8>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V8;
		static constexpr std::uint32_t    layout = 0x46A2E708;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI9>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V9;
		static constexpr std::uint32_t    layout = 0xC51F3A09;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI10>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V10;
		static constexpr std::uint32_t    layout = 0xE17A9C0A;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI11>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V11;
		static constexpr std::uint32_t    layout = 0x2D7F1B0B;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI12>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V12;
		static constexpr std::uint32_t    layout = 0x7A4B5C0C;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI13>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V13;
		static constexpr std::uint32_t    layout = 0xE6A14D0D;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI14>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V14;
		static constexpr std::uint32_t    layout = 0x5E1D0C0E;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI15>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V15;
		static constexpr std::uint32_t    layout = 0x8B3F2E0F;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI16>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V16;
		static constexpr std::uint32_t    layout = 0x4C6E8B10;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI17>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V17;
		static constexpr std::uint32_t    layout = 0xE5C2A71F;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI18>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V18;
		static constexpr std::uint32_t    layout = 0x6F2D9C12;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI19>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V19;
		static constexpr std::uint32_t    layout = 0xA2D71F13;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI20>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V20;
		static constexpr std::uint32_t    layout = 0x6D1B2E14;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI21>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V21;
		static constexpr std::uint32_t    layout = 0x3E7B9D15;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI22>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V22;
		static constexpr std::uint32_t    layout = 0x7D4E9A16;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI23>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V23;
		static constexpr std::uint32_t    layout = 0x4B8E3D17;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI24>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V24;
		static constexpr std::uint32_t    layout = 0x2A7D5E18;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI25>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V25;
		static constexpr std::uint32_t    layout = 0x6A3F2B19;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI26>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V26;
		static constexpr std::uint32_t    layout = 0x3F6C9B1A;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI27>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V27;
		static constexpr std::uint32_t    layout = 0x5B9E4C1B;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI28>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V28;
		static constexpr std::uint32_t    layout = 0x8E2D5A1C;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI29>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V29;
		static constexpr std::uint32_t    layout = 0x9A7C3E1D;
	};

	template <>
	struct InterfaceVersionMap<IVCommonwealthOnlineUI30>
	{
		static constexpr InterfaceVersion version = InterfaceVersion::V30;
		static constexpr std::uint32_t    layout = 0x4F1B7C2D;
	};

	template <typename T>
	[[nodiscard]] inline T* AcceptInterface(T* a_table, std::uint32_t a_layout)
	{
		return a_layout == InterfaceVersionMap<T>::layout ? a_table : nullptr;
	}

	template <typename T>
	[[nodiscard]] inline T* RequestPluginAPI()
	{
#if defined(_WIN32)
		const auto pluginHandle = GetModuleHandleW(L"CommonwealthOnline.dll");
		if (!pluginHandle) {
			return nullptr;
		}

		const auto requestPluginAPI =
			reinterpret_cast<RequestPluginAPICheckedFunc>(GetProcAddress(pluginHandle, "RequestPluginAPIChecked"));
		auto* api = requestPluginAPI ?
		                 static_cast<T*>(requestPluginAPI(InterfaceVersionMap<T>::version, InterfaceVersionMap<T>::layout)) :
		                 nullptr;
		if (!api && OnInterfaceRefused) {
			OnInterfaceRefused(InterfaceVersionMap<T>::version, InterfaceVersionMap<T>::layout);
		}
		return api;
#else
		return nullptr;
#endif
	}
}
#endif
