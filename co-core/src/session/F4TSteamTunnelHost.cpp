// SPDX-License-Identifier: GPL-3.0-or-later
// Design informed by FathersFallout (FatherLamb), GPL-3.0 with additional terms; used with the author's permission.
#include "F4TSteamTunnel.h"

#include "F4TSteamTunnelInternal.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Transport
{
	SteamHostTunnel::SteamHostTunnel(SteamApi a_steam, UdpSocketApi a_udp) :
		steam_{ std::move(a_steam) },
		udp_{ std::move(a_udp) }
	{
	}

	SteamHostTunnel::~SteamHostTunnel()
	{
		Stop();
	}

	void SteamHostTunnel::SetLog(TunnelLog a_log)
	{
		std::lock_guard lock{ mutex_ };
		log_ = std::move(a_log);
	}

	void SteamHostTunnel::SetClock(std::function<double()> a_now)
	{
		std::lock_guard lock{ mutex_ };
		clock_ = std::move(a_now);
	}

	double SteamHostTunnel::NowSeconds() const
	{
		return clock_ ? clock_() : std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void SteamHostTunnel::Log(const TunnelLog& a_log, std::string_view a_line) const
	{
		if (!a_log) return;
		try {
			a_log(a_line);
		} catch (...) {
		}
	}

	// Start must not run concurrently with Pump: Pump is gated on running_, which only Start sets.
	TunnelError SteamHostTunnel::Start(std::uint16_t a_serverPort, SteamAdmissionPolicy a_policy, std::size_t a_maximumConnections, SteamIdentityBinder a_binder)
	{
		if (!steam_.IsComplete()) return TunnelError::SteamUnavailable;
		if (!udp_.IsComplete()) return TunnelError::LoopbackSocketFailed;
		if (a_serverPort == 0) return TunnelError::InvalidArgument;
		if (a_maximumConnections == 0 || a_maximumConnections > kMaximumTunnelConnections) return TunnelError::InvalidArgument;
		if (!a_binder.IsEmpty() && !a_binder.IsComplete()) return TunnelError::InvalidArgument;
		if (!steam_.initRelay()) return TunnelError::RelayNotReady;

		std::lock_guard lock{ mutex_ };
		if (running_) return TunnelError::None;

		const auto listenSocket = steam_.createListenSocketP2P(kSteamTunnelVirtualPort);
		if (listenSocket == kInvalidSteamListenSocket) return TunnelError::ListenSocketFailed;

		listenSocket_ = listenSocket;
		serverPort_ = a_serverPort;
		maximumConnections_ = a_maximumConnections;
		policy_ = std::move(a_policy);
		binder_ = std::move(a_binder);
		running_ = true;
		Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::TunnelStarted, kInvalidSteamConnection, 0, 0, a_serverPort,
			TunnelState::Connected, binder_.IsEmpty() ? std::string{ "guests join unverified" } : std::string{} });
		return TunnelError::None;
	}

	bool SteamHostTunnel::IsRunning() const
	{
		std::lock_guard lock{ mutex_ };
		return running_;
	}

	SteamTunnelStats SteamHostTunnel::Stats() const
	{
		std::lock_guard lock{ mutex_ };
		return stats_;
	}

	std::vector<std::uint64_t> SteamHostTunnel::PeerSteamIds() const
	{
		std::lock_guard lock{ mutex_ };
		std::vector<std::uint64_t> peers;
		peers.reserve(relays_.size());
		for (const auto& relay : relays_) peers.push_back(relay.peerSteamId);
		return peers;
	}

	std::vector<TunnelEvent> SteamHostTunnel::DrainEvents()
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

	void SteamHostTunnel::Record(TunnelEvent a_event)
	{
		std::lock_guard lock{ mutex_ };
		Internal::PushEvent(events_, std::move(a_event));
	}

	void SteamHostTunnel::Pump()
	{
		{
			std::lock_guard lock{ mutex_ };
			if (!running_) return;
		}

		steam_.runCallbacks();
		HandleEvents();
		SweepWelcomeDeadlines();
		PumpSteamToLocal();
		PumpLocalToSteam();
	}

	void SteamHostTunnel::Stop()
	{
		std::vector<Relay> relays;
		std::vector<std::thread> workers;
		SteamListenSocketHandle listenSocket = kInvalidSteamListenSocket;
		SteamIdentityBinder binder;
		{
			std::lock_guard lock{ mutex_ };
			if (!running_ && admissionWorkers_.empty()) return;
			running_ = false;
			relays.swap(relays_);
			workers.swap(admissionWorkers_);
			listenSocket = listenSocket_;
			listenSocket_ = kInvalidSteamListenSocket;
			binder = std::move(binder_);
			binder_ = {};
			stats_.activeConnections = 0;
		}

		for (auto& worker : workers) {
			if (worker.joinable()) worker.join();
		}
		for (auto& relay : relays) {
			steam_.closeConnection(relay.connection, 0, "tunnel stopped");
			for (const auto port : { relay.localPort, relay.pendingPort }) {
				if (port == 0) continue;
				if (binder.unbind) binder.unbind(port);
				udp_.close(port);
			}
		}
		if (listenSocket != kInvalidSteamListenSocket) steam_.closeListenSocket(listenSocket);
		Record(TunnelEvent{ TunnelEventKind::TunnelStopped });
	}

	SteamHostTunnel::Relay* SteamHostTunnel::FindRelay(SteamConnectionHandle a_connection)
	{
		const auto found = std::find_if(relays_.begin(), relays_.end(), [a_connection](const Relay& a_relay) {
			return a_relay.connection == a_connection;
		});
		return found == relays_.end() ? nullptr : &*found;
	}

	void SteamHostTunnel::RefuseConnection(
		SteamConnectionHandle a_connection,
		int a_reason,
		std::string_view a_debug,
		bool a_capacity)
	{
		{
			std::lock_guard lock{ mutex_ };
			if (a_capacity) ++stats_.connectionsRefusedCapacity;
			else ++stats_.connectionsRejected;
		}
		steam_.closeConnection(a_connection, a_reason, a_debug);
	}

	void SteamHostTunnel::HandleEvents()
	{
		for (const auto& event : steam_.pollConnectionEvents()) {
			switch (event.state) {
			case TunnelState::Connecting:
				AdmitConnection(event.connection, event.peerSteamId);
				break;
			case TunnelState::Closed:
			case TunnelState::Failed:
				DropConnection(event.connection, "steam connection ended");
				break;
			case TunnelState::Connected:
				break;
			}
		}
	}

	void SteamHostTunnel::AdmitConnection(SteamConnectionHandle a_connection, std::uint64_t a_peerSteamId)
	{
		SteamAdmissionPolicy policy;
		SteamIdentityBinder binder;
		TunnelLog log;
		SteamAdmission::TunnelRequest request{ .peerSteamId = a_peerSteamId };
		const auto superseded = [a_peerSteamId](const Relay& a_relay) {
			return a_peerSteamId != 0 && a_relay.peerSteamId == a_peerSteamId;
		};
		{
			std::lock_guard lock{ mutex_ };
			if (!running_) return;
			if (FindRelay(a_connection) != nullptr) return;
			policy = policy_;
			binder = binder_;
			log = log_;
			request.relayed = relays_.size() - static_cast<std::size_t>(std::count_if(relays_.begin(), relays_.end(), superseded));
			request.maximumRelayed = maximumConnections_;
		}

		const auto tag = "admission connection=" + std::to_string(a_connection);
		SteamAdmission::TunnelDecision decision;
		const auto hostPolicy = binder.hostPolicy.value_or(SteamAdmission::HostPolicy{});
		try {
			request.friendCheck = !policy ? SteamAdmission::FriendCheck::Unavailable
				: policy(a_peerSteamId) ? SteamAdmission::FriendCheck::Friend : SteamAdmission::FriendCheck::NotFriend;
			request.identityLinked = binder.IsComplete();
			decision = SteamAdmission::DecideTunnel(hostPolicy, request);
		} catch (...) {
			Log(log, tag + " outcome=refused reason=policy_error");
			Record(TunnelEvent{ TunnelEventKind::PeerRefused, a_connection, a_peerSteamId, kConnectionEndNotPermitted, 0, TunnelState::Closed, "admission check failed" });
			RefuseConnection(a_connection, kConnectionEndNotPermitted, "admission check failed", false);
			return;
		}
		Log(log, SteamAdmission::DescribeTunnelDecision(a_connection, hostPolicy, request, decision));
		if (!decision.Admitted()) {
			const std::string outcome{ SteamAdmission::OutcomeName(decision.outcome) };
			Record(TunnelEvent{ TunnelEventKind::PeerRefused, a_connection, a_peerSteamId, decision.closeReason, 0, TunnelState::Closed, outcome });
			RefuseConnection(a_connection, decision.closeReason, outcome, decision.outcome == SteamAdmission::Outcome::ServerFull);
			return;
		}

		const auto localPort = udp_.openLoopback();
		if (!localPort) {
			Log(log, tag + " outcome=refused reason=local_socket_failed");
			Record(TunnelEvent{ TunnelEventKind::PeerRefused, a_connection, a_peerSteamId, kConnectionEndLocalSocketFailed, 0, TunnelState::Closed, "local socket failed" });
			RefuseConnection(a_connection, kConnectionEndLocalSocketFailed, "local socket failed", false);
			return;
		}

		if (!steam_.acceptConnection(a_connection)) {
			udp_.close(*localPort);
			Log(log, tag + " outcome=refused reason=accept_failed");
			Record(TunnelEvent{ TunnelEventKind::PeerRefused, a_connection, a_peerSteamId, kConnectionEndNotPermitted, *localPort, TunnelState::Closed, "accept failed" });
			RefuseConnection(a_connection, kConnectionEndNotPermitted, "accept failed", false);
			return;
		}

		bool stopped = false;
		bool full = false;
		std::size_t relayed = 0;
		std::vector<SteamConnectionHandle> replaced;
		{
			std::lock_guard lock{ mutex_ };
			stopped = !running_ || FindRelay(a_connection) != nullptr;
			const auto active = relays_.size() - static_cast<std::size_t>(std::count_if(relays_.begin(), relays_.end(), superseded));
			full = !stopped && active >= maximumConnections_;
			if (!stopped && !full) {
				for (const auto& relay : relays_) {
					if (superseded(relay)) replaced.push_back(relay.connection);
				}
				relays_.push_back(Relay{ a_connection, a_peerSteamId, static_cast<std::uint16_t>(binder.bind ? 0 : *localPort), static_cast<std::uint16_t>(binder.bind ? *localPort : 0), {}, 0,
					NowSeconds() + kRelayWelcomeTimeoutSeconds, false, replaced });
				++stats_.connectionsAccepted;
				stats_.activeConnections = static_cast<std::uint32_t>(relays_.size());
				relayed = relays_.size();
				Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::PeerAccepted, a_connection, a_peerSteamId, 0, *localPort, TunnelState::Connecting, {} });
			}
		}
		if (stopped || full) {
			udp_.close(*localPort);
			if (!full) {
				steam_.closeConnection(a_connection, 0, "tunnel stopped");
				return;
			}
			Log(log, tag + " outcome=refused reason=server_full");
			Record(TunnelEvent{ TunnelEventKind::PeerRefused, a_connection, a_peerSteamId, kConnectionEndServerFull, 0, TunnelState::Closed, "server full" });
			RefuseConnection(a_connection, kConnectionEndServerFull, "server full", true);
			return;
		}

		if (!binder.bind) {
			for (const auto old : replaced) {
				Log(log, "admission connection=" + std::to_string(old) + " replaced by connection=" + std::to_string(a_connection) + " from the same Steam account");
				DropConnection(old, "replaced by a newer connection from the same Steam account");
			}
			Log(log, tag + " outcome=relay_ready relayed=" + std::to_string(relayed) + "/" + std::to_string(request.maximumRelayed));
			return;
		}
		StartIdentityBinder(a_connection, *localPort, std::move(binder), a_peerSteamId, decision.passwordRequired);
	}

	void SteamHostTunnel::StartIdentityBinder(
		SteamConnectionHandle a_connection,
		std::uint16_t a_port,
		SteamIdentityBinder a_binder,
		std::uint64_t a_peerSteamId,
		bool a_passwordRequired)
	{
		try {
			std::thread worker([this, a_connection, a_port, binder = std::move(a_binder), a_peerSteamId, a_passwordRequired] {
				const bool bound = binder.bind && binder.bind(a_port, a_peerSteamId, a_passwordRequired);
				bool ownsPort = true;
				bool ready = false;
				bool closeConnection = false;
				int closeReason = 0;
				std::string_view closeDebug{};
				std::vector<SteamConnectionHandle> replaced;
				std::string readyLine;
				TunnelLog log;
				{
					std::lock_guard lock{ mutex_ };
					log = log_;
					auto* relay = FindRelay(a_connection);
					if (relay == nullptr) {
						ownsPort = false;
					} else if (bound && running_) {
						relay->pendingPort = 0;
						relay->localPort = a_port;
						replaced.swap(relay->supersedes);
						ready = true;
						readyLine = "admission connection=" + std::to_string(a_connection) + " outcome=relay_ready relayed=" +
							std::to_string(relays_.size()) + "/" + std::to_string(maximumConnections_);
						Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::IdentityBound, a_connection, a_peerSteamId, 0, a_port, TunnelState::Connecting, {} });
					} else {
						relays_.erase(relays_.begin() + (relay - relays_.data()));
						stats_.activeConnections = static_cast<std::uint32_t>(relays_.size());
						if (!bound) ++stats_.identityBindFailures;
						else ++stats_.connectionsRejected;
						Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::IdentityBindFailed, a_connection, a_peerSteamId,
							running_ ? kConnectionEndIdentityUnlinked : 0, a_port, TunnelState::Failed,
							running_ ? "identity link failed" : "tunnel stopped" });
						closeConnection = true;
						closeReason = running_ ? kConnectionEndIdentityUnlinked : 0;
						closeDebug = running_ ? "identity link failed" : "tunnel stopped";
					}
				}
				if (ready) {
					for (const auto old : replaced) {
						Log(log, "admission connection=" + std::to_string(old) + " replaced by connection=" + std::to_string(a_connection) + " from the same Steam account");
						DropConnection(old, "replaced by a newer connection from the same Steam account");
					}
					Log(log, readyLine);
					return;
				}
				if (closeConnection) steam_.closeConnection(a_connection, closeReason, closeDebug);
				if (bound && binder.unbind) binder.unbind(a_port);
				if (ownsPort) udp_.close(a_port);
			});
			{
				std::lock_guard lock{ mutex_ };
				admissionWorkers_.push_back(std::move(worker));
			}
		} catch (const std::exception&) {
			std::lock_guard lock{ mutex_ };
			const auto found = std::find_if(relays_.begin(), relays_.end(), [a_connection](const Relay& a_relay) {
				return a_relay.connection == a_connection;
			});
			if (found == relays_.end()) return;
			relays_.erase(found);
			stats_.activeConnections = static_cast<std::uint32_t>(relays_.size());
			++stats_.identityBindFailures;
			steam_.closeConnection(a_connection, kConnectionEndIdentityUnlinked, "identity link failed");
			udp_.close(a_port);
		}
	}

	void SteamHostTunnel::DropConnection(SteamConnectionHandle a_connection, std::string_view a_debug)
	{
		std::uint16_t localPort = 0;
		std::uint16_t pendingPort = 0;
		std::uint64_t peerSteamId = 0;
		std::function<void(std::uint16_t)> unbind;
		{
			std::lock_guard lock{ mutex_ };
			const auto found = std::find_if(relays_.begin(), relays_.end(), [a_connection](const Relay& a_relay) {
				return a_relay.connection == a_connection;
			});
			if (found == relays_.end()) return;
			localPort = found->localPort;
			pendingPort = found->pendingPort;
			peerSteamId = found->peerSteamId;
			unbind = binder_.unbind;
			relays_.erase(found);
			stats_.activeConnections = static_cast<std::uint32_t>(relays_.size());
			Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::PeerDropped, a_connection, peerSteamId, 0, localPort, TunnelState::Closed, std::string{ a_debug } });
		}
		steam_.closeConnection(a_connection, 0, a_debug);
		for (const auto port : { localPort, pendingPort }) {
			if (port == 0) continue;
			if (unbind) unbind(port);
			udp_.close(port);
		}
	}

	void SteamHostTunnel::SweepWelcomeDeadlines()
	{
		std::vector<SteamConnectionHandle> expired;
		TunnelLog log;
		{
			std::lock_guard lock{ mutex_ };
			const auto now = NowSeconds();
			log = log_;
			for (const auto& relay : relays_) {
				if (relay.welcomeDeadline > 0.0 && now >= relay.welcomeDeadline) expired.push_back(relay.connection);
			}
		}
		for (const auto connection : expired) {
			Log(log, "admission connection=" + std::to_string(connection) + " dropped reason=welcome_timeout");
			DropConnection(connection, "no server traffic before welcome");
		}
	}

	void SteamHostTunnel::PumpSteamToLocal()
	{
		std::vector<std::pair<SteamConnectionHandle, std::uint16_t>> relays;
		{
			std::lock_guard lock{ mutex_ };
			relays.reserve(relays_.size());
			for (const auto& relay : relays_) relays.emplace_back(relay.connection, relay.localPort);
		}

		for (const auto& [connection, localPort] : relays) {
			const auto messages = steam_.receiveMessages(connection, kMaximumQueuedDatagrams);

			std::vector<std::vector<std::uint8_t>> outgoing;
			std::uint64_t invalid = 0;
			std::uint64_t evicted = 0;
			{
				std::lock_guard lock{ mutex_ };
				auto* relay = FindRelay(connection);
				if (relay == nullptr) continue;

				std::vector<std::vector<std::uint8_t>> valid;
				for (const auto& message : messages) {
					if (Internal::IsRelayableDatagram(message)) valid.push_back(message);
					else ++invalid;
				}

				if (localPort == 0) {
					for (const auto& payload : valid) {
						evicted += Internal::PushPending(relay->pending, relay->pendingBytes, payload.data(), payload.size());
					}
				} else {
					outgoing = Internal::DrainPending(relay->pending, relay->pendingBytes);
					for (auto& payload : valid) outgoing.push_back(std::move(payload));
				}
			}

			std::uint64_t sent = 0;
			for (const auto& payload : outgoing) {
				if (udp_.sendTo(localPort, serverPort_, payload)) ++sent;
			}

			if (sent == 0 && invalid == 0 && evicted == 0) continue;

			std::lock_guard lock{ mutex_ };
			stats_.datagramsToLocal += sent;
			stats_.datagramsDroppedInvalid += invalid;
			stats_.datagramsDroppedBufferFull += evicted;
			if (sent != 0) {
				auto* relay = FindRelay(connection);
				if (relay != nullptr && !relay->forwardedFirstDatagram) {
					relay->forwardedFirstDatagram = true;
					Internal::PushEvent(events_, TunnelEvent{ TunnelEventKind::FirstDatagramForwarded, connection, relay->peerSteamId, 0,
						localPort, TunnelState::Connected, "host to guest" });
				}
			}
		}
	}

	void SteamHostTunnel::PumpLocalToSteam()
	{
		std::vector<std::pair<SteamConnectionHandle, std::uint16_t>> relays;
		{
			std::lock_guard lock{ mutex_ };
			relays.reserve(relays_.size());
			for (const auto& relay : relays_) relays.emplace_back(relay.connection, relay.localPort);
		}

		for (const auto& [connection, localPort] : relays) {
			if (localPort == 0) continue;

			const auto datagrams = udp_.receive(localPort);
			if (datagrams.empty()) continue;

			std::vector<std::vector<std::uint8_t>> outgoing;
			std::uint64_t invalid = 0;
			for (const auto& datagram : datagrams) {
				if (datagram.sourcePort != serverPort_) continue;
				if (Internal::IsRelayableDatagram(datagram.payload)) outgoing.push_back(datagram.payload);
				else ++invalid;
			}

			std::uint64_t sent = 0;
			for (const auto& payload : outgoing) {
				if (steam_.sendMessage(connection, payload, kSendUnreliableNoNagle)) ++sent;
			}

			std::lock_guard lock{ mutex_ };
			stats_.datagramsToSteam += sent;
			stats_.datagramsDroppedInvalid += invalid;
			if (sent != 0) {
				if (auto* relay = FindRelay(connection); relay != nullptr) relay->welcomeDeadline = 0.0;
			}
		}
	}

	std::size_t ClampHostMaximumConnections(std::size_t a_desiredConnections, std::size_t a_activeConnections)
	{
		const auto ceiling = kMaximumTunnelConnections;
		const auto active = a_activeConnections > ceiling ? ceiling : a_activeConnections;
		const auto desired = a_desiredConnections > ceiling ? ceiling : a_desiredConnections;
		return desired < active ? active : desired;
	}

	void SteamHostTunnel::SetMaximumConnections(std::size_t a_maximumConnections)
	{
		std::lock_guard lock{ mutex_ };
		maximumConnections_ = ClampHostMaximumConnections(a_maximumConnections, relays_.size());
	}
}
