#pragma once

#include "F4TSteamApiLoader.h"
#include "F4TSteamTunnel.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::Steam
{
	struct ServiceStatus
	{
		bool available{};
		std::string reason;
		bool matchmaking{};
	};

	using LoaderFn = std::function<SteamApiLoadResult()>;
	using IdentityBinderFactory = std::function<Transport::SteamIdentityBinder(std::uint16_t, std::uint64_t)>;

	struct HostLookup
	{
		std::uint64_t steamId{};
		std::string name;
		std::string error;

		[[nodiscard]] bool ok() const { return steamId != 0 && error.empty(); }
	};

	struct LobbyPeer
	{
		std::uint64_t steamId{};
		std::string name;
	};

	using LobbyPeersSink = std::function<void(const std::vector<LobbyPeer>&)>;

	struct HostingFriend
	{
		std::uint64_t steamId{};
		std::uint64_t friendCode{};
		std::string name;
		std::string hostName;
		std::uint32_t players{};
		std::uint32_t maxPlayers{};
		std::string build;
		bool compatible{ true };
		std::string reason;
	};

	struct HostingFriends
	{
		bool matchmakingAvailable{};
		std::string error;
		std::vector<HostingFriend> hosts;
	};

	void Initialize(LoaderFn a_loader = &LoadSteamApi);
	bool RetryIfUnavailable();
	void SetIdentityBinderFactory(IdentityBinderFactory a_factory);
	void SetAdmissionLog(Transport::TunnelLog a_log);
	[[nodiscard]] ServiceStatus Status();
	[[nodiscard]] bool MatchmakingAvailable();
	[[nodiscard]] std::vector<std::string> MissingOptionalExports();
	[[nodiscard]] std::string LocalBuildTag();
	void SetLocalBuildTag(std::string a_tag);
	[[nodiscard]] std::vector<std::string> InterfaceProbe();
	[[nodiscard]] std::string ModuleVersion();
	[[nodiscard]] std::uint64_t MySteamId();
	[[nodiscard]] std::optional<std::string> CreateAuthTicketHex();
	[[nodiscard]] std::uint64_t FriendCode(std::uint64_t a_steamId);
	[[nodiscard]] HostLookup FindHost(std::string_view a_target);
	[[nodiscard]] HostingFriends ListHostingFriends(std::chrono::milliseconds a_wait = std::chrono::milliseconds{ 400 });
	[[nodiscard]] std::optional<std::string> CheckHostBuild(std::uint64_t a_hostSteamId, std::chrono::milliseconds a_wait = std::chrono::milliseconds{ 400 });
	void AnnounceHosting(std::string_view a_name);
	[[nodiscard]] std::uint64_t HostLobby();
	void JoinLobby(std::uint64_t a_lobby);
	void SetGuestPresence(std::string_view a_hostName, std::uint64_t a_hostFriendCode = 0);
	void ClearGuestPresence();
	void SetHostMemberLimit(std::uint32_t a_maxPlayers);
	void SetJoinConnectString(std::string_view a_connect);
	void SetLobbyPeersSink(LobbyPeersSink a_sink);
	[[nodiscard]] Transport::TunnelError StartHost(std::uint16_t a_serverPort, std::uint32_t a_maxPlayers);
	void StopHost();
	[[nodiscard]] bool HostRunning();
	[[nodiscard]] bool HostIdentityLinked();
	[[nodiscard]] Transport::GuestConnectResult ConnectGuest(std::uint64_t a_hostSteamId);
	void StopGuest();
	[[nodiscard]] bool GuestRunning();
	[[nodiscard]] std::string GuestRefusalMessage();
	[[nodiscard]] Transport::TunnelState GuestState();
	[[nodiscard]] std::string GuestFailureReason();
	[[nodiscard]] bool WaitForGuestConnected(std::chrono::milliseconds a_timeout, std::string& a_failure);
	void Shutdown();
}
