#pragma once

// constraint: official docs pack(4), connect offset 8, 256 bytes

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::Steam
{
	constexpr int kRichPresenceJoinCallback = 337;
	constexpr int kGameLobbyJoinRequestedCallback = 333;
	constexpr int kLobbyEnterCallback = 504;
	constexpr int kLobbyCreatedCallback = 513;
	constexpr std::size_t kRichPresenceConnectBytes = 256;
	constexpr std::size_t kMaximumSteamIdDigits = 20;
	constexpr std::uint64_t kSteamId64Base = 76561197960265728ull;

#pragma pack(push, 4)
	struct RichPresenceJoinRequest
	{
		std::uint64_t steamIdFriend{};
		char connect[kRichPresenceConnectBytes]{};
	};

	struct GameLobbyJoinRequest
	{
		std::uint64_t steamIdLobby{};
		std::uint64_t steamIdFriend{};
	};

	struct LobbyCreatedResult
	{
		std::int32_t result{};
		std::uint64_t steamIdLobby{};
	};

	struct LobbyEnterResult
	{
		std::uint64_t steamIdLobby{};
		std::uint32_t permissions{};
		bool locked{};
		std::uint32_t response{};
	};
#pragma pack(pop)

	static_assert(sizeof(RichPresenceJoinRequest) == 264);
	static_assert(offsetof(RichPresenceJoinRequest, connect) == 8);
	static_assert(sizeof(GameLobbyJoinRequest) == 16);
	static_assert(offsetof(GameLobbyJoinRequest, steamIdFriend) == 8);
	static_assert(sizeof(LobbyCreatedResult) == 12);
	static_assert(offsetof(LobbyCreatedResult, steamIdLobby) == 4);
	static_assert(sizeof(LobbyEnterResult) == 20);
	static_assert(offsetof(LobbyEnterResult, permissions) == 8);
	static_assert(offsetof(LobbyEnterResult, locked) == 12);
	static_assert(offsetof(LobbyEnterResult, response) == 16);

	using LobbyCreatedFn = std::function<void(bool ok, int result, std::uint64_t lobby)>;
	using LobbyEnteredFn = std::function<void(bool ok, std::uint32_t response, std::uint64_t lobby)>;

	struct InviteBind
	{
		void* friends{};
		std::function<void(void*, const char*)> activate;
		std::function<void(void*, int)> registerCallback;
		std::function<void(void*)> unregisterCallback;
		std::function<bool()> overlayEnabled;
		std::function<void(void*, std::uint64_t)> activateLobby;
		std::function<void(void*, std::uint64_t)> registerCallResult;
		std::function<void(void*, std::uint64_t)> unregisterCallResult;
	};

	struct ConnectTarget
	{
		std::uint64_t value{};
		std::uint64_t steamId{};
		bool friendCode{};
	};

	struct LaunchArgs
	{
		std::optional<std::string> connect;
		std::optional<std::uint64_t> lobbyId;
	};

	[[nodiscard]] bool IsIndividualSteamId(std::uint64_t a_steamId);
	[[nodiscard]] bool IsLobbySteamId(std::uint64_t a_steamId);
	[[nodiscard]] std::optional<ConnectTarget> ValidateConnectString(std::string_view a_text);
	[[nodiscard]] LaunchArgs ParseLaunchArgs(std::string_view a_commandLine);
	[[nodiscard]] std::string SanitizeForLog(std::string_view a_text, std::size_t a_maximum = 48);

	void BindInvite(const InviteBind& a_bind);
	void UnbindInvite();
	void RememberInviteCode(std::uint64_t a_friendCode);
	void ClearInviteCode();
	[[nodiscard]] std::string OpenFriendInvite();
	void SetJoinConnectHandler(void (*a_handler)(const char* a_connect));
	void SetLobbyJoinHandler(void (*a_handler)(std::uint64_t a_lobby));
	void SetJoinFailureHandler(void (*a_handler)(const char* a_message));
	void ReportJoinFailure(std::string_view a_message);
	[[nodiscard]] bool HandOffConnect(std::string_view a_connect, std::string_view a_source);
	void QueueLaunchJoin(const LaunchArgs& a_args);
	[[nodiscard]] bool FireLaunchJoin();
	[[nodiscard]] bool AwaitLobbyCreated(std::uint64_t a_call, LobbyCreatedFn a_done);
	[[nodiscard]] bool AwaitLobbyEntered(std::uint64_t a_call, LobbyEnteredFn a_done);
	[[nodiscard]] int InviteCallbackBytes();
	[[nodiscard]] int LastRegisteredCallbackId();
	[[nodiscard]] std::vector<int> RegisteredCallbackIds();
	void RunInviteCallbackForTest(const void* a_param);
	void RunLobbyJoinCallbackForTest(const void* a_param);
	void RunLobbyCreatedForTest(const void* a_param, bool a_ioFailure, std::uint64_t a_call);
	void RunLobbyEnteredForTest(const void* a_param, bool a_ioFailure, std::uint64_t a_call);
}
