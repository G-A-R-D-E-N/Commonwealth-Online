// SPDX-License-Identifier: GPL-3.0-or-later
// Design informed by FathersFallout (FatherLamb), GPL-3.0 with additional terms; used with the author's permission.
#include "F4TSteamTunnel.h"

#include "F4TSteamTunnelInternal.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Transport
{
	SteamGuestTunnel::SteamGuestTunnel(SteamApi a_steam, UdpSocketApi a_udp) :
		steam_{ std::move(a_steam) },
		udp_{ std::move(a_udp) }
	{
	}

	GuestConnectResult SteamGuestTunnel::Connect(std::uint64_t a_hostSteamId)
	{
		if (!steam_.IsComplete()) return { TunnelError::SteamUnavailable, 0 };
		if (!udp_.IsComplete()) return { TunnelError::LoopbackSocketFailed, 0 };
		if (a_hostSteamId == 0) return { TunnelError::InvalidArgument, 0 };

		Stop();
		if (!steam_.initRelay()) return { TunnelError::RelayNotReady, 0 };

		const auto loopbackPort = udp_.openLoopback();
		if (!loopbackPort || *loopbackPort == 0) return { TunnelError::LoopbackSocketFailed, 0 };

		const auto connection = steam_.connectP2P(a_hostSteamId, kSteamTunnelVirtualPort);
		if (connection == kInvalidSteamConnection) {
			udp_.close(*loopbackPort);
			return { TunnelError::ConnectionFailed, 0 };
		}

		std::lock_guard lock{ mutex_ };
		connection_ = connection;
		hostSteamId_ = a_hostSteamId;
		loopbackPort_ = *loopbackPort;
		localPeerPort_ = 0;
		state_ = TunnelState::Connecting;
		stats_.activeConnections = 1;
		running_ = true;
		forwardedFirstDatagram_ = false;
		endReason_ = 0;
		Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::LinkStateChanged, connection, a_hostSteamId, 0, *loopbackPort,
			TunnelState::Connecting, "waiting for the host route" });
		return { TunnelError::None, *loopbackPort };
	}

	bool SteamGuestTunnel::IsRunning() const
	{
		std::lock_guard lock{ mutex_ };
		return running_;
	}

	TunnelState SteamGuestTunnel::State() const
	{
		std::lock_guard lock{ mutex_ };
		return state_;
	}

	SteamTunnelStats SteamGuestTunnel::Stats() const
	{
		std::lock_guard lock{ mutex_ };
		return stats_;
	}

	std::string SteamGuestTunnel::FailureReason() const
	{
		std::lock_guard lock{ mutex_ };
		return failureReason_;
	}

	int SteamGuestTunnel::EndReason() const
	{
		std::lock_guard lock{ mutex_ };
		return endReason_;
	}

	std::vector<TunnelEvent> SteamGuestTunnel::DrainEvents()
	{
		std::lock_guard lock{ mutex_ };
		std::vector<TunnelEvent> drained;
		drained.reserve(events_.size());
		while (!events_.empty()) {
			drained.push_back(std::move(events_.front()));
			events_.pop_front();
		}
		return drained;
	}


	void SteamGuestTunnel::Pump()
	{
		{
			std::lock_guard lock{ mutex_ };
			if (!running_) return;
		}

		steam_.runCallbacks();
		HandleEvents();
		PumpLocalToSteam();
		PumpSteamToLocal();
	}

	void SteamGuestTunnel::Stop()
	{
		Teardown(TunnelState::Closed, "tunnel stopped");
	}

	void SteamGuestTunnel::Teardown(TunnelState a_state, std::string_view a_debug, int a_endReason)
	{
		SteamConnectionHandle connection = kInvalidSteamConnection;
		std::uint16_t loopbackPort = 0;
		{
			std::lock_guard lock{ mutex_ };
			if (!running_) return;
			running_ = false;
			connection = connection_;
			loopbackPort = loopbackPort_;
			connection_ = kInvalidSteamConnection;
			loopbackPort_ = 0;
			localPeerPort_ = 0;
			pending_.clear();
			pendingBytes_ = 0;
			outboundPending_.clear();
			outboundPendingBytes_ = 0;
			state_ = a_state;
			failureReason_ = a_state == TunnelState::Failed ? std::string{ a_debug } : std::string{};
			endReason_ = a_state == TunnelState::Failed ? a_endReason : 0;
			stats_.activeConnections = 0;
			Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::LinkStateChanged, connection, hostSteamId_, endReason_, loopbackPort,
				a_state, std::string{ a_debug } });
		}

		if (connection != kInvalidSteamConnection) steam_.closeConnection(connection, 0, a_debug);
		if (loopbackPort != 0) udp_.close(loopbackPort);
	}

	void SteamGuestTunnel::HandleEvents()
	{
		SteamConnectionHandle connection = kInvalidSteamConnection;
		{
			std::lock_guard lock{ mutex_ };
			connection = connection_;
		}
		if (connection == kInvalidSteamConnection) return;

		for (const auto& event : steam_.pollConnectionEvents()) {
			if (event.connection != connection) continue;
			switch (event.state) {
			case TunnelState::Connected: {
				std::lock_guard lock{ mutex_ };
				if (connection_ == connection) {
					state_ = TunnelState::Connected;
					Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::LinkStateChanged, connection, hostSteamId_, 0, loopbackPort_,
						TunnelState::Connected, {} });
				}
				break;
			}
			case TunnelState::Closed:
				Teardown(TunnelState::Failed, Internal::FailureText(event, "the host closed the Steam link"), event.endReason);
				return;
			case TunnelState::Failed:
				Teardown(TunnelState::Failed, Internal::FailureText(event, "the Steam link failed"), event.endReason);
				return;
			case TunnelState::Connecting:
				break;
			}
		}
	}

	void SteamGuestTunnel::PumpSteamToLocal()
	{
		SteamConnectionHandle connection = kInvalidSteamConnection;
		std::uint16_t loopbackPort = 0;
		{
			std::lock_guard lock{ mutex_ };
			connection = connection_;
			loopbackPort = loopbackPort_;
		}
		if (connection == kInvalidSteamConnection) return;

		const auto messages = steam_.receiveMessages(connection, kMaximumQueuedDatagrams);

		std::vector<std::vector<std::uint8_t>> outgoing;
		std::uint64_t invalid = 0;
		std::uint64_t evicted = 0;
		std::uint16_t peerPort = 0;
		{
			std::lock_guard lock{ mutex_ };
			if (connection_ != connection) return;
			peerPort = localPeerPort_;

			std::vector<std::vector<std::uint8_t>> valid;
			for (const auto& message : messages) {
				if (Internal::IsRelayableDatagram(message)) valid.push_back(message);
				else ++invalid;
			}

			if (peerPort == 0) {
				for (const auto& payload : valid) {
					evicted += Internal::PushPending(pending_, pendingBytes_, payload.data(), payload.size());
				}
			} else {
				outgoing = Internal::DrainPending(pending_, pendingBytes_);
				for (auto& payload : valid) outgoing.push_back(std::move(payload));
			}
		}

		std::uint64_t sent = 0;
		if (peerPort != 0) {
			for (const auto& payload : outgoing) {
				if (udp_.sendTo(loopbackPort, peerPort, payload)) ++sent;
			}
		}

		if (sent == 0 && invalid == 0 && evicted == 0) return;

		std::lock_guard lock{ mutex_ };
		stats_.datagramsToLocal += sent;
		stats_.datagramsDroppedInvalid += invalid;
		stats_.datagramsDroppedBufferFull += evicted;
	}

	void SteamGuestTunnel::PumpLocalToSteam()
	{
		SteamConnectionHandle connection = kInvalidSteamConnection;
		std::uint16_t loopbackPort = 0;
		{
			std::lock_guard lock{ mutex_ };
			connection = connection_;
			loopbackPort = loopbackPort_;
		}
		if (connection == kInvalidSteamConnection || loopbackPort == 0) return;

		const auto datagrams = udp_.receive(loopbackPort);

		std::vector<std::vector<std::uint8_t>> outgoing;
		std::uint64_t invalid = 0;
		std::uint64_t evicted = 0;
		{
			std::lock_guard lock{ mutex_ };
			if (connection_ != connection) return;
			std::uint16_t peerPort = 0;
			const bool connected = state_ == TunnelState::Connected;
			for (const auto& datagram : datagrams) {
				if (!Internal::IsRelayableDatagram(datagram.payload)) {
					++invalid;
					continue;
				}
				peerPort = datagram.sourcePort;
				if (connected) outgoing.push_back(datagram.payload);
				else evicted += Internal::PushPending(outboundPending_, outboundPendingBytes_, datagram.payload.data(), datagram.payload.size());
			}
			if (peerPort != 0) localPeerPort_ = peerPort;
			if (connected) {
				auto queued = Internal::DrainPending(outboundPending_, outboundPendingBytes_);
				outgoing.insert(outgoing.begin(), std::make_move_iterator(queued.begin()), std::make_move_iterator(queued.end()));
			}
		}
		if (outgoing.empty() && invalid == 0 && evicted == 0) return;

		std::uint64_t sent = 0;
		for (const auto& payload : outgoing) {
			if (steam_.sendMessage(connection, payload, kSendUnreliableNoNagle)) ++sent;
		}

		{
			std::lock_guard lock{ mutex_ };
			stats_.datagramsToSteam += sent;
			stats_.datagramsDroppedInvalid += invalid;
			stats_.datagramsDroppedBufferFull += evicted;
			if (sent != 0 && !forwardedFirstDatagram_) {
				forwardedFirstDatagram_ = true;
				Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::FirstDatagramForwarded, connection, hostSteamId_, 0, loopbackPort_,
					TunnelState::Connected, "guest to host" });
			}
		}
	}
}
