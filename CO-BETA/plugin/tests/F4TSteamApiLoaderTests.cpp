#include "F4TSteamApiLoader.h"
#include "F4TSteamInvite.h"
#include "F4TSteamTunnelService.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace RecordedLog
{
	std::mutex g_mutex;
	std::vector<std::string> g_lines;

	void Clear()
	{
		std::lock_guard lock{ g_mutex };
		g_lines.clear();
	}

	bool Contains(std::string_view a_text)
	{
		std::lock_guard lock{ g_mutex };
		for (const auto& line : g_lines) {
			if (line.find(a_text) != std::string::npos) return true;
		}
		return false;
	}

	std::vector<std::string> Lines()
	{
		std::lock_guard lock{ g_mutex };
		return g_lines;
	}
}

namespace F4T::DebugLogging
{
	bool IsEnabled()
	{
		return true;
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		std::lock_guard lock{ RecordedLog::g_mutex };
		RecordedLog::g_lines.push_back(std::string{ a_component } + ": " + std::string{ a_message });
	}
}

namespace
{
	constexpr const char* kOrdered[] = { "v012", "v011", "v010", "v009" };

	std::function<void*(const char*)> Present(std::initializer_list<std::string_view> a_present)
	{
		return [present = std::vector<std::string_view>{ a_present }](const char* a_name) -> void* {
			for (const auto name : present) {
				if (name == a_name) return const_cast<char*>("");
			}
			return nullptr;
		};
	}

	bool Chose(const char* a_chosen, const char* a_expected)
	{
		return a_chosen != nullptr && std::strcmp(a_chosen, a_expected) == 0;
	}

	int g_sockets = 0;
	int g_utils = 0;
	int g_client = 0;

	F4T::Steam::InterfaceProbeInputs FakeProbe(void* a_sockets, void* a_utils)
	{
		F4T::Steam::InterfaceProbeInputs inputs;
		inputs.steamClient = [] { return static_cast<void*>(&g_client); };
		inputs.steamUser = [] { return std::int32_t{ 1 }; };
		inputs.steamPipe = [] { return std::int32_t{ 2 }; };
		inputs.genericInterface = [](void* a_client, std::int32_t a_user, std::int32_t a_pipe, const char* a_version) -> void* {
			if (a_client != &g_client || a_user != 1 || a_pipe != 2) return nullptr;
			if (std::strcmp(a_version, "SteamNetworkingSockets012") == 0) return &g_sockets;
			if (std::strcmp(a_version, "SteamNetworkingUtils004") == 0) return &g_utils;
			return nullptr;
		};
		inputs.socketsFromAccessor = a_sockets;
		inputs.utilsFromAccessor = a_utils;
		return inputs;
	}

	bool Lines(const std::vector<std::string>& a_lines, std::initializer_list<std::string_view> a_expected)
	{
		if (a_lines.size() != a_expected.size()) return false;
		auto line = a_lines.begin();
		for (const auto expected : a_expected) {
			if (*line++ != expected) return false;
		}
		return true;
	}

	int g_friendsAccessorCalls = 0;
	int g_userAccessorCalls = 0;
	int g_steamIdCalls = 0;
	std::uint8_t g_friendsObject = 0;
	std::uint8_t g_userObject = 0;
	constexpr std::uint64_t kFakeSteamId = 76561198000000001ull;
	constexpr std::uint64_t kFakeFriend = 76561198000000042ull;
	constexpr std::uint64_t kFakeStranger = 76561198000000043ull;

	void* FakeFriendsAccessor()
	{
		++g_friendsAccessorCalls;
		return &g_friendsObject;
	}

	void* FakeUserAccessor()
	{
		++g_userAccessorCalls;
		return &g_userObject;
	}

	std::uint64_t FakeGetSteamId(void*)
	{
		++g_steamIdCalls;
		return kFakeSteamId;
	}

	int FakeGetFriendRelationship(void*, std::uint64_t a_steamId)
	{
		return a_steamId == kFakeFriend ? 3 : 0;
	}

	std::uint32_t FakeTicketLegacy(void*, void* a_ticket, int, std::uint32_t* a_size)
	{
		std::memset(a_ticket, 0xAB, 4);
		*a_size = 4;
		return 7;
	}

	std::uint32_t FakeTicket(void*, void* a_ticket, int, std::uint32_t* a_size, const void*)
	{
		std::memset(a_ticket, 0xCD, 2);
		*a_size = 2;
		return 9;
	}

	void FakeCancelAuthTicket(void*, std::uint32_t) {}

	using FakeTable = std::map<std::string, void*>;

	std::function<void*(const char*)> Lookup(const FakeTable& a_table)
	{
		return [a_table](const char* a_name) -> void* {
			const auto found = a_table.find(a_name);
			return found == a_table.end() ? nullptr : found->second;
		};
	}

	FakeTable Identity(const char* a_friendsName, const char* a_userName, void* a_ticket)
	{
		FakeTable table;
		table[a_friendsName] = reinterpret_cast<void*>(&FakeFriendsAccessor);
		table[a_userName] = reinterpret_cast<void*>(&FakeUserAccessor);
		table["SteamAPI_ISteamUser_GetSteamID"] = reinterpret_cast<void*>(&FakeGetSteamId);
		table["SteamAPI_ISteamFriends_GetFriendRelationship"] = reinterpret_cast<void*>(&FakeGetFriendRelationship);
		table["SteamAPI_ISteamUser_GetAuthSessionTicket"] = a_ticket;
		table["SteamAPI_ISteamUser_CancelAuthTicket"] = reinterpret_cast<void*>(&FakeCancelAuthTicket);
		return table;
	}

	void ResetCounters()
	{
		g_friendsAccessorCalls = 0;
		g_userAccessorCalls = 0;
		g_steamIdCalls = 0;
	}

	constexpr std::uint64_t kHostId = 76561198000000007ull;
	constexpr std::uint64_t kLobbyId = (1ull << 56) | (8ull << 52) | (0x40000ull << 32) | 1234567ull;
	constexpr std::uint64_t kFriendId = 76561198000000042ull;
	std::uint8_t g_mmObject = 0;
	int g_createType = -1;
	int g_createMax = -1;
	std::uint64_t g_joinedLobby = 0;
	std::uint64_t g_leftLobby = 0;
	std::map<std::string, std::string> g_lobbyData;
	std::vector<std::uint64_t> g_registeredCalls;
	std::vector<std::uint64_t> g_unregisteredCalls;
	std::vector<int> g_registeredCallbacks;
	std::vector<void*> g_unregisteredCallbacks;

	void* FakeMatchmakingAccessor()
	{
		return &g_mmObject;
	}

	void FakeNoop() {}

	std::uint64_t FakeCreateLobby(void* a_self, int a_type, int a_max)
	{
		assert(a_self == &g_mmObject);
		g_createType = a_type;
		g_createMax = a_max;
		return 4242;
	}

	std::uint64_t FakeJoinLobby(void* a_self, std::uint64_t a_lobby)
	{
		assert(a_self == &g_mmObject);
		g_joinedLobby = a_lobby;
		return 4343;
	}

	void FakeLeaveLobby(void*, std::uint64_t a_lobby)
	{
		g_leftLobby = a_lobby;
	}

	bool FakeSetLobbyData(void*, std::uint64_t, const char* a_key, const char* a_value)
	{
		g_lobbyData[a_key] = a_value;
		return true;
	}

	const char* FakeGetLobbyData(void*, std::uint64_t, const char* a_key)
	{
		const auto found = g_lobbyData.find(a_key);
		return found == g_lobbyData.end() ? "" : found->second.c_str();
	}

	std::uint64_t FakeGetLobbyOwner(void*, std::uint64_t)
	{
		return kHostId;
	}

	int FakeGetNumLobbyMembers(void*, std::uint64_t)
	{
		return 3;
	}

	bool FakeSetLobbyMemberLimit(void*, std::uint64_t, int)
	{
		return true;
	}

	bool FakeSetLobbyJoinable(void*, std::uint64_t, bool)
	{
		return true;
	}

	void FakeRegisterCallResult(void*, std::uint64_t a_call)
	{
		g_registeredCalls.push_back(a_call);
	}

	void FakeUnregisterCallResult(void*, std::uint64_t a_call)
	{
		g_unregisteredCalls.push_back(a_call);
	}

	void FakeRegisterCallback(void*, int a_id)
	{
		g_registeredCallbacks.push_back(a_id);
	}

	void FakeUnregisterCallback(void* a_callback)
	{
		g_unregisteredCallbacks.push_back(a_callback);
	}

	void* FakeSteamClient()
	{
		return &g_client;
	}

	std::int32_t FakeHandle()
	{
		return 1;
	}

	void* FakeGenericMatchmaking(void*, std::int32_t, std::int32_t, const char* a_version)
	{
		return std::strcmp(a_version, "SteamMatchMaking009") == 0 ? &g_mmObject : nullptr;
	}

	FakeTable FullNetworkTable()
	{
		auto table = Identity("SteamAPI_SteamFriends_v017", "SteamAPI_SteamUser_v021", reinterpret_cast<void*>(&FakeTicket));
		void* const noop = reinterpret_cast<void*>(&FakeNoop);
		table["SteamAPI_SteamNetworkingSockets_SteamAPI_v012"] = reinterpret_cast<void*>(&FakeFriendsAccessor);
		table["SteamAPI_SteamNetworkingUtils_SteamAPI_v004"] = reinterpret_cast<void*>(&FakeUserAccessor);
		for (const char* name : {
				 "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess", "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus",
				 "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P", "SteamAPI_ISteamNetworkingSockets_ConnectP2P",
				 "SteamAPI_ISteamNetworkingSockets_AcceptConnection", "SteamAPI_ISteamNetworkingSockets_CloseConnection",
				 "SteamAPI_ISteamNetworkingSockets_CloseListenSocket", "SteamAPI_ISteamNetworkingSockets_SendMessageToConnection",
				 "SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection", "SteamAPI_ISteamNetworkingSockets_RunCallbacks" }) {
			table[name] = noop;
		}
		table["SteamAPI_RegisterCallback"] = reinterpret_cast<void*>(&FakeRegisterCallback);
		table["SteamAPI_UnregisterCallback"] = reinterpret_cast<void*>(&FakeUnregisterCallback);
		table["SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog"] = noop;
		return table;
	}

	FakeTable MatchmakingTable()
	{
		auto table = FullNetworkTable();
		table["SteamAPI_SteamMatchmaking_v009"] = reinterpret_cast<void*>(&FakeMatchmakingAccessor);
		table["SteamAPI_ISteamMatchmaking_CreateLobby"] = reinterpret_cast<void*>(&FakeCreateLobby);
		table["SteamAPI_ISteamMatchmaking_JoinLobby"] = reinterpret_cast<void*>(&FakeJoinLobby);
		table["SteamAPI_ISteamMatchmaking_LeaveLobby"] = reinterpret_cast<void*>(&FakeLeaveLobby);
		table["SteamAPI_ISteamMatchmaking_SetLobbyData"] = reinterpret_cast<void*>(&FakeSetLobbyData);
		table["SteamAPI_ISteamMatchmaking_GetLobbyData"] = reinterpret_cast<void*>(&FakeGetLobbyData);
		table["SteamAPI_ISteamMatchmaking_GetLobbyOwner"] = reinterpret_cast<void*>(&FakeGetLobbyOwner);
		table["SteamAPI_ISteamMatchmaking_GetNumLobbyMembers"] = reinterpret_cast<void*>(&FakeGetNumLobbyMembers);
		table["SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit"] = reinterpret_cast<void*>(&FakeSetLobbyMemberLimit);
		table["SteamAPI_ISteamMatchmaking_SetLobbyJoinable"] = reinterpret_cast<void*>(&FakeSetLobbyJoinable);
		table["SteamAPI_RegisterCallResult"] = reinterpret_cast<void*>(&FakeRegisterCallResult);
		table["SteamAPI_UnregisterCallResult"] = reinterpret_cast<void*>(&FakeUnregisterCallResult);
		return table;
	}

	constexpr bool kHasGnsHeaders =
#if __has_include(<steam/steamnetworkingsockets.h>)
		true;
#else
		false;
#endif

	bool Has(const std::vector<std::string>& a_names, std::string_view a_name)
	{
		for (const auto& name : a_names) {
			if (name == a_name) return true;
		}
		return false;
	}

	std::vector<std::string> g_connects;
	std::vector<std::uint64_t> g_lobbyJoins;
	std::vector<std::string> g_failures;

	void RecordConnect(const char* a_connect)
	{
		g_connects.emplace_back(a_connect ? a_connect : "<null>");
	}

	void RecordLobbyJoin(std::uint64_t a_lobby)
	{
		g_lobbyJoins.push_back(a_lobby);
	}

	void RecordFailure(const char* a_message)
	{
		g_failures.emplace_back(a_message ? a_message : "<null>");
	}

	void ResetHandlers()
	{
		g_connects.clear();
		g_lobbyJoins.clear();
		g_failures.clear();
		F4T::Steam::SetJoinConnectHandler(&RecordConnect);
		F4T::Steam::SetLobbyJoinHandler(&RecordLobbyJoin);
		F4T::Steam::SetJoinFailureHandler(&RecordFailure);
	}

	void CheckLaunchArgs()
	{
		using F4T::Steam::ParseLaunchArgs;

		auto lobby = ParseLaunchArgs("Fallout4.exe +connect_lobby 109775241234567890");
		assert(lobby.lobbyId == 109775241234567890ull);
		assert(!lobby.connect);

		auto connect = ParseLaunchArgs("+connect 39734273");
		assert(connect.connect == "39734273");
		assert(!connect.lobbyId);

		auto both = ParseLaunchArgs("\"C:\\Games\\Fallout 4\\Fallout4.exe\" +connect 55 +connect_lobby 77");
		assert(both.connect == "55");
		assert(both.lobbyId == 77ull);

		assert(!ParseLaunchArgs("+connect_lobbyX 123").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby2 123").lobbyId);
		assert(!ParseLaunchArgs("x+connect_lobby 123").lobbyId);
		assert(!ParseLaunchArgs("+connectX 123").connect);
		assert(!ParseLaunchArgs("-connect_lobby 123").lobbyId);
		assert(!ParseLaunchArgs("+CONNECT_LOBBY 123").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby=123").lobbyId);

		assert(!ParseLaunchArgs("").lobbyId && !ParseLaunchArgs("").connect);
		assert(!ParseLaunchArgs("   \t ").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby").lobbyId);
		assert(!ParseLaunchArgs("+connect").connect);
		assert(!ParseLaunchArgs("+connect_lobby +connect 5").lobbyId);
		assert(ParseLaunchArgs("+connect_lobby +connect 5").connect == "5");

		assert(ParseLaunchArgs("+connect_lobby 18446744073709551615").lobbyId == 18446744073709551615ull);
		assert(!ParseLaunchArgs("+connect_lobby 18446744073709551616").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby 123456789012345678901").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby 12a4").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby -5").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby 0").lobbyId);
		assert(!ParseLaunchArgs("+connect_lobby 0123").lobbyId);
		assert(ParseLaunchArgs("+connect_lobby 12 3").lobbyId == 12ull);

		assert(ParseLaunchArgs("+connect_lobby \"4567\"").lobbyId == 4567ull);
		assert(ParseLaunchArgs("\"+connect_lobby\" \"4567\"").lobbyId == 4567ull);
		assert(ParseLaunchArgs("+connect \"hello world\"").connect == "hello world");
		assert(!ParseLaunchArgs("+connect \"\"").connect);

		assert(ParseLaunchArgs("+connect_lobby 11 +connect_lobby 22").lobbyId == 11ull);
		assert(!ParseLaunchArgs("+connect_lobby abc +connect_lobby 22").lobbyId);
		assert(ParseLaunchArgs("+connect 1 +connect 2").connect == "1");
		assert(!ParseLaunchArgs("+connect \"\" +connect 2").connect);

		assert(ParseLaunchArgs("-windowed +foo bar +connect_lobby 9 --last").lobbyId == 9ull);
		assert(!ParseLaunchArgs("+connect_lobby \"123").lobbyId);
		assert(!ParseLaunchArgs("+connect \"123 +connect_lobby 5").lobbyId);

		const std::string oversized(300, '7');
		assert(!ParseLaunchArgs("+connect " + oversized).connect);
		assert(ParseLaunchArgs("+connect " + std::string(255, '7')).connect->size() == 255);
		assert(!ParseLaunchArgs(std::string(40000, ' ') + "+connect_lobby 5").lobbyId);
		assert(!ParseLaunchArgs(std::string{ "+connect a" } + '\x01' + "b").connect);
	}

	void CheckValidateConnectString()
	{
		using F4T::Steam::ValidateConnectString;

		const auto code = ValidateConnectString("39734273");
		assert(code && code->friendCode && code->value == 39734273ull && code->steamId == 76561197960265728ull + 39734273ull);
		assert(ValidateConnectString("1"));
		assert(ValidateConnectString("4294967295"));
		const auto individual = ValidateConnectString("76561198000000042");
		assert(individual && !individual->friendCode && individual->steamId == 76561198000000042ull);

		assert(!ValidateConnectString(""));
		assert(!ValidateConnectString("0"));
		assert(!ValidateConnectString("0123"));
		assert(!ValidateConnectString("00"));
		assert(!ValidateConnectString("12 "));
		assert(!ValidateConnectString(" 12"));
		assert(!ValidateConnectString("1 2"));
		assert(!ValidateConnectString("12\n"));
		assert(!ValidateConnectString("+12"));
		assert(!ValidateConnectString("-12"));
		assert(!ValidateConnectString("1e5"));
		assert(!ValidateConnectString("0x10"));
		assert(!ValidateConnectString("Ada"));
		assert(!ValidateConnectString("ada the host"));
		assert(!ValidateConnectString("auto"));
		assert(!ValidateConnectString(std::string_view{ "12\0" "34", 5 }));
		assert(!ValidateConnectString(std::string_view{ "\0", 1 }));
		assert(!ValidateConnectString("123456789012345678901"));
		assert(!ValidateConnectString(std::string(300, '9')));
		assert(!ValidateConnectString("18446744073709551616"));
		assert(!ValidateConnectString("99999999999999999999"));
		assert(!ValidateConnectString("4294967296"));
		assert(!ValidateConnectString("18446744073709551615"));

		assert(!ValidateConnectString(std::to_string(kLobbyId)));
		assert(!ValidateConnectString(std::to_string((1ull << 56) | (3ull << 52) | (1ull << 32) | 5ull)));
		assert(!ValidateConnectString(std::to_string((2ull << 56) | (1ull << 52) | (1ull << 32) | 5ull)));
		assert(!ValidateConnectString(std::to_string((1ull << 56) | (1ull << 52) | (2ull << 32) | 5ull)));
		assert(!ValidateConnectString(std::to_string((1ull << 56) | (1ull << 52) | (1ull << 32))));

		assert(F4T::Steam::IsLobbySteamId(kLobbyId));
		assert(!F4T::Steam::IsLobbySteamId((1ull << 56) | (8ull << 52) | (0x20000ull << 32) | 9ull));
		assert(F4T::Steam::IsLobbySteamId((1ull << 56) | (8ull << 52) | (0x60000ull << 32) | 9ull));
		assert(!F4T::Steam::IsLobbySteamId((1ull << 56) | (8ull << 52) | (0x80000ull << 32) | 9ull));
		assert(!F4T::Steam::IsLobbySteamId((1ull << 56) | (8ull << 52) | 9ull));
		assert(!F4T::Steam::IsLobbySteamId((2ull << 56) | (8ull << 52) | (0x40000ull << 32) | 9ull));
		assert(!F4T::Steam::IsLobbySteamId((1ull << 56) | (7ull << 52) | (0x40000ull << 32) | 9ull));
		assert(!F4T::Steam::IsLobbySteamId(0));
		assert(!F4T::Steam::IsLobbySteamId(kHostId));
		assert(F4T::Steam::IsIndividualSteamId(kHostId));
		assert(!F4T::Steam::IsIndividualSteamId(kLobbyId));
	}

	void CheckCallbackAbi()
	{
		using namespace F4T::Steam;
		static_assert(sizeof(GameLobbyJoinRequest) == 16 && sizeof(LobbyCreatedResult) == 12 && sizeof(LobbyEnterResult) == 20);
		assert(kGameLobbyJoinRequestedCallback == 333 && kLobbyEnterCallback == 504 && kLobbyCreatedCallback == 513);

		ResetHandlers();
		RecordedLog::Clear();

		RichPresenceJoinRequest request{};
		std::memset(request.connect, '1', sizeof(request.connect));
		RunInviteCallbackForTest(&request);
		assert(g_connects.empty());
		assert(RecordedLog::Contains("rich presence join callback"));
		for (const auto& line : RecordedLog::Lines()) assert(line.size() < 200);

		RichPresenceJoinRequest midNul{};
		std::memcpy(midNul.connect, "12\0" "garbage", 10);
		RunInviteCallbackForTest(&midNul);
		assert(g_connects.size() == 1 && g_connects.front() == "12");

		RichPresenceJoinRequest name{};
		std::memcpy(name.connect, "Ada", 3);
		RunInviteCallbackForTest(&name);
		assert(g_connects.size() == 1);
		assert(RecordedLog::Contains("connect rejected"));

		assert(g_failures.size() == 2);
		for (const auto& failure : g_failures) assert(failure == "That Steam invite is not valid.");

		RichPresenceJoinRequest leading{};
		std::memcpy(leading.connect, "0042", 4);
		RunInviteCallbackForTest(&leading);
		assert(g_connects.size() == 1);

		RichPresenceJoinRequest id{};
		const auto text = std::to_string(kHostId);
		std::memcpy(id.connect, text.c_str(), text.size());
		RunInviteCallbackForTest(&id);
		assert(g_connects.size() == 2 && g_connects.back() == text);

		SetJoinConnectHandler(nullptr);
		g_failures.clear();
		RecordedLog::Clear();
		RunInviteCallbackForTest(&id);
		assert(g_connects.size() == 2);
		assert(g_failures.size() == 1 && g_failures.front() == "Joining is not ready yet. Try the invite again in a moment.");
		assert(RecordedLog::Contains("no join handler is set"));
		SetJoinConnectHandler(&RecordConnect);
		g_failures.clear();

		QueueLaunchJoin(ParseLaunchArgs("+connect_lobby " + std::to_string(kHostId)));
		assert(!FireLaunchJoin());
		assert(g_failures.size() == 1 && g_failures.front() == "That Steam lobby invite is not valid.");
		assert(RecordedLog::Contains("is not a lobby id"));
		g_failures.clear();
		RecordedLog::Clear();
		SetLobbyJoinHandler(nullptr);
		QueueLaunchJoin(ParseLaunchArgs("+connect_lobby " + std::to_string(kLobbyId)));
		assert(!FireLaunchJoin());
		assert(g_failures.size() == 1 && g_failures.front() == "Joining is not ready yet. Try the invite again in a moment.");
		assert(RecordedLog::Contains("no lobby join handler is set"));
		assert(!RecordedLog::Contains("is not a lobby id"));
		SetLobbyJoinHandler(&RecordLobbyJoin);
		g_failures.clear();
		RecordedLog::Clear();

		RichPresenceJoinRequest lobbyAsConnect{};
		const auto lobbyText = std::to_string(kLobbyId);
		std::memcpy(lobbyAsConnect.connect, lobbyText.c_str(), lobbyText.size());
		RunInviteCallbackForTest(&lobbyAsConnect);
		assert(g_connects.size() == 2);

		RunInviteCallbackForTest(nullptr);
		RichPresenceJoinRequest empty{};
		RunInviteCallbackForTest(&empty);
		assert(g_connects.size() == 2);

		GameLobbyJoinRequest join{ kLobbyId, kFriendId };
		RunLobbyJoinCallbackForTest(&join);
		assert(g_lobbyJoins.size() == 1 && g_lobbyJoins.front() == kLobbyId);
		GameLobbyJoinRequest notLobby{ kHostId, kFriendId };
		RunLobbyJoinCallbackForTest(&notLobby);
		GameLobbyJoinRequest zero{};
		RunLobbyJoinCallbackForTest(&zero);
		RunLobbyJoinCallbackForTest(nullptr);
		assert(g_lobbyJoins.size() == 1);
		assert(RecordedLog::Contains("lobby join requested lobby=" + std::to_string(kLobbyId)));
		assert(RecordedLog::Contains("is not a lobby id"));

		std::vector<std::uint64_t> registered;
		std::vector<std::uint64_t> unregistered;
		int registeredIds[2]{};
		int idCount = 0;
		int unregisteredCallbacks = 0;
		BindInvite({
			nullptr,
			{},
			[&registeredIds, &idCount](void*, int a_id) { registeredIds[idCount++] = a_id; },
			[&unregisteredCallbacks](void*) { ++unregisteredCallbacks; },
			{},
			{},
			[&registered](void*, std::uint64_t a_call) { registered.push_back(a_call); },
			[&unregistered](void*, std::uint64_t a_call) { unregistered.push_back(a_call); },
		});
		assert(idCount == 2 && registeredIds[0] == 333 && registeredIds[1] == 337);
		assert((RegisteredCallbackIds() == std::vector<int>{ 333, 337 }));
		assert(LastRegisteredCallbackId() == 337);

		struct Seen
		{
			int calls{};
			bool ok{};
			int result{};
			std::uint64_t lobby{};
			std::uint32_t response{};
		};
		Seen created;
		assert(AwaitLobbyCreated(77, [&created](bool a_ok, int a_result, std::uint64_t a_lobby) { created = { 1, a_ok, a_result, a_lobby, 0 }; }));
		assert(registered.size() == 1 && registered.back() == 77);
		LobbyCreatedResult createdResult{ 1, kLobbyId };
		RunLobbyCreatedForTest(&createdResult, false, 78);
		assert(created.calls == 0);
		RunLobbyCreatedForTest(&createdResult, false, 77);
		assert(created.calls == 1 && created.ok && created.result == 1 && created.lobby == kLobbyId);
		RunLobbyCreatedForTest(&createdResult, false, 77);
		assert(created.calls == 1);

		assert(AwaitLobbyCreated(80, [&created](bool a_ok, int a_result, std::uint64_t a_lobby) { created = { created.calls + 1, a_ok, a_result, a_lobby, 0 }; }));
		RunLobbyCreatedForTest(nullptr, true, 80);
		assert(created.calls == 2 && !created.ok);
		assert(AwaitLobbyCreated(81, [&created](bool a_ok, int a_result, std::uint64_t a_lobby) { created = { created.calls + 1, a_ok, a_result, a_lobby, 0 }; }));
		LobbyCreatedResult limited{ 25, kLobbyId };
		RunLobbyCreatedForTest(&limited, false, 81);
		assert(created.calls == 3 && !created.ok && created.result == 25);

		Seen entered;
		assert(AwaitLobbyEntered(90, [&entered](bool a_ok, std::uint32_t a_response, std::uint64_t a_lobby) { entered = { 1, a_ok, 0, a_lobby, a_response }; }));
		LobbyEnterResult enterResult{ kLobbyId, 0, false, 1 };
		RunLobbyEnteredForTest(&enterResult, false, 90);
		assert(entered.calls == 1 && entered.ok && entered.lobby == kLobbyId && entered.response == 1);
		assert(AwaitLobbyEntered(91, [&entered](bool a_ok, std::uint32_t a_response, std::uint64_t a_lobby) { entered = { entered.calls + 1, a_ok, 0, a_lobby, a_response }; }));
		LobbyEnterResult full{ kLobbyId, 0, false, 5 };
		RunLobbyEnteredForTest(&full, false, 91);
		assert(entered.calls == 2 && !entered.ok && entered.response == 5);

		int firstDone = 0;
		int secondDone = 0;
		assert(AwaitLobbyCreated(100, [&firstDone](bool a_ok, int, std::uint64_t) { firstDone += a_ok ? 1 : 100; }));
		assert(AwaitLobbyCreated(101, [&secondDone](bool a_ok, int, std::uint64_t) { secondDone += a_ok ? 1 : 100; }));
		assert(!AwaitLobbyCreated(101, [](bool, int, std::uint64_t) {}));
		RunLobbyCreatedForTest(&createdResult, false, 101);
		assert(firstDone == 0 && secondDone == 1);
		RunLobbyCreatedForTest(&createdResult, false, 100);
		assert(firstDone == 1 && secondDone == 1);
		assert(unregistered.empty());

		int enteredFirst = 0;
		int enteredSecond = 0;
		assert(AwaitLobbyEntered(110, [&enteredFirst](bool a_ok, std::uint32_t, std::uint64_t) { enteredFirst += a_ok ? 1 : 100; }));
		assert(AwaitLobbyEntered(111, [&enteredSecond](bool a_ok, std::uint32_t, std::uint64_t) { enteredSecond += a_ok ? 1 : 100; }));
		RunLobbyEnteredForTest(&enterResult, false, 110);
		RunLobbyEnteredForTest(&enterResult, false, 111);
		assert(enteredFirst == 1 && enteredSecond == 1);

		assert(AwaitLobbyEntered(92, [](bool, std::uint32_t, std::uint64_t) {}));
		assert(AwaitLobbyEntered(93, [](bool, std::uint32_t, std::uint64_t) {}));
		assert(unregistered.empty());

		UnbindInvite();
		assert(unregisteredCallbacks == 2);
		assert(unregistered.size() == 2 && unregistered.front() == 92 && unregistered.back() == 93);
		assert(RegisteredCallbackIds().empty());
		assert(!AwaitLobbyCreated(5, [](bool, int, std::uint64_t) {}));
		assert(RecordedLog::Contains("steam invite callbacks registered ids=333,337"));
		assert(RecordedLog::Contains("steam invite callbacks unregistered"));
		UnbindInvite();
		assert(unregisteredCallbacks == 2);
	}

	void CheckHandlerSwapIsSafe()
	{
		using namespace F4T::Steam;
		ResetHandlers();
		std::atomic<bool> stop{ false };
		std::atomic<int> seen{ 0 };
		static std::atomic<int>* counter = nullptr;
		counter = &seen;
		auto count = [](const char*) { if (counter != nullptr) counter->fetch_add(1); };
		RichPresenceJoinRequest request{};
		std::memcpy(request.connect, "42", 2);
		std::vector<std::thread> threads;
		threads.emplace_back([&] {
			while (!stop.load()) {
				SetJoinConnectHandler(count);
				SetJoinConnectHandler(nullptr);
			}
		});
		threads.emplace_back([&] {
			while (!stop.load()) RunInviteCallbackForTest(&request);
		});
		threads.emplace_back([&] {
			while (!stop.load()) {
				BindInvite({ nullptr, {}, [](void*, int) {}, [](void*) {}, {}, {}, {}, {} });
				UnbindInvite();
			}
		});
		threads.emplace_back([&] {
			while (!stop.load()) (void)LastRegisteredCallbackId();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds{ 150 });
		stop.store(true);
		for (auto& thread : threads) thread.join();
		counter = nullptr;
		SetJoinConnectHandler(nullptr);
		UnbindInvite();
	}

	void CheckMatchmakingLoad()
	{
		using namespace F4T::Steam;

		g_registeredCalls.clear();
		g_registeredCallbacks.clear();
		RecordedLog::Clear();
		const auto loaded = LoadSteamApiFrom(Lookup(MatchmakingTable()), {});
		if (!kHasGnsHeaders) return;
		assert(loaded.available);
		assert(loaded.matchmakingAvailable);
		assert(loaded.matchmakingMissing.empty());
		assert(Has(loaded.foundVersions, "ISteamMatchmaking SteamAPI_SteamMatchmaking_v009"));
		assert(g_registeredCallbacks == (std::vector<int>{ 333, 337 }));

		struct Seen
		{
			int calls{};
			bool ok{};
			std::uint64_t lobby{};
		};
		Seen created;
		loaded.matchmaking.createLobby(4, [&created](bool a_ok, int, std::uint64_t a_lobby) { created = { 1, a_ok, a_lobby }; });
		assert(g_createType == 1 && g_createMax == 4);
		assert(g_registeredCalls.size() == 1 && g_registeredCalls.back() == 4242);
		LobbyCreatedResult createdResult{ 1, kLobbyId };
		RunLobbyCreatedForTest(&createdResult, false, 4242);
		assert(created.calls == 1 && created.ok && created.lobby == kLobbyId);

		Seen entered;
		loaded.matchmaking.joinLobby(kLobbyId, [&entered](bool a_ok, std::uint32_t, std::uint64_t a_lobby) { entered = { 1, a_ok, a_lobby }; });
		assert(g_joinedLobby == kLobbyId);
		assert(g_registeredCalls.back() == 4343);
		LobbyEnterResult enterResult{ kLobbyId, 0, false, 1 };
		RunLobbyEnteredForTest(&enterResult, false, 4343);
		assert(entered.calls == 1 && entered.ok);

		assert(loaded.matchmaking.setLobbyData(kLobbyId, "co_build", "0.1.0"));
		assert(loaded.matchmaking.getLobbyData(kLobbyId, "co_build") == "0.1.0");
		assert(loaded.matchmaking.getLobbyData(kLobbyId, "absent").empty());
		assert(loaded.matchmaking.lobbyOwner(kLobbyId) == kHostId);
		assert(loaded.matchmaking.memberCount(kLobbyId) == 3);
		assert(loaded.matchmaking.setMemberLimit(kLobbyId, 4));
		assert(loaded.matchmaking.setJoinable(kLobbyId, true));
		loaded.matchmaking.leaveLobby(kLobbyId);
		assert(g_leftLobby == kLobbyId);
		assert(!RecordedLog::Contains("matchmaking unavailable"));

		Seen refused;
		g_registeredCalls.clear();
		loaded.matchmaking.joinLobby(kLobbyId, [&refused](bool a_ok, std::uint32_t, std::uint64_t) { refused = { 1, a_ok, 0 }; });
		LobbyEnterResult denied{ kLobbyId, 0, false, 5 };
		RunLobbyEnteredForTest(&denied, false, 4343);
		assert(refused.calls == 1 && !refused.ok);
		UnbindInvite();

		g_leftLobby = 0;
		RecordedLog::Clear();
		Seen unawaited;
		loaded.matchmaking.joinLobby(kLobbyId, [&unawaited](bool a_ok, std::uint32_t, std::uint64_t) { unawaited = { 1, a_ok, 0 }; });
		assert(unawaited.calls == 1 && !unawaited.ok);
		assert(g_leftLobby == kLobbyId);
		assert(RecordedLog::Contains("lobby join result could not be awaited"));
		Seen uncreated;
		loaded.matchmaking.createLobby(4, [&uncreated](bool a_ok, int, std::uint64_t) { uncreated = { 1, a_ok, 0 }; });
		assert(uncreated.calls == 1 && !uncreated.ok);
		assert(RecordedLog::Contains("lobby create result could not be awaited"));
	}

	void CheckMatchmakingMissingExport()
	{
		using namespace F4T::Steam;
		if (!kHasGnsHeaders) return;

		for (const char* absent : { "SteamAPI_ISteamMatchmaking_CreateLobby", "SteamAPI_ISteamMatchmaking_GetLobbyOwner", "SteamAPI_RegisterCallResult" }) {
			auto table = MatchmakingTable();
			table.erase(absent);
			RecordedLog::Clear();
			const auto loaded = LoadSteamApiFrom(Lookup(table), {});
			assert(loaded.available);
			assert(!loaded.matchmakingAvailable);
			assert(loaded.matchmakingMissing == std::vector<std::string>{ absent });
			assert(Has(loaded.missingOptional, absent));
			assert(!loaded.matchmaking.createLobby);
			assert(RecordedLog::Contains(std::string{ "steam matchmaking unavailable: steam_api64.dll is missing " } + absent));
			assert(RecordedLog::Contains("rich presence join still works"));
			assert(RecordedLog::Contains("steam optional exports missing"));
			assert(loaded.api.IsComplete());
			UnbindInvite();
		}

		{
			auto table = MatchmakingTable();
			table.erase("SteamAPI_SteamMatchmaking_v009");
			RecordedLog::Clear();
			const auto loaded = LoadSteamApiFrom(Lookup(table), {});
			assert(loaded.available && !loaded.matchmakingAvailable);
			assert(Has(loaded.matchmakingMissing, "SteamAPI_SteamMatchmaking_v009"));
			assert(Has(loaded.matchmakingMissing, "SteamMatchmaking"));
			assert(RecordedLog::Contains("SteamAPI_SteamMatchmaking_v009, SteamMatchmaking"));
			UnbindInvite();
		}

		{
			auto table = MatchmakingTable();
			table.erase("SteamAPI_SteamMatchmaking_v009");
			table["SteamClient"] = reinterpret_cast<void*>(&FakeSteamClient);
			table["SteamAPI_GetHSteamUser"] = reinterpret_cast<void*>(&FakeHandle);
			table["SteamAPI_GetHSteamPipe"] = reinterpret_cast<void*>(&FakeHandle);
			table["SteamAPI_ISteamClient_GetISteamGenericInterface"] = reinterpret_cast<void*>(&FakeGenericMatchmaking);
			const auto loaded = LoadSteamApiFrom(Lookup(table), {});
			assert(loaded.available && loaded.matchmakingAvailable);
			assert(loaded.matchmakingMissing.empty());
			assert(Has(loaded.foundVersions, "ISteamMatchmaking SteamMatchMaking009 from GetISteamGenericInterface"));
			UnbindInvite();
		}

		{
			auto table = MatchmakingTable();
			table.erase("SteamAPI_RegisterCallback");
			table.erase("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
			const auto loaded = LoadSteamApiFrom(Lookup(table), {});
			assert(loaded.available && loaded.matchmakingAvailable);
			assert(Has(loaded.missingOptional, "SteamAPI_RegisterCallback"));
			assert(Has(loaded.missingOptional, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog"));
			assert(RegisteredCallbackIds().empty());
			UnbindInvite();
		}

		{
			const FakeTable partial = Identity("SteamAPI_SteamFriends_v017", "SteamAPI_SteamUser_v021", reinterpret_cast<void*>(&FakeTicket));
			const auto loaded = LoadSteamApiFrom(Lookup(partial), {});
			assert(!loaded.available);
			assert(!loaded.matchmakingAvailable);
		}
	}
}

int main()
{
	using F4T::Steam::ChooseFirstPresentName;

	assert(Chose(ChooseFirstPresentName(kOrdered, Present({ "v009", "v012" })), "v012"));
	assert(Chose(ChooseFirstPresentName(kOrdered, Present({ "v010", "v011" })), "v011"));
	assert(Chose(ChooseFirstPresentName(kOrdered, Present({ "v009", "v010", "v011", "v012" })), "v012"));

	assert(Chose(ChooseFirstPresentName(kOrdered, Present({ "v011" })), "v011"));
	assert(Chose(ChooseFirstPresentName(kOrdered, Present({ "v009" })), "v009"));

	assert(ChooseFirstPresentName(kOrdered, Present({})) == nullptr);
	assert(ChooseFirstPresentName(std::span<const char* const>{}, Present({ "v012" })) == nullptr);

	{
		int calls = 0;
		const auto* chosen = ChooseFirstPresentName(kOrdered, [&calls](const char* a_name) -> void* {
			++calls;
			return std::strcmp(a_name, "v011") == 0 ? const_cast<char*>("") : nullptr;
		});
		assert(Chose(chosen, "v011"));
		assert(calls == 2);
	}

	using F4T::Steam::FriendsAccessorNames;
	using F4T::Steam::UserAccessorNames;

	assert(Chose(FriendsAccessorNames().back(), "SteamFriends"));
	assert(Chose(UserAccessorNames().back(), "SteamUser"));
	assert(Chose(ChooseFirstPresentName(FriendsAccessorNames(), Present({ "SteamFriends" })), "SteamFriends"));
	assert(Chose(ChooseFirstPresentName(UserAccessorNames(), Present({ "SteamUser" })), "SteamUser"));
	assert(Chose(ChooseFirstPresentName(FriendsAccessorNames(), Present({ "SteamFriends", "SteamAPI_SteamFriends_v017" })), "SteamAPI_SteamFriends_v017"));
	assert(Chose(ChooseFirstPresentName(UserAccessorNames(), Present({ "SteamUser", "SteamAPI_SteamUser_v019" })), "SteamAPI_SteamUser_v019"));

	using F4T::Steam::ProbeGenericInterfaces;

	assert(Lines(ProbeGenericInterfaces(FakeProbe(nullptr, nullptr)), {
		"SteamNetworkingSockets012 non-null",
		"SteamNetworkingUtils004 non-null" }));
	assert(Lines(ProbeGenericInterfaces(FakeProbe(&g_sockets, &g_client)), {
		"SteamNetworkingSockets012 non-null, same as the versioned accessor",
		"SteamNetworkingUtils004 non-null, differs from the versioned accessor" }));
	{
		auto inputs = FakeProbe(nullptr, nullptr);
		inputs.genericInterface = [](void*, std::int32_t, std::int32_t, const char*) -> void* { return nullptr; };
		assert(Lines(ProbeGenericInterfaces(inputs), { "SteamNetworkingSockets012 null", "SteamNetworkingUtils004 null" }));
	}
	{
		auto inputs = FakeProbe(nullptr, nullptr);
		inputs.steamClient = {};
		assert(Lines(ProbeGenericInterfaces(inputs), { "SteamClient is not exported" }));
	}
	{
		auto inputs = FakeProbe(nullptr, nullptr);
		inputs.genericInterface = {};
		assert(Lines(ProbeGenericInterfaces(inputs), {
			"SteamAPI_ISteamClient_GetISteamGenericInterface is not exported" }));
	}
	{
		auto inputs = FakeProbe(nullptr, nullptr);
		inputs.steamClient = [] { return static_cast<void*>(nullptr); };
		assert(Lines(ProbeGenericInterfaces(inputs), { "SteamClient returned null" }));
	}
	{
		int generic = 0;
		auto inputs = FakeProbe(nullptr, nullptr);
		inputs.steamPipe = [] { return std::int32_t{ 0 }; };
		inputs.genericInterface = [&generic](void*, std::int32_t, std::int32_t, const char*) -> void* {
			++generic;
			return nullptr;
		};
		assert(Lines(ProbeGenericInterfaces(inputs), { "HSteamUser or HSteamPipe is 0: SteamAPI_Init has not run in this process" }));
		assert(generic == 0);
	}

	{
		const auto loaded = F4T::Steam::LoadSteamApi();
		assert(!loaded.available);
		assert(loaded.mySteamId == 0);
		assert(loaded.interfaceProbe.empty());
		assert(loaded.moduleVersion.empty());
	}

	{
		const auto table = Identity("SteamAPI_SteamFriends_v017", "SteamAPI_SteamUser_v021", reinterpret_cast<void*>(&FakeTicket));
		ResetCounters();
		const auto loaded = F4T::Steam::LoadSteamApiFrom(Lookup(table), {});
		assert(!loaded.available);
		assert(loaded.mySteamId == kFakeSteamId);
		assert(loaded.reason.starts_with("steam_api64.dll does not export SteamAPI_SteamNetworkingSockets_SteamAPI_v012"));
		assert(loaded.reason.find("Steam sign-in still works") != std::string::npos);
		assert(loaded.isFriend(kFakeFriend));
		assert(!loaded.isFriend(kFakeStranger));
		assert(loaded.createAuthTicket);
		assert(loaded.createAuthTicket().size() == 2);
		assert(loaded.interfaceProbe.empty());
		assert(g_friendsAccessorCalls == 1 && g_userAccessorCalls == 1);

		const auto probed = F4T::Steam::LoadSteamApiFrom(Lookup(table), { true });
		assert(!probed.interfaceProbe.empty());
		assert(probed.interfaceProbe.front() == "SteamClient is not exported");
	}

	{
		const auto table = Identity("SteamFriends", "SteamUser", reinterpret_cast<void*>(&FakeTicketLegacy));
		ResetCounters();
		const auto loaded = F4T::Steam::LoadSteamApiFrom(Lookup(table), {});
		assert(!loaded.available);
		assert(loaded.mySteamId == kFakeSteamId);
		assert(g_friendsAccessorCalls == 1 && g_userAccessorCalls == 1);
		assert(loaded.reason.find("SteamNetworkingSockets") != std::string::npos);
		assert(loaded.createAuthTicket);
		assert(loaded.createAuthTicket().size() == 4);
	}

	{
		const FakeTable empty;
		const auto loaded = F4T::Steam::LoadSteamApiFrom(Lookup(empty), {});
		assert(!loaded.available);
		assert(loaded.mySteamId == 0);
		assert(loaded.reason.starts_with("steam_api64.dll does not export SteamAPI_SteamFriends_v017"));
		assert(loaded.interfaceProbe.empty());
	}

	{
		const auto none = F4T::Steam::ReadLoadOptions(nullptr);
		assert(!none.probe);
		const auto on = F4T::Steam::ReadLoadOptions("yes");
		assert(on.probe);
		const auto off = F4T::Steam::ReadLoadOptions("0");
		assert(!off.probe);
	}

	CheckLaunchArgs();
	CheckValidateConnectString();
	CheckCallbackAbi();
	CheckHandlerSwapIsSafe();
	CheckMatchmakingLoad();
	CheckMatchmakingMissingExport();

	{
		std::vector<std::uint8_t> resource(8, 0x11);
		for (const std::uint32_t word : { 0xFEEF04BDu, 0x00010000u, 0x00070028u, 0x003D0042u }) {
			for (int shift = 0; shift < 32; shift += 8) resource.push_back(static_cast<std::uint8_t>(word >> shift));
		}
		assert(F4T::Steam::DescribeVersionResource(resource) == "file version 7.40.61.66");

		const std::vector<std::uint8_t> zeros(64, 0);
		assert(F4T::Steam::DescribeVersionResource(zeros) == "no version resource");

		std::vector<std::uint8_t> truncated(8, 0x11);
		for (int shift = 0; shift < 32; shift += 8) truncated.push_back(static_cast<std::uint8_t>(0xFEEF04BDu >> shift));
		assert(F4T::Steam::DescribeVersionResource(truncated) == "no version resource");
	}

	return 0;
}
