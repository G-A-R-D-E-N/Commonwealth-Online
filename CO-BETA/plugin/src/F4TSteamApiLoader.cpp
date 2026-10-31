// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.

#include "F4TSteamApiLoader.h"

#include "F4TDebugLogging.h"
#include "F4TDebugSwitch.h"
#include "F4TSteamId.h"
#include "F4TSteamInvite.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if __has_include(<steam/steamnetworkingsockets.h>)
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
#define F4T_STEAM_TUNNEL_HAS_GNS_HEADERS 1
#else
#define F4T_STEAM_TUNNEL_HAS_GNS_HEADERS 0
#ifdef _WIN32
#error "GameNetworkingSockets headers are missing: run git submodule update --init CO-BETA/ThirdParty/GameNetworkingSockets-src (Steam P2P would otherwise ship disabled)"
#endif
#endif

namespace F4T::Steam
{
	namespace
	{
		constexpr const char* kSteamDllName = "steam_api64.dll";
		constexpr int kFriendFlagImmediate = 0x04;
		constexpr int kFriendRelationshipFriend = 3;
		constexpr int kResultOk = 1;
		constexpr std::size_t kMaximumBufferedEvents = 256;

		constexpr const char* kSocketsAccessors[] = {
			"SteamAPI_SteamNetworkingSockets_SteamAPI_v012",
			"SteamAPI_SteamNetworkingSockets_SteamAPI_v011",
			"SteamAPI_SteamNetworkingSockets_SteamAPI_v010",
			"SteamAPI_SteamNetworkingSockets_SteamAPI_v009",
		};

		constexpr const char* kUtilsAccessors[] = {
			"SteamAPI_SteamNetworkingUtils_SteamAPI_v004",
			"SteamAPI_SteamNetworkingUtils_SteamAPI_v003",
		};

		constexpr const char* kFriendsAccessors[] = {
			"SteamAPI_SteamFriends_v017",
			"SteamAPI_SteamFriends_v016",
			"SteamAPI_SteamFriends_v015",
			"SteamFriends",
		};

		constexpr const char* kUserAccessors[] = {
			"SteamAPI_SteamUser_v021",
			"SteamAPI_SteamUser_v020",
			"SteamAPI_SteamUser_v019",
			"SteamUser",
		};

		constexpr const char* kMatchmakingAccessors[] = {
			"SteamAPI_SteamMatchmaking_v009",
			"SteamMatchmaking",
		};

		constexpr const char* kProbeSocketsVersion = "SteamNetworkingSockets012";
		constexpr const char* kProbeUtilsVersion = "SteamNetworkingUtils004";
		constexpr const char* kProbeMatchmakingVersion = "SteamMatchMaking009";
		constexpr std::string_view kSessionLog = "SteamSession";
		constexpr int kLobbyTypeFriendsOnly = 1;

#if F4T_STEAM_TUNNEL_HAS_GNS_HEADERS && defined(STEAMNETWORKINGSOCKETS_INTERFACE_VERSION)
		static_assert(std::string_view{ STEAMNETWORKINGSOCKETS_INTERFACE_VERSION } == std::string_view{ kProbeSocketsVersion },
			"the pinned GameNetworkingSockets header and the generic-interface fallback disagree on the sockets interface version");
#endif

#ifdef _WIN32
		using ModuleHandle = HMODULE;

		ModuleHandle FindLoadedModule(const char* a_name)
		{
			return GetModuleHandleA(a_name);
		}

		void* FindExport(ModuleHandle a_module, const char* a_name)
		{
			return reinterpret_cast<void*>(GetProcAddress(a_module, a_name));
		}
#else
		using ModuleHandle = void*;

		ModuleHandle FindLoadedModule(const char*)
		{
			return nullptr;
		}

		void* FindExport(ModuleHandle, const char*)
		{
			return nullptr;
		}
#endif

		using InterfaceAccessor = void* (*)();
		using GetSteamIdFn = std::uint64_t (*)(void*);
		using InitRelayFn = void (*)(void*);
		using GetRelayStatusFn = int (*)(void*, void*);
		using CreateListenSocketP2PFn = std::uint32_t (*)(void*, int, int, const void*);
		using ConnectP2PFn = std::uint32_t (*)(void*, const void*, int, int, const void*);
		using AcceptConnectionFn = int (*)(void*, std::uint32_t);
		using CloseConnectionFn = bool (*)(void*, std::uint32_t, int, const char*, bool);
		using CloseListenSocketFn = bool (*)(void*, std::uint32_t);
		using SendMessageFn = int (*)(void*, std::uint32_t, const void*, std::uint32_t, int, std::int64_t*);
		using ReceiveMessagesFn = int (*)(void*, std::uint32_t, void**, int);
		using RunCallbacksFn = void (*)(void*);
		using GetFriendRelationshipFn = int (*)(void*, std::uint64_t);
		using GetFriendCountFn = int (*)(void*, int);
		using GetFriendByIndexFn = std::uint64_t (*)(void*, int, int);
		using GetFriendPersonaNameFn = const char* (*)(void*, std::uint64_t);
		using GetPersonaNameFn = const char* (*)(void*);
		using GetFriendRichPresenceFn = const char* (*)(void*, std::uint64_t, const char*);
		using RequestFriendRichPresenceFn = void (*)(void*, std::uint64_t);
		using SetRichPresenceFn = bool (*)(void*, const char*, const char*);
		using ClearRichPresenceFn = void (*)(void*);
		using GetFriendGamePlayedFn = bool (*)(void*, std::uint64_t, void*);
		using GetAuthSessionTicketFn = std::uint32_t (*)(void*, void*, int, std::uint32_t*, const void*);
		using LegacyGetAuthSessionTicketFn = std::uint32_t (*)(void*, void*, int, std::uint32_t*);
		using CancelAuthTicketFn = void (*)(void*, std::uint32_t);
		using SteamClientFn = void* (*)();
		using GetSteamHandleFn = std::int32_t (*)();
		using GetGenericInterfaceFn = void* (*)(void*, std::int32_t, std::int32_t, const char*);
		using MatchmakingCreateLobbyFn = std::uint64_t (*)(void*, int, int);
		using MatchmakingJoinLobbyFn = std::uint64_t (*)(void*, std::uint64_t);
		using MatchmakingLeaveLobbyFn = void (*)(void*, std::uint64_t);
		using MatchmakingSetLobbyDataFn = bool (*)(void*, std::uint64_t, const char*, const char*);
		using MatchmakingGetLobbyDataFn = const char* (*)(void*, std::uint64_t, const char*);
		using MatchmakingGetLobbyOwnerFn = std::uint64_t (*)(void*, std::uint64_t);
		using MatchmakingGetNumLobbyMembersFn = int (*)(void*, std::uint64_t);
		using MatchmakingSetLobbyMemberLimitFn = bool (*)(void*, std::uint64_t, int);
		using MatchmakingSetLobbyJoinableFn = bool (*)(void*, std::uint64_t, bool);
		constexpr std::uint32_t kInvalidAuthTicket = 0;

		struct SteamExports
		{
			void* sockets{};
			void* utils{};
			void* friends{};
			void* user{};
			GetSteamIdFn getSteamId{};
			InitRelayFn initRelay{};
			GetRelayStatusFn getRelayStatus{};
			CreateListenSocketP2PFn createListenSocketP2P{};
			ConnectP2PFn connectP2P{};
			AcceptConnectionFn acceptConnection{};
			CloseConnectionFn closeConnection{};
			CloseListenSocketFn closeListenSocket{};
			SendMessageFn sendMessage{};
			ReceiveMessagesFn receiveMessages{};
			RunCallbacksFn runCallbacks{};
			GetFriendRelationshipFn getFriendRelationship{};
			GetFriendCountFn friendCount{};
			GetFriendByIndexFn friendByIndex{};
			GetFriendPersonaNameFn friendPersonaName{};
			GetPersonaNameFn personaName{};
			GetFriendRichPresenceFn friendRichPresence{};
			RequestFriendRichPresenceFn requestFriendRichPresence{};
			SetRichPresenceFn setRichPresence{};
			ClearRichPresenceFn clearRichPresence{};
			GetFriendGamePlayedFn getFriendGamePlayed{};
			GetAuthSessionTicketFn getAuthSessionTicket{};
			LegacyGetAuthSessionTicketFn getAuthSessionTicketLegacy{};
			CancelAuthTicketFn cancelAuthTicket{};
		};

		std::function<std::vector<std::uint8_t>()> MakeAuthTicketSource(const SteamExports& a_exports)
		{
			if (a_exports.cancelAuthTicket == nullptr || (a_exports.getAuthSessionTicket == nullptr && a_exports.getAuthSessionTicketLegacy == nullptr)) return {};
			struct Live
			{
				~Live()
				{
					if (handle != kInvalidAuthTicket) cancel(user, handle);
				}

				void* user{};
				CancelAuthTicketFn cancel{};
				std::mutex mutex;
				std::uint32_t handle{ kInvalidAuthTicket };
			};
			auto live = std::make_shared<Live>();
			live->user = a_exports.user;
			live->cancel = a_exports.cancelAuthTicket;
			return [live, issue = a_exports.getAuthSessionTicket, legacyIssue = a_exports.getAuthSessionTicketLegacy] {
				std::lock_guard lock{ live->mutex };
				if (live->handle != kInvalidAuthTicket) live->cancel(live->user, std::exchange(live->handle, kInvalidAuthTicket));
				std::vector<std::uint8_t> ticket(F4T::SteamId::kMaximumTicketBytes);
				std::uint32_t size = 0;
				live->handle = legacyIssue != nullptr
					? legacyIssue(live->user, ticket.data(), static_cast<int>(ticket.size()), &size)
					: issue(live->user, ticket.data(), static_cast<int>(ticket.size()), &size, nullptr);
				if (live->handle == kInvalidAuthTicket || size == 0 || size > ticket.size()) return std::vector<std::uint8_t>{};
				ticket.resize(size);
				return ticket;
			};
		}

		template <std::size_t N>
		InterfaceAccessor ResolveAccessor(
			const std::function<void*(const char*)>& a_findExport,
			const char* const (&a_names)[N],
			std::string& a_matched,
			std::vector<std::string>& a_missing)
		{
			const auto* matched = ChooseFirstPresentName(a_names, a_findExport);
			if (matched == nullptr) {
				for (const auto* name : a_names) a_missing.emplace_back(name);
				return nullptr;
			}
			a_matched = matched;
			return reinterpret_cast<InterfaceAccessor>(a_findExport(matched));
		}

		template <class T>
		void Resolve(const std::function<void*(const char*)>& a_findExport, const char* a_name, T& a_out, std::vector<std::string>& a_missing)
		{
			a_out = reinterpret_cast<T>(a_findExport(a_name));
			if (a_out == nullptr) a_missing.emplace_back(a_name);
		}

		InterfaceProbeInputs MakeProbeInputs(const std::function<void*(const char*)>& a_findExport, void* a_sockets, void* a_utils)
		{
			InterfaceProbeInputs inputs;
			if (const auto client = reinterpret_cast<SteamClientFn>(a_findExport("SteamClient"))) {
				inputs.steamClient = [client] { return client(); };
			}
			if (const auto user = reinterpret_cast<GetSteamHandleFn>(a_findExport("SteamAPI_GetHSteamUser"))) {
				inputs.steamUser = [user] { return user(); };
			}
			if (const auto pipe = reinterpret_cast<GetSteamHandleFn>(a_findExport("SteamAPI_GetHSteamPipe"))) {
				inputs.steamPipe = [pipe] { return pipe(); };
			}
			if (const auto generic = reinterpret_cast<GetGenericInterfaceFn>(a_findExport("SteamAPI_ISteamClient_GetISteamGenericInterface"))) {
				inputs.genericInterface = [generic](void* a_client, std::int32_t a_user, std::int32_t a_pipe, const char* a_version) {
					return generic(a_client, a_user, a_pipe, a_version);
				};
			}
			inputs.socketsFromAccessor = a_sockets;
			inputs.utilsFromAccessor = a_utils;
			return inputs;
		}

		std::string DescribeMissing(const std::vector<std::string>& a_missing)
		{
			std::string reason = std::string{ kSteamDllName } + " does not export " + a_missing.front();
			if (a_missing.size() > 1) {
				reason += " (and " + std::to_string(a_missing.size() - 1) + " more)";
			}
			return reason;
		}

#ifdef _WIN32
		std::string DescribeModuleVersion(ModuleHandle a_module)
		{
			std::string versionPart = "no version resource";
			const auto resource = FindResourceW(a_module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
			const auto loaded = resource != nullptr ? LoadResource(a_module, resource) : nullptr;
			const auto size = resource != nullptr ? SizeofResource(a_module, resource) : 0;
			const auto data = loaded != nullptr ? LockResource(loaded) : nullptr;
			if (data != nullptr && size != 0) {
				versionPart = DescribeVersionResource(std::span<const std::uint8_t>{ static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size) });
			}

			wchar_t path[MAX_PATH]{};
			std::string sizePart;
			if (GetModuleFileNameW(a_module, path, MAX_PATH) > 0) {
				std::error_code error;
				const auto bytes = std::filesystem::file_size(path, error);
				if (!error) sizePart = ", " + std::to_string(bytes) + " bytes";
			}
			return std::string{ kSteamDllName } + " " + versionPart + sizePart;
		}
#else
		std::string DescribeModuleVersion(ModuleHandle)
		{
			return {};
		}
#endif

#if F4T_STEAM_TUNNEL_HAS_GNS_HEADERS
		std::mutex g_eventMutex;
		std::deque<Transport::SteamConnectionEvent> g_events;
		std::unordered_set<Transport::SteamConnectionHandle> g_outbound;

		std::vector<Transport::SteamConnectionEvent> TakeEvents(bool a_outbound)
		{
			std::lock_guard lock{ g_eventMutex };
			std::vector<Transport::SteamConnectionEvent> mine;
			std::deque<Transport::SteamConnectionEvent> keep;
			for (const auto& event : g_events) {
				if (g_outbound.contains(event.connection) == a_outbound) mine.push_back(event);
				else keep.push_back(event);
			}
			g_events.swap(keep);
			return mine;
		}

		Transport::TunnelState MapConnectionState(ESteamNetworkingConnectionState a_state)
		{
			switch (a_state) {
			case k_ESteamNetworkingConnectionState_Connecting:
			case k_ESteamNetworkingConnectionState_FindingRoute:
				return Transport::TunnelState::Connecting;
			case k_ESteamNetworkingConnectionState_Connected:
				return Transport::TunnelState::Connected;
			case k_ESteamNetworkingConnectionState_ClosedByPeer:
			case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
				return Transport::TunnelState::Failed;
			default:
				return Transport::TunnelState::Closed;
			}
		}

		void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* a_info)
		{
			if (a_info == nullptr) return;

			Transport::SteamConnectionEvent event;
			event.connection = a_info->m_hConn;
			event.peerSteamId = a_info->m_info.m_identityRemote.GetSteamID64();
			event.state = MapConnectionState(a_info->m_info.m_eState);
			event.endReason = a_info->m_info.m_eEndReason;
			event.endDebug = a_info->m_info.m_szEndDebug;

			std::lock_guard lock{ g_eventMutex };
			if (g_events.size() >= kMaximumBufferedEvents) g_events.pop_front();
			g_events.push_back(event);
		}

		void* FindGenericInterface(const InterfaceProbeInputs& a_inputs, const char* a_version)
		{
			if (!a_inputs.steamClient || !a_inputs.steamUser || !a_inputs.steamPipe || !a_inputs.genericInterface) return nullptr;
			void* const client = a_inputs.steamClient();
			const auto user = a_inputs.steamUser();
			const auto pipe = a_inputs.steamPipe();
			if (client == nullptr || user == 0 || pipe == 0) return nullptr;
			return a_inputs.genericInterface(client, user, pipe, a_version);
		}

		std::string JoinNames(const std::vector<std::string>& a_names)
		{
			std::string joined;
			for (const auto& name : a_names) {
				if (!joined.empty()) joined += ", ";
				joined += name;
			}
			return joined;
		}

		void ResolveMatchmaking(const std::function<void*(const char*)>& a_findExport, SteamApiLoadResult& a_result)
		{
			std::vector<std::string> missing;
			std::string version;
			void* self = nullptr;
			if (const auto accessor = ResolveAccessor(a_findExport, kMatchmakingAccessors, version, missing)) {
				self = accessor();
				if (self == nullptr) missing.emplace_back(version + " returned null");
			} else {
				self = FindGenericInterface(MakeProbeInputs(a_findExport, nullptr, nullptr), kProbeMatchmakingVersion);
				if (self != nullptr) {
					missing.clear();
					version = std::string{ kProbeMatchmakingVersion } + " from GetISteamGenericInterface";
				}
			}

			MatchmakingCreateLobbyFn create{};
			MatchmakingJoinLobbyFn join{};
			MatchmakingLeaveLobbyFn leave{};
			MatchmakingSetLobbyDataFn setData{};
			MatchmakingGetLobbyDataFn getData{};
			MatchmakingGetLobbyOwnerFn owner{};
			MatchmakingGetNumLobbyMembersFn members{};
			MatchmakingSetLobbyMemberLimitFn limit{};
			MatchmakingSetLobbyJoinableFn joinable{};
			void* registerCallResult{};
			void* unregisterCallResult{};
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_CreateLobby", create, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_JoinLobby", join, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_LeaveLobby", leave, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_SetLobbyData", setData, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_GetLobbyData", getData, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_GetLobbyOwner", owner, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers", members, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit", limit, missing);
			Resolve(a_findExport, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable", joinable, missing);
			Resolve(a_findExport, "SteamAPI_RegisterCallResult", registerCallResult, missing);
			Resolve(a_findExport, "SteamAPI_UnregisterCallResult", unregisterCallResult, missing);

			a_result.matchmakingMissing = missing;
			a_result.matchmakingAvailable = missing.empty();
			if (!missing.empty()) {
				a_result.missingOptional.insert(a_result.missingOptional.end(), missing.begin(), missing.end());
				F4T::DebugLogging::Log(kSessionLog,
					"steam matchmaking unavailable: {} is missing {}; friends lobbies and the build check are off, rich presence join still works",
					kSteamDllName, JoinNames(missing));
				return;
			}

			a_result.foundVersions.push_back("ISteamMatchmaking " + version);
			auto& api = a_result.matchmaking;
			api.createLobby = [create, self](int a_maximum, LobbyCreatedFn a_done) {
				const auto call = create(self, kLobbyTypeFriendsOnly, a_maximum);
				if (call == 0) {
					a_done(false, 0, 0);
					return;
				}
				if (!AwaitLobbyCreated(call, a_done)) {
					F4T::DebugLogging::Log(kSessionLog, "lobby create result could not be awaited (call {}), the lobby id is unknown and cannot be left", call);
					a_done(false, 0, 0);
				}
			};
			api.joinLobby = [join, leave, self](std::uint64_t a_lobby, LobbyEnteredFn a_done) {
				const auto call = join(self, a_lobby);
				if (call == 0) {
					a_done(false, 0, a_lobby);
					return;
				}
				if (!AwaitLobbyEntered(call, a_done)) {
					leave(self, a_lobby);
					F4T::DebugLogging::Log(kSessionLog, "lobby join result could not be awaited, left lobby {}", a_lobby);
					a_done(false, 0, a_lobby);
				}
			};
			api.leaveLobby = [leave, self](std::uint64_t a_lobby) { leave(self, a_lobby); };
			api.setLobbyData = [setData, self](std::uint64_t a_lobby, const char* a_key, const char* a_value) {
				return a_key != nullptr && a_value != nullptr && setData(self, a_lobby, a_key, a_value);
			};
			api.getLobbyData = [getData, self](std::uint64_t a_lobby, const char* a_key) {
				const auto* value = a_key != nullptr ? getData(self, a_lobby, a_key) : nullptr;
				return value != nullptr ? std::string{ value } : std::string{};
			};
			api.lobbyOwner = [owner, self](std::uint64_t a_lobby) { return owner(self, a_lobby); };
			api.memberCount = [members, self](std::uint64_t a_lobby) { return members(self, a_lobby); };
			api.setMemberLimit = [limit, self](std::uint64_t a_lobby, int a_maximum) { return limit(self, a_lobby, a_maximum); };
			api.setJoinable = [joinable, self](std::uint64_t a_lobby, bool a_open) { return joinable(self, a_lobby, a_open); };
		}
		void UseInterfaceMethods(SteamExports& a_exports)
		{
			a_exports.initRelay = [](void* a_utils) {
				static_cast<ISteamNetworkingUtils*>(a_utils)->InitRelayNetworkAccess();
			};
			a_exports.getRelayStatus = [](void* a_utils, void* a_details) {
				return static_cast<int>(static_cast<ISteamNetworkingUtils*>(a_utils)->GetRelayNetworkStatus(
					static_cast<SteamRelayNetworkStatus_t*>(a_details)));
			};
			a_exports.createListenSocketP2P = [](void* a_sockets, int a_port, int a_options, const void* a_config) {
				return static_cast<std::uint32_t>(static_cast<ISteamNetworkingSockets*>(a_sockets)->CreateListenSocketP2P(
					a_port, a_options, static_cast<const SteamNetworkingConfigValue_t*>(a_config)));
			};
			a_exports.connectP2P = [](void* a_sockets, const void* a_identity, int a_port, int a_options, const void* a_config) {
				return static_cast<std::uint32_t>(static_cast<ISteamNetworkingSockets*>(a_sockets)->ConnectP2P(
					*static_cast<const SteamNetworkingIdentity*>(a_identity), a_port, a_options,
					static_cast<const SteamNetworkingConfigValue_t*>(a_config)));
			};
			a_exports.acceptConnection = [](void* a_sockets, std::uint32_t a_connection) {
				return static_cast<int>(static_cast<ISteamNetworkingSockets*>(a_sockets)->AcceptConnection(a_connection));
			};
			a_exports.closeConnection = [](void* a_sockets, std::uint32_t a_connection, int a_reason, const char* a_debug, bool a_linger) {
				return static_cast<ISteamNetworkingSockets*>(a_sockets)->CloseConnection(a_connection, a_reason, a_debug, a_linger);
			};
			a_exports.closeListenSocket = [](void* a_sockets, std::uint32_t a_listen) {
				return static_cast<ISteamNetworkingSockets*>(a_sockets)->CloseListenSocket(a_listen);
			};
			a_exports.sendMessage = [](void* a_sockets, std::uint32_t a_connection, const void* a_data, std::uint32_t a_size, int a_flags, std::int64_t* a_out) {
				return static_cast<int>(static_cast<ISteamNetworkingSockets*>(a_sockets)->SendMessageToConnection(
					a_connection, a_data, a_size, a_flags, a_out));
			};
			a_exports.receiveMessages = [](void* a_sockets, std::uint32_t a_connection, void** a_out, int a_maximum) {
				return static_cast<ISteamNetworkingSockets*>(a_sockets)->ReceiveMessagesOnConnection(
					a_connection, reinterpret_cast<SteamNetworkingMessage_t**>(a_out), a_maximum);
			};
			a_exports.runCallbacks = [](void* a_sockets) { static_cast<ISteamNetworkingSockets*>(a_sockets)->RunCallbacks(); };
		}

		bool RelayUsable(ESteamNetworkingAvailability a_availability)
		{
			switch (a_availability) {
			case k_ESteamNetworkingAvailability_Waiting:
			case k_ESteamNetworkingAvailability_Retrying:
			case k_ESteamNetworkingAvailability_Attempting:
			case k_ESteamNetworkingAvailability_Current:
				return true;
			default:
				return false;
			}
		}

		Transport::SteamApi BuildApi(const SteamExports& a_exports)
		{
			const auto sockets = a_exports.sockets;
			const auto utils = a_exports.utils;

			Transport::SteamApi api;
			api.getSteamId = [getSteamId = a_exports.getSteamId, user = a_exports.user] {
				return getSteamId(user);
			};
			api.initRelay = [initRelay = a_exports.initRelay, getRelayStatus = a_exports.getRelayStatus, utils] {
				initRelay(utils);
				return RelayUsable(static_cast<ESteamNetworkingAvailability>(getRelayStatus(utils, nullptr)));
			};
			api.createListenSocketP2P = [createListenSocketP2P = a_exports.createListenSocketP2P, sockets](std::uint16_t a_virtualPort) {
				SteamNetworkingConfigValue_t option;
				option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&OnConnectionStatusChanged));
				return createListenSocketP2P(sockets, a_virtualPort, 1, &option);
			};
			api.connectP2P = [connectP2P = a_exports.connectP2P, sockets](std::uint64_t a_steamId, std::uint16_t a_virtualPort) {
				SteamNetworkingIdentity identity;
				identity.Clear();
				identity.SetSteamID64(a_steamId);
				SteamNetworkingConfigValue_t option;
				option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&OnConnectionStatusChanged));
				const auto handle = connectP2P(sockets, &identity, a_virtualPort, 1, &option);
				if (handle != Transport::kInvalidSteamConnection) {
					std::lock_guard lock{ g_eventMutex };
					g_outbound.insert(handle);
				}
				return handle;
			};
			api.acceptConnection = [acceptConnection = a_exports.acceptConnection, sockets](Transport::SteamConnectionHandle a_connection) {
				return acceptConnection(sockets, a_connection) == kResultOk;
			};
			api.closeConnection = [closeConnection = a_exports.closeConnection, sockets](Transport::SteamConnectionHandle a_connection, int a_reason, std::string_view a_debug) {
				{
					std::lock_guard lock{ g_eventMutex };
					g_outbound.erase(a_connection);
				}
				const std::string debug{ a_debug };
				closeConnection(sockets, a_connection, a_reason, debug.c_str(), false);
			};
			api.closeListenSocket = [closeListenSocket = a_exports.closeListenSocket, sockets](Transport::SteamListenSocketHandle a_listenSocket) {
				closeListenSocket(sockets, a_listenSocket);
			};
			api.sendMessage = [sendMessage = a_exports.sendMessage, sockets](Transport::SteamConnectionHandle a_connection, std::span<const std::uint8_t> a_payload, int a_flags) {
				return sendMessage(sockets, a_connection, a_payload.data(), static_cast<std::uint32_t>(a_payload.size()), a_flags, nullptr) == kResultOk;
			};
			api.receiveMessages = [receiveMessages = a_exports.receiveMessages, sockets](Transport::SteamConnectionHandle a_connection, std::size_t a_maximum) {
				std::vector<std::vector<std::uint8_t>> messages;
				if (a_maximum == 0) return messages;

				std::vector<SteamNetworkingMessage_t*> buffer(a_maximum);
				const int count = receiveMessages(sockets, a_connection, reinterpret_cast<void**>(buffer.data()), static_cast<int>(buffer.size()));
				for (int index = 0; index < count; ++index) {
					auto* const message = buffer[static_cast<std::size_t>(index)];
					const auto* data = static_cast<const std::uint8_t*>(message->m_pData);
					messages.emplace_back(data, data + message->m_cbSize);
					message->Release();
				}
				return messages;
			};
			api.runCallbacks = [runCallbacks = a_exports.runCallbacks, sockets] { runCallbacks(sockets); };
			api.pollConnectionEvents = [] { return TakeEvents(false); };
			return api;
		}
#endif
	}

	std::span<const char* const> FriendsAccessorNames()
	{
		return kFriendsAccessors;
	}

	std::span<const char* const> UserAccessorNames()
	{
		return kUserAccessors;
	}

	std::vector<std::string> ProbeGenericInterfaces(const InterfaceProbeInputs& a_inputs)
	{
		std::vector<std::string> lines;
		if (!a_inputs.steamClient) lines.emplace_back("SteamClient is not exported");
		if (!a_inputs.steamUser) lines.emplace_back("SteamAPI_GetHSteamUser is not exported");
		if (!a_inputs.steamPipe) lines.emplace_back("SteamAPI_GetHSteamPipe is not exported");
		if (!a_inputs.genericInterface) lines.emplace_back("SteamAPI_ISteamClient_GetISteamGenericInterface is not exported");
		if (!lines.empty()) return lines;
		void* const client = a_inputs.steamClient();
		if (client == nullptr) {
			lines.emplace_back("SteamClient returned null");
			return lines;
		}
		const auto user = a_inputs.steamUser();
		const auto pipe = a_inputs.steamPipe();
		if (user == 0 || pipe == 0) {
			lines.emplace_back("HSteamUser or HSteamPipe is 0: SteamAPI_Init has not run in this process");
			return lines;
		}

		const auto describe = [&](const char* a_version, void* a_fromAccessor) {
			void* const found = a_inputs.genericInterface(client, user, pipe, a_version);
			std::string line = std::string{ a_version } + (found != nullptr ? " non-null" : " null");
			if (found != nullptr && a_fromAccessor != nullptr) {
				line += found == a_fromAccessor ? ", same as the versioned accessor" : ", differs from the versioned accessor";
			}
			lines.push_back(std::move(line));
		};
		describe(kProbeSocketsVersion, a_inputs.socketsFromAccessor);
		describe(kProbeUtilsVersion, a_inputs.utilsFromAccessor);
		return lines;
	}

	const char* ChooseFirstPresentName(
		std::span<const char* const> a_names,
		const std::function<void*(const char*)>& a_lookup)
	{
		for (const auto* name : a_names) {
			if (a_lookup(name) != nullptr) return name;
		}
		return nullptr;
	}

	SteamApiLoadOptions ReadLoadOptions(const char* a_probeValue)
	{
		SteamApiLoadOptions options;
		options.probe = a_probeValue != nullptr && F4T::DebugSwitch::ParseValue(a_probeValue).value_or(false);
		return options;
	}

	std::string DescribeVersionResource(std::span<const std::uint8_t> a_resource)
	{
		constexpr std::uint32_t kVersionSignature = 0xFEEF04BDu;
		constexpr std::size_t kFixedFileInfoBytes = 16;
		for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= a_resource.size(); offset += sizeof(std::uint32_t)) {
			std::uint32_t signature = 0;
			std::memcpy(&signature, a_resource.data() + offset, sizeof(signature));
			if (signature != kVersionSignature) continue;
			if (offset + kFixedFileInfoBytes > a_resource.size()) return "no version resource";
			std::uint32_t fileVersionMs = 0;
			std::uint32_t fileVersionLs = 0;
			std::memcpy(&fileVersionMs, a_resource.data() + offset + 8, sizeof(fileVersionMs));
			std::memcpy(&fileVersionLs, a_resource.data() + offset + 12, sizeof(fileVersionLs));
			return "file version " + std::to_string(fileVersionMs >> 16) + "." + std::to_string(fileVersionMs & 0xFFFFu) + "." +
				std::to_string(fileVersionLs >> 16) + "." + std::to_string(fileVersionLs & 0xFFFFu);
		}
		return "no version resource";
	}

	SteamApiLoadResult LoadSteamApiFrom(const std::function<void*(const char*)>& a_findExport, const SteamApiLoadOptions& a_options)
	{
		SteamApiLoadResult result;

		std::vector<std::string> missingNetwork;
		std::vector<std::string> missingIdentity;
		std::vector<std::string> missingOptional;
		SteamExports exports;

		std::string socketsVersion;
		const auto socketsAccessor = ResolveAccessor(a_findExport, kSocketsAccessors, socketsVersion, missingNetwork);
		std::string utilsVersion;
		const auto utilsAccessor = ResolveAccessor(a_findExport, kUtilsAccessors, utilsVersion, missingNetwork);
		std::string friendsVersion;
		const auto friendsAccessor = ResolveAccessor(a_findExport, kFriendsAccessors, friendsVersion, missingIdentity);
		std::string userVersion;
		const auto userAccessor = ResolveAccessor(a_findExport, kUserAccessors, userVersion, missingIdentity);

		Resolve(a_findExport, "SteamAPI_ISteamUser_GetSteamID", exports.getSteamId, missingIdentity);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess", exports.initRelay, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus", exports.getRelayStatus, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P", exports.createListenSocketP2P, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_ConnectP2P", exports.connectP2P, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_AcceptConnection", exports.acceptConnection, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_CloseConnection", exports.closeConnection, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_CloseListenSocket", exports.closeListenSocket, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_SendMessageToConnection", exports.sendMessage, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection", exports.receiveMessages, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamNetworkingSockets_RunCallbacks", exports.runCallbacks, missingNetwork);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendRelationship", exports.getFriendRelationship, missingIdentity);

		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendCount", exports.friendCount, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendByIndex", exports.friendByIndex, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendPersonaName", exports.friendPersonaName, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetPersonaName", exports.personaName, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendRichPresence", exports.friendRichPresence, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_RequestFriendRichPresence", exports.requestFriendRichPresence, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_SetRichPresence", exports.setRichPresence, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_ClearRichPresence", exports.clearRichPresence, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamFriends_GetFriendGamePlayed", exports.getFriendGamePlayed, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamUser_GetAuthSessionTicket", exports.getAuthSessionTicket, missingOptional);
		Resolve(a_findExport, "SteamAPI_ISteamUser_CancelAuthTicket", exports.cancelAuthTicket, missingOptional);

		result.missingOptional = missingOptional;

		if (!missingIdentity.empty()) {
			std::vector<std::string> missingRequired = missingIdentity;
			missingRequired.insert(missingRequired.end(), missingNetwork.begin(), missingNetwork.end());
			result.reason = DescribeMissing(missingRequired);
			if (a_options.probe) result.interfaceProbe = ProbeGenericInterfaces(MakeProbeInputs(a_findExport, nullptr, nullptr));
			return result;
		}

		result.foundVersions.push_back("ISteamFriends " + friendsVersion);
		result.foundVersions.push_back("ISteamUser " + userVersion);

		exports.friends = friendsAccessor();
		exports.user = userAccessor();
		if (exports.friends == nullptr || exports.user == nullptr) {
			result.reason = "an interface accessor returned null: the Steam client is not running for this game";
			return result;
		}

		result.mySteamId = exports.getSteamId(exports.user);
		result.foundVersions.push_back(result.mySteamId != 0
			? "steam user " + std::to_string(result.mySteamId)
			: "GetSteamID returned 0: the Steam user is not logged in yet");

		if (userVersion == "SteamUser") {
			exports.getAuthSessionTicketLegacy = reinterpret_cast<LegacyGetAuthSessionTicketFn>(a_findExport("SteamAPI_ISteamUser_GetAuthSessionTicket"));
			exports.getAuthSessionTicket = nullptr;
		}
		result.createAuthTicket = MakeAuthTicketSource(exports);

		const auto friends = exports.friends;
		result.isFriend = [getFriendRelationship = exports.getFriendRelationship, friends](std::uint64_t a_steamId) {
			return a_steamId != 0 && getFriendRelationship(friends, a_steamId) == kFriendRelationshipFriend;
		};
		result.friends.myName = [personaName = exports.personaName, friends] {
			if (personaName == nullptr) return std::string{};
			const auto* name = personaName(friends);
			return name != nullptr ? std::string{ name } : std::string{};
		};
		result.friends.friendCount = [friendCount = exports.friendCount, friends] {
			return friendCount != nullptr ? friendCount(friends, kFriendFlagImmediate) : 0;
		};
		result.friends.friendByIndex = [friendByIndex = exports.friendByIndex, friends](int a_index) {
			return friendByIndex != nullptr ? friendByIndex(friends, a_index, kFriendFlagImmediate) : 0;
		};
		result.friends.friendName = [friendPersonaName = exports.friendPersonaName, friends](std::uint64_t a_steamId) {
			if (friendPersonaName == nullptr) return std::string{};
			const auto* name = friendPersonaName(friends, a_steamId);
			return name != nullptr ? std::string{ name } : std::string{};
		};
		result.friends.richPresence = [friendRichPresence = exports.friendRichPresence, friends](std::uint64_t a_steamId, const char* a_key) {
			if (friendRichPresence == nullptr || a_key == nullptr) return std::string{};
			const auto* value = friendRichPresence(friends, a_steamId, a_key);
			return value != nullptr ? std::string{ value } : std::string{};
		};
		result.friends.requestRichPresence = [requestFriendRichPresence = exports.requestFriendRichPresence, friends](std::uint64_t a_steamId) {
			if (requestFriendRichPresence != nullptr) requestFriendRichPresence(friends, a_steamId);
		};
		result.friends.setRichPresence = [setRichPresence = exports.setRichPresence, friends](const char* a_key, const char* a_value) {
			return setRichPresence != nullptr && a_key != nullptr && a_value != nullptr && setRichPresence(friends, a_key, a_value);
		};
		result.friends.clearRichPresence = [clearRichPresence = exports.clearRichPresence, friends] {
			if (clearRichPresence != nullptr) clearRichPresence(friends);
		};
		result.friends.playingFallout = [getFriendGamePlayed = exports.getFriendGamePlayed, friends](std::uint64_t a_steamId) {
			if (getFriendGamePlayed == nullptr) return true;
			std::uint8_t game[32]{};
			if (!getFriendGamePlayed(friends, a_steamId, game)) return false;
			std::uint64_t gameId = 0;
			std::memcpy(&gameId, game, sizeof(gameId));
			return (gameId & 0xFFFFFFull) == 377160u;
		};

		bool networkFromClient = false;
#if F4T_STEAM_TUNNEL_HAS_GNS_HEADERS
		if (!missingNetwork.empty()) {
			const auto inputs = MakeProbeInputs(a_findExport, nullptr, nullptr);
			exports.sockets = FindGenericInterface(inputs, kProbeSocketsVersion);
			exports.utils = FindGenericInterface(inputs, kProbeUtilsVersion);
			if (exports.sockets != nullptr && exports.utils != nullptr) {
				UseInterfaceMethods(exports);
				socketsVersion = std::string{ kProbeSocketsVersion } + " from GetISteamGenericInterface";
				utilsVersion = std::string{ kProbeUtilsVersion } + " from GetISteamGenericInterface";
				networkFromClient = true;
				missingNetwork.clear();
			}
		}
#endif

		if (!missingNetwork.empty()) {
			result.reason = DescribeMissing(missingNetwork) + ": Steam friends hosting is not available on this game version (Steam sign-in still works)";
			if (a_options.probe) result.interfaceProbe = ProbeGenericInterfaces(MakeProbeInputs(a_findExport, nullptr, nullptr));
			return result;
		}

		result.foundVersions.push_back("ISteamNetworkingSockets " + socketsVersion);
		result.foundVersions.push_back("ISteamNetworkingUtils " + utilsVersion);

		if (!networkFromClient) {
			exports.sockets = socketsAccessor();
			exports.utils = utilsAccessor();
		}
		if (a_options.probe) {
			result.interfaceProbe = ProbeGenericInterfaces(MakeProbeInputs(
				a_findExport,
				socketsVersion.ends_with("_v012") ? exports.sockets : nullptr,
				utilsVersion.ends_with("_v004") ? exports.utils : nullptr));
		}
		if (exports.sockets == nullptr || exports.utils == nullptr) {
			result.reason = "an interface accessor returned null: the Steam client is not running for this game";
			return result;
		}

		InviteBind invite;
		invite.friends = friends;
		std::vector<std::string> missingInvite;
		const auto steamUtils = a_findExport("SteamAPI_SteamUtils_v010");
		if (const auto overlayEnabled = a_findExport("SteamAPI_ISteamUtils_IsOverlayEnabled"); steamUtils != nullptr && overlayEnabled != nullptr) {
			invite.overlayEnabled = [utils = reinterpret_cast<void* (*)()>(steamUtils)(), overlayEnabled] {
				return utils == nullptr || reinterpret_cast<bool (*)(void*)>(overlayEnabled)(utils);
			};
		}
		if (const auto activate = a_findExport("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString")) {
			invite.activate = [activate](void* a_friends, const char* a_connect) {
				reinterpret_cast<void (*)(void*, const char*)>(activate)(a_friends, a_connect);
			};
		} else {
			missingInvite.emplace_back("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString");
		}
		if (const auto activateLobby = a_findExport("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog")) {
			invite.activateLobby = [activateLobby](void* a_friends, std::uint64_t a_lobby) {
				reinterpret_cast<void (*)(void*, std::uint64_t)>(activateLobby)(a_friends, a_lobby);
			};
		} else {
			missingInvite.emplace_back("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
		}
		if (const auto registerCallback = a_findExport("SteamAPI_RegisterCallback")) {
			invite.registerCallback = [registerCallback](void* a_callback, int a_id) {
				reinterpret_cast<void (*)(void*, int)>(registerCallback)(a_callback, a_id);
			};
		} else {
			missingInvite.emplace_back("SteamAPI_RegisterCallback");
		}
		if (const auto unregisterCallback = a_findExport("SteamAPI_UnregisterCallback")) {
			invite.unregisterCallback = [unregisterCallback](void* a_callback) {
				reinterpret_cast<void (*)(void*)>(unregisterCallback)(a_callback);
			};
		} else {
			missingInvite.emplace_back("SteamAPI_UnregisterCallback");
		}
		if (const auto registerCallResult = a_findExport("SteamAPI_RegisterCallResult")) {
			invite.registerCallResult = [registerCallResult](void* a_callback, std::uint64_t a_call) {
				reinterpret_cast<void (*)(void*, std::uint64_t)>(registerCallResult)(a_callback, a_call);
			};
		}
		if (const auto unregisterCallResult = a_findExport("SteamAPI_UnregisterCallResult")) {
			invite.unregisterCallResult = [unregisterCallResult](void* a_callback, std::uint64_t a_call) {
				reinterpret_cast<void (*)(void*, std::uint64_t)>(unregisterCallResult)(a_callback, a_call);
			};
		}
		BindInvite(invite);
		result.missingOptional.insert(result.missingOptional.end(), missingInvite.begin(), missingInvite.end());

#if F4T_STEAM_TUNNEL_HAS_GNS_HEADERS
		ResolveMatchmaking(a_findExport, result);
		if (!result.missingOptional.empty()) {
			F4T::DebugLogging::Log(kSessionLog, "steam optional exports missing: {}", JoinNames(result.missingOptional));
		}
		result.api = BuildApi(exports);
		result.guestConnectionEvents = [] { return TakeEvents(true); };
		result.available = true;
#else
		result.reason = "the Steam connection-status callback needs the pinned GameNetworkingSockets headers, which are not on this build's include path (CO-BETA/ThirdParty/GameNetworkingSockets-src is not initialized)";
#endif
		return result;
	}

	SteamApiLoadResult LoadSteamApi()
	{
		const auto module = FindLoadedModule(kSteamDllName);
		if (module == nullptr) {
			SteamApiLoadResult result;
			result.reason = std::string{ kSteamDllName } + " is not loaded in this process: the game was not started through Steam";
			return result;
		}

		const std::function<void*(const char*)> lookup = [module](const char* a_name) {
			return FindExport(module, a_name);
		};
		auto result = LoadSteamApiFrom(lookup, ReadLoadOptions(std::getenv(kProbeVariable)));
		result.foundVersions.insert(result.foundVersions.begin(), std::string{ kSteamDllName } + " is loaded");
		result.moduleVersion = DescribeModuleVersion(module);
		return result;
	}
}
