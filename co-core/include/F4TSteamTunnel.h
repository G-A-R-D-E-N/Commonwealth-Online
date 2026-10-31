// SPDX-License-Identifier: GPL-3.0-or-later
// Design informed by FathersFallout (FatherLamb), GPL-3.0 with additional terms; used with the author's permission.
#pragma once

#include "F4TSteamAdmission.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace F4T::Transport
{
	using SteamConnectionHandle = std::uint32_t;
	using SteamListenSocketHandle = std::uint32_t;

	inline constexpr SteamConnectionHandle kInvalidSteamConnection = 0;
	inline constexpr SteamListenSocketHandle kInvalidSteamListenSocket = 0;

	inline constexpr std::uint16_t kSteamTunnelVirtualPort = 7779;
	inline constexpr std::uint16_t kDefaultDedicatedServerPort = 7777;
	inline constexpr std::size_t kMaximumTunnelConnections = 16;
	inline constexpr std::size_t kMaximumQueuedDatagrams = 64;
	inline constexpr std::size_t kMaximumQueuedBytes = 256 * 1024;
	inline constexpr std::size_t kMaximumRelayedDatagramBytes = 1500;
	inline constexpr std::size_t kMaximumTunnelEvents = 256;
	inline constexpr int kSendUnreliableNoNagle = 1;
	inline constexpr double kRelayWelcomeTimeoutSeconds = 20.0;
	inline constexpr int kConnectionEndNotPermitted = SteamAdmission::kEndNotPermitted;
	inline constexpr int kConnectionEndServerFull = SteamAdmission::kEndServerFull;
	inline constexpr int kConnectionEndLocalSocketFailed = SteamAdmission::kEndLocalSocketFailed;
	inline constexpr int kConnectionEndIdentityUnlinked = SteamAdmission::kEndIdentityUnlinked;
	inline constexpr int kConnectionEndFriendsOnly = SteamAdmission::kEndFriendsOnly;
	inline constexpr int kConnectionEndFriendCheckUnavailable = SteamAdmission::kEndFriendCheckUnavailable;
	inline constexpr int kConnectionEndUndeliverable = SteamAdmission::kEndUndeliverable;
	inline constexpr std::string_view kLoopbackHost = "127.0.0.1";

	enum class TunnelState
	{
		Closed,
		Connecting,
		Connected,
		Failed
	};

	struct SteamConnectionEvent
	{
		SteamConnectionHandle connection{ kInvalidSteamConnection };
		std::uint64_t peerSteamId{};
		TunnelState state{ TunnelState::Closed };
		int endReason{};
		std::string endDebug;
	};

	struct SteamApi
	{
		std::function<std::uint64_t()> getSteamId;
		std::function<bool()> initRelay;
		std::function<SteamListenSocketHandle(std::uint16_t)> createListenSocketP2P;
		std::function<SteamConnectionHandle(std::uint64_t, std::uint16_t)> connectP2P;
		std::function<bool(SteamConnectionHandle)> acceptConnection;
		std::function<void(SteamConnectionHandle, int, std::string_view)> closeConnection;
		std::function<void(SteamListenSocketHandle)> closeListenSocket;
		std::function<bool(SteamConnectionHandle, std::span<const std::uint8_t>, int)> sendMessage;
		std::function<std::vector<std::vector<std::uint8_t>>(SteamConnectionHandle, std::size_t)> receiveMessages;
		std::function<std::vector<SteamConnectionEvent>()> pollConnectionEvents;
		std::function<void()> runCallbacks;

		[[nodiscard]] bool IsComplete() const;
	};

	struct UdpDatagram
	{
		std::uint16_t sourcePort{};
		std::vector<std::uint8_t> payload;
	};

	struct UdpSocketApi
	{
		std::function<std::optional<std::uint16_t>()> openLoopback;
		std::function<void(std::uint16_t)> close;
		std::function<bool(std::uint16_t, std::uint16_t, std::span<const std::uint8_t>)> sendTo;
		std::function<std::vector<UdpDatagram>(std::uint16_t)> receive;

		[[nodiscard]] bool IsComplete() const;
	};

	enum class TunnelEventKind : std::uint8_t
	{
		TunnelStarted,
		TunnelStopped,
		LinkStateChanged,
		PeerAccepted,
		PeerRefused,
		PeerDropped,
		IdentityBound,
		IdentityBindFailed,
		FirstDatagramForwarded
	};

	struct TunnelEvent
	{
		TunnelEventKind kind{ TunnelEventKind::LinkStateChanged };
		SteamConnectionHandle connection{ kInvalidSteamConnection };
		std::uint64_t peerSteamId{};
		int code{};
		std::uint16_t port{};
		TunnelState state{ TunnelState::Closed };
		std::string detail;
	};

	[[nodiscard]] std::string_view DescribeTunnelEventKind(TunnelEventKind a_kind);
	[[nodiscard]] std::string FormatTunnelEvent(const TunnelEvent& a_event);

	struct SteamTunnelStats
	{
		std::uint64_t datagramsToLocal{};
		std::uint64_t datagramsToSteam{};
		std::uint64_t datagramsDroppedInvalid{};
		std::uint64_t datagramsDroppedBufferFull{};
		std::uint64_t connectionsAccepted{};
		std::uint64_t connectionsRejected{};
		std::uint64_t connectionsRefusedCapacity{};
		std::uint64_t identityBindFailures{};
		std::uint32_t activeConnections{};
	};

	enum class TunnelError
	{
		None,
		InvalidArgument,
		SteamUnavailable,
		RelayNotReady,
		ListenSocketFailed,
		LoopbackSocketFailed,
		ConnectionFailed,
		NotRunning
	};

	[[nodiscard]] std::string_view DescribeTunnelError(TunnelError a_error);
	[[nodiscard]] std::size_t ClampHostMaximumConnections(std::size_t a_desiredConnections, std::size_t a_activeConnections);
	[[nodiscard]] std::string_view DescribeTunnelState(TunnelState a_state);
	[[nodiscard]] std::string_view DescribeConnectionEnd(int a_endReason);

	using SteamAdmissionPolicy = std::function<bool(std::uint64_t)>;

	struct SteamIdentityBinder
	{
		std::function<bool(std::uint16_t, std::uint64_t, bool)> bind;
		std::function<void(std::uint16_t)> unbind;
		std::optional<SteamAdmission::HostPolicy> hostPolicy;

		[[nodiscard]] bool IsEmpty() const { return !bind && !unbind; }
		[[nodiscard]] bool IsComplete() const { return bind && unbind; }
	};

	[[nodiscard]] SteamAdmissionPolicy MakeFriendsOnlyPolicy(
		std::function<bool(std::uint64_t)> a_isFriend,
		std::vector<std::uint64_t> a_allowSet = {});

	[[nodiscard]] std::optional<std::uint64_t> ReadSteamHostId(std::string_view a_text);
	[[nodiscard]] std::string FormatLoopbackEndpoint(std::uint16_t a_port);

	using TunnelLog = std::function<void(std::string_view)>;

	class SteamHostTunnel
	{
	public:
		SteamHostTunnel(SteamApi a_steam, UdpSocketApi a_udp);
		~SteamHostTunnel();

		void SetLog(TunnelLog a_log);
		void SetClock(std::function<double()> a_now);

		SteamHostTunnel(const SteamHostTunnel&) = delete;
		SteamHostTunnel& operator=(const SteamHostTunnel&) = delete;

		[[nodiscard]] TunnelError Start(
			std::uint16_t a_serverPort,
			SteamAdmissionPolicy a_policy,
			std::size_t a_maximumConnections = kMaximumTunnelConnections,
			SteamIdentityBinder a_binder = {});
		void Pump();
		void Stop();

		[[nodiscard]] bool IsRunning() const;
		[[nodiscard]] SteamTunnelStats Stats() const;
		[[nodiscard]] std::vector<std::uint64_t> PeerSteamIds() const;
		void SetMaximumConnections(std::size_t a_maximumConnections);
		[[nodiscard]] std::vector<TunnelEvent> DrainEvents();

	private:
		struct Relay
		{
			SteamConnectionHandle connection{ kInvalidSteamConnection };
			std::uint64_t peerSteamId{};
			std::uint16_t localPort{};
			std::uint16_t pendingPort{};
			std::deque<std::vector<std::uint8_t>> pending;
			std::size_t pendingBytes{};
			double welcomeDeadline{};
			bool forwardedFirstDatagram{};
			std::vector<SteamConnectionHandle> supersedes;
		};

		void Record(TunnelEvent a_event);

		[[nodiscard]] Relay* FindRelay(SteamConnectionHandle a_connection);
		void HandleEvents();
		void AdmitConnection(SteamConnectionHandle a_connection, std::uint64_t a_peerSteamId);
		void StartIdentityBinder(SteamConnectionHandle a_connection, std::uint16_t a_port, SteamIdentityBinder a_binder, std::uint64_t a_peerSteamId, bool a_passwordRequired);
		void RefuseConnection(SteamConnectionHandle a_connection, int a_reason, std::string_view a_debug, bool a_capacity);
		void DropConnection(SteamConnectionHandle a_connection, std::string_view a_debug);
		void SweepWelcomeDeadlines();
		void Log(const TunnelLog& a_log, std::string_view a_line) const;
		[[nodiscard]] double NowSeconds() const;
		void PumpSteamToLocal();
		void PumpLocalToSteam();

		SteamApi steam_;
		UdpSocketApi udp_;
		mutable std::mutex mutex_;
		SteamAdmissionPolicy policy_;
		SteamIdentityBinder binder_;
		TunnelLog log_;
		std::function<double()> clock_;
		std::vector<Relay> relays_;
		std::vector<std::thread> admissionWorkers_;
		SteamListenSocketHandle listenSocket_{ kInvalidSteamListenSocket };
		std::uint16_t serverPort_{};
		std::size_t maximumConnections_{ kMaximumTunnelConnections };
		SteamTunnelStats stats_;
		bool running_{ false };
		std::deque<TunnelEvent> events_;
	};

	struct GuestConnectResult
	{
		TunnelError error{ TunnelError::None };
		std::uint16_t loopbackPort{};

		[[nodiscard]] bool ok() const { return error == TunnelError::None; }
	};

	class SteamGuestTunnel
	{
	public:
		SteamGuestTunnel(SteamApi a_steam, UdpSocketApi a_udp);

		SteamGuestTunnel(const SteamGuestTunnel&) = delete;
		SteamGuestTunnel& operator=(const SteamGuestTunnel&) = delete;

		[[nodiscard]] GuestConnectResult Connect(std::uint64_t a_hostSteamId);
		void Pump();
		void Stop();

		[[nodiscard]] bool IsRunning() const;
		[[nodiscard]] TunnelState State() const;
		[[nodiscard]] SteamTunnelStats Stats() const;
		[[nodiscard]] std::string FailureReason() const;
		[[nodiscard]] int EndReason() const;
		[[nodiscard]] std::vector<TunnelEvent> DrainEvents();

	private:
		void HandleEvents();
		void PumpSteamToLocal();
		void PumpLocalToSteam();
		void Teardown(TunnelState a_state, std::string_view a_debug, int a_endReason = 0);

		SteamApi steam_;
		UdpSocketApi udp_;
		mutable std::mutex mutex_;
		SteamConnectionHandle connection_{ kInvalidSteamConnection };
		std::uint64_t hostSteamId_{};
		std::uint16_t loopbackPort_{};
		std::uint16_t localPeerPort_{};
		std::deque<std::vector<std::uint8_t>> pending_;
		std::size_t pendingBytes_{};
		std::deque<std::vector<std::uint8_t>> outboundPending_;
		std::size_t outboundPendingBytes_{};
		SteamTunnelStats stats_;
		TunnelState state_{ TunnelState::Closed };
		std::string failureReason_;
		int endReason_{};
		bool running_{ false };
		bool forwardedFirstDatagram_{};
		std::deque<TunnelEvent> events_;
	};
}
