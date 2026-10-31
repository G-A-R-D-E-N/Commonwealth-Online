#pragma once

#include "F4TSteamTunnel.h"

#include "F4TTransportEndpoint.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using F4T::Transport::DescribeTunnelError;
	using F4T::Transport::DescribeTunnelEventKind;
	using F4T::Transport::FormatLoopbackEndpoint;
	using F4T::Transport::FormatTunnelEvent;
	using F4T::Transport::kConnectionEndNotPermitted;
	using F4T::Transport::kConnectionEndServerFull;
	using F4T::Transport::kConnectionEndLocalSocketFailed;
	using F4T::Transport::kInvalidSteamListenSocket;
	using F4T::Transport::kMaximumQueuedDatagrams;
	using F4T::Transport::kMaximumRelayedDatagramBytes;
	using F4T::Transport::kMaximumTunnelConnections;
	using F4T::Transport::kSendUnreliableNoNagle;
	using F4T::Transport::kSteamTunnelVirtualPort;
	using F4T::Transport::MakeFriendsOnlyPolicy;
	using F4T::Transport::ReadSteamHostId;
	using F4T::Transport::SteamApi;
	using F4T::Transport::SteamConnectionEvent;
	using F4T::Transport::SteamConnectionHandle;
	using F4T::Transport::SteamGuestTunnel;
	using F4T::Transport::SteamHostTunnel;
	using F4T::Transport::SteamListenSocketHandle;
	using F4T::Transport::TunnelError;
	using F4T::Transport::TunnelEvent;
	using F4T::Transport::TunnelEventKind;
	using F4T::Transport::TunnelState;
	using F4T::Transport::UdpDatagram;
	using F4T::Transport::UdpSocketApi;

	constexpr std::uint64_t kFriend = 76561198000000042ull;
	constexpr std::uint64_t kStranger = 76561198000000099ull;
	constexpr std::uint64_t kHostSteamId = 76561198000000007ull;
	constexpr std::uint16_t kServerPort = 7777;
	constexpr std::uint16_t kGnsPort = 51000;
	constexpr std::size_t kGnsMaximumUdpPacketBytes = 1300;
	constexpr std::uint16_t kFirstFakePort = 40000;
	constexpr SteamConnectionHandle kGuestConnection = 1;

	static_assert(kMaximumRelayedDatagramBytes >= kGnsMaximumUdpPacketBytes);

	struct FakeSteam
	{
		mutable std::mutex mutex;

		struct Connection
		{
			std::uint64_t peerSteamId{};
			bool accepted{ false };
			bool closed{ false };
			int closeReason{ 0 };
			std::string closeDebug;
			std::deque<std::vector<std::uint8_t>> inbox;
			std::vector<std::pair<std::vector<std::uint8_t>, int>> sent;
		};

		std::uint64_t mySteamId{ 76561198000000001ull };
		bool relayReady{ true };
		bool listenOk{ true };
		SteamListenSocketHandle nextListen{ 100 };
		SteamConnectionHandle nextConnection{ 1 };
		std::vector<SteamConnectionEvent> events;
		std::map<SteamConnectionHandle, Connection> connections;
		std::vector<std::uint16_t> listenVirtualPorts;
		std::vector<SteamListenSocketHandle> closedListenSockets;
		std::vector<std::pair<std::uint64_t, std::uint16_t>> connectRequests;
		int callbackRuns{ 0 };

		Connection* Find(SteamConnectionHandle a_connection)
		{
			const auto found = connections.find(a_connection);
			return found == connections.end() ? nullptr : &found->second;
		}

		[[nodiscard]] bool IsPeerAccepted(std::uint64_t a_peerSteamId) const
		{
			std::lock_guard lock{ mutex };
			for (const auto& entry : connections) {
				if (entry.second.peerSteamId == a_peerSteamId && entry.second.accepted) return true;
			}
			return false;
		}

		[[nodiscard]] bool IsAccepted(SteamConnectionHandle a_connection) const
		{
			std::lock_guard lock{ mutex };
			const auto found = connections.find(a_connection);
			return found != connections.end() && found->second.accepted;
		}

		[[nodiscard]] bool IsClosed(SteamConnectionHandle a_connection) const
		{
			std::lock_guard lock{ mutex };
			const auto found = connections.find(a_connection);
			return found != connections.end() && found->second.closed;
		}

		[[nodiscard]] int CloseReason(SteamConnectionHandle a_connection) const
		{
			std::lock_guard lock{ mutex };
			const auto found = connections.find(a_connection);
			return found == connections.end() ? 0 : found->second.closeReason;
		}
	};

	struct FakeUdp
	{
		mutable std::mutex mutex;

		struct Socket
		{
			bool open{ true };
			std::deque<UdpDatagram> inbox;
			std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> sent;
		};

		bool openFails{ false };
		std::uint16_t nextPort{ kFirstFakePort };
		std::map<std::uint16_t, Socket> sockets;
		std::vector<std::uint16_t> opened;
		std::vector<std::uint16_t> closed;

		Socket* Find(std::uint16_t a_port)
		{
			const auto found = sockets.find(a_port);
			return found == sockets.end() ? nullptr : &found->second;
		}

		[[nodiscard]] bool IsOpen(std::uint16_t a_port) const
		{
			std::lock_guard lock{ mutex };
			const auto found = sockets.find(a_port);
			return found != sockets.end() && found->second.open;
		}

		[[nodiscard]] std::vector<std::uint16_t> OpenedPorts() const
		{
			std::lock_guard lock{ mutex };
			return opened;
		}

		[[nodiscard]] std::vector<std::uint16_t> ClosedPorts() const
		{
			std::lock_guard lock{ mutex };
			return closed;
		}

		[[nodiscard]] std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> SentTo(std::uint16_t a_port) const
		{
			std::lock_guard lock{ mutex };
			const auto found = sockets.find(a_port);
			if (found == sockets.end()) return {};
			return found->second.sent;
		}
	};

	SteamApi MakeSteamApi(FakeSteam& a_fake)
	{
		SteamApi api;
		api.getSteamId = [&a_fake] {
			std::lock_guard lock{ a_fake.mutex };
			return a_fake.mySteamId;
		};
		api.initRelay = [&a_fake] {
			std::lock_guard lock{ a_fake.mutex };
			return a_fake.relayReady;
		};
		api.createListenSocketP2P = [&a_fake](std::uint16_t a_virtualPort) {
			std::lock_guard lock{ a_fake.mutex };
			a_fake.listenVirtualPorts.push_back(a_virtualPort);
			return a_fake.listenOk ? a_fake.nextListen++ : kInvalidSteamListenSocket;
		};
		api.connectP2P = [&a_fake](std::uint64_t a_steamId, std::uint16_t a_virtualPort) {
			std::lock_guard lock{ a_fake.mutex };
			a_fake.connectRequests.emplace_back(a_steamId, a_virtualPort);
			return a_fake.nextConnection++;
		};
		api.acceptConnection = [&a_fake](SteamConnectionHandle a_connection) {
			std::lock_guard lock{ a_fake.mutex };
			auto* connection = a_fake.Find(a_connection);
			if (connection == nullptr || connection->closed) return false;
			connection->accepted = true;
			return true;
		};
		api.closeConnection = [&a_fake](SteamConnectionHandle a_connection, int a_reason, std::string_view a_debug) {
			std::lock_guard lock{ a_fake.mutex };
			if (auto* connection = a_fake.Find(a_connection)) {
				connection->closed = true;
				connection->closeReason = a_reason;
				connection->closeDebug = std::string{ a_debug };
			}
		};
		api.closeListenSocket = [&a_fake](SteamListenSocketHandle a_socket) {
			std::lock_guard lock{ a_fake.mutex };
			a_fake.closedListenSockets.push_back(a_socket);
		};
		api.sendMessage = [&a_fake](SteamConnectionHandle a_connection, std::span<const std::uint8_t> a_payload, int a_flags) {
			std::lock_guard lock{ a_fake.mutex };
			auto* connection = a_fake.Find(a_connection);
			if (connection == nullptr || connection->closed) return false;
			connection->sent.emplace_back(std::vector<std::uint8_t>{ a_payload.begin(), a_payload.end() }, a_flags);
			return true;
		};
		api.receiveMessages = [&a_fake](SteamConnectionHandle a_connection, std::size_t a_maximum) {
			std::lock_guard lock{ a_fake.mutex };
			std::vector<std::vector<std::uint8_t>> messages;
			auto* connection = a_fake.Find(a_connection);
			if (connection == nullptr || connection->closed) return messages;
			while (!connection->inbox.empty() && messages.size() < a_maximum) {
				messages.push_back(std::move(connection->inbox.front()));
				connection->inbox.pop_front();
			}
			return messages;
		};
		api.pollConnectionEvents = [&a_fake] {
			std::lock_guard lock{ a_fake.mutex };
			auto events = std::move(a_fake.events);
			a_fake.events.clear();
			return events;
		};
		api.runCallbacks = [&a_fake] {
			std::lock_guard lock{ a_fake.mutex };
			++a_fake.callbackRuns;
		};
		return api;
	}

	UdpSocketApi MakeUdpApi(FakeUdp& a_fake)
	{
		UdpSocketApi api;
		api.openLoopback = [&a_fake]() -> std::optional<std::uint16_t> {
			std::lock_guard lock{ a_fake.mutex };
			if (a_fake.openFails) return std::nullopt;
			const auto port = a_fake.nextPort++;
			a_fake.sockets[port] = FakeUdp::Socket{};
			a_fake.opened.push_back(port);
			return port;
		};
		api.close = [&a_fake](std::uint16_t a_port) {
			std::lock_guard lock{ a_fake.mutex };
			if (auto* socket = a_fake.Find(a_port)) socket->open = false;
			a_fake.closed.push_back(a_port);
		};
		api.sendTo = [&a_fake](std::uint16_t a_port, std::uint16_t a_destination, std::span<const std::uint8_t> a_payload) {
			std::lock_guard lock{ a_fake.mutex };
			auto* socket = a_fake.Find(a_port);
			if (socket == nullptr || !socket->open) return false;
			socket->sent.emplace_back(a_destination, std::vector<std::uint8_t>{ a_payload.begin(), a_payload.end() });
			return true;
		};
		api.receive = [&a_fake](std::uint16_t a_port) {
			std::lock_guard lock{ a_fake.mutex };
			std::vector<UdpDatagram> datagrams;
			auto* socket = a_fake.Find(a_port);
			if (socket == nullptr || !socket->open) return datagrams;
			while (!socket->inbox.empty()) {
				datagrams.push_back(std::move(socket->inbox.front()));
				socket->inbox.pop_front();
			}
			return datagrams;
		};
		return api;
	}

	SteamConnectionHandle QueueIncoming(FakeSteam& a_fake, std::uint64_t a_peerSteamId)
	{
		std::lock_guard lock{ a_fake.mutex };
		const auto connection = a_fake.nextConnection++;
		a_fake.connections[connection] = FakeSteam::Connection{};
		a_fake.connections[connection].peerSteamId = a_peerSteamId;
		a_fake.events.push_back(SteamConnectionEvent{ connection, a_peerSteamId, TunnelState::Connecting });
		return connection;
	}

	F4T::Transport::SteamAdmissionPolicy FriendOnly()
	{
		return MakeFriendsOnlyPolicy([](std::uint64_t a_steamId) { return a_steamId == kFriend; });
	}
}
