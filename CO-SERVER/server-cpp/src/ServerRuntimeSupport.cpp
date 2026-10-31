#include "ServerRuntimeSupport.h"

#include "ServerPacketDecode.h"
#include "ServerTextHelpers.h"
#include "F4TVoiceWire.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace CommonwealthOnline::Server::RuntimeSupport
{
	using PacketDecode::FramesContainType;
	using PacketDecode::JsonBytes;
	using PacketDecode::PlayerIdFromFrames;
	using PacketDecode::RawPacketType;
	using TextHelpers::Ipv4Text;

	volatile std::sig_atomic_t g_running = 1;

	void HandleSignal(int)
	{
		g_running = 0;
	}

	double UnixTimeSeconds()
	{
		return std::chrono::duration<double>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	double MonotonicSeconds()
	{
		return std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::function<std::optional<std::uint32_t>(std::uint32_t)> MakeRemoteIpv4Lookup(co_gns_server_handle a_server)
	{
		return [a_server](std::uint32_t a_connectionId) -> std::optional<std::uint32_t> {
			std::uint32_t remoteIpv4 = 0;
			std::uint16_t remotePort = 0;
			if (a_server == nullptr || co_gns_server_remote_ipv4(a_server, a_connectionId, &remoteIpv4, &remotePort) != 1) {
				return std::nullopt;
			}
			return remoteIpv4;
		};
	}

	std::string AppendPersistenceWarning(std::string a_message, std::optional<std::string> a_warning)
	{
		if (a_warning) {
			a_message += " Persistence warning: ";
			a_message += *a_warning;
		}
		return a_message;
	}

	void FlushPersistence(PersistenceGate* a_persistence, DurableStateFileStore* a_store, bool a_awaitCommit)
	{
		if (a_persistence == nullptr) {
			return;
		}
		try {
			(void)a_persistence->FlushPendingPlayerStates(a_awaitCommit);
			if (a_store != nullptr) {
				ReportPersistenceWarning(*a_store);
			}
		} catch (const std::exception& exception) {
			std::cerr << "Failed to flush pending playerState updates: " << exception.what() << '\n';
		}
	}

	void ReportPersistenceWarning(DurableStateFileStore& a_store)
	{
		if (const auto warning = a_store.TakePersistenceWarning()) {
			std::cerr << "Durable persistence warning: " << *warning << '\n';
		}
	}

	namespace
	{
		void WriteRefusal(char* a_buffer, std::size_t a_size, std::string_view a_reason)
		{
			if (a_buffer == nullptr || a_size == 0) {
				return;
			}
			const auto length = (std::min)(a_reason.size(), a_size - 1);
			std::memcpy(a_buffer, a_reason.data(), length);
			a_buffer[length] = '\0';
		}

		int AdmitPeer(AdmissionContext& a_context, const std::string& a_banKey, std::uint64_t a_peerKey, char* a_reason, std::size_t a_reasonSize)
		{
			try {
				if (a_context.bans != nullptr) {
					if (const auto ban = a_context.bans->Get(a_banKey)) {
						WriteRefusal(a_reason, a_reasonSize, "Banned: " + ban->reason);
						return 0;
					}
				}
				if (a_context.attempts != nullptr && !a_context.attempts->Allow(a_peerKey, MonotonicSeconds())) {
					WriteRefusal(a_reason, a_reasonSize, "Too many connection attempts.");
					return 0;
				}
				if (a_context.connections != nullptr) {
					std::size_t tracked = 0;
					for (const auto& entry : *a_context.connections) {
						if (!entry.second.active) {
							++tracked;
						}
					}
					const auto pending = tracked + a_context.reservedPending;
					if (pending >= F4T::ConnectionPolicy::MaximumPendingConnections(a_context.maximumPlayers)) {
						WriteRefusal(a_reason, a_reasonSize, "Too many pending connections.");
						return 0;
					}
				}
			} catch (const std::exception&) {
				WriteRefusal(a_reason, a_reasonSize, "Server could not evaluate admission.");
				return 0;
			}

			++a_context.reservedPending;
			return 1;
		}
	}

	int AdmitConnection(void* a_user, std::uint32_t a_ipv4, std::uint16_t a_port, char* a_reason, std::size_t a_reasonSize)
	{
		(void)a_port;
		auto* context = static_cast<AdmissionContext*>(a_user);
		if (context == nullptr) {
			return 1;
		}
		return AdmitPeer(*context, Ipv4Text(a_ipv4), a_ipv4, a_reason, a_reasonSize);
	}

	int AdmitAddressConnection(void* a_user, const char* a_address, std::uint16_t a_port, char* a_reason, std::size_t a_reasonSize)
	{
		(void)a_port;
		auto* context = static_cast<AdmissionContext*>(a_user);
		if (context == nullptr) {
			return 1;
		}
		const auto prefix = a_address != nullptr ? F4T::ConnectionPolicy::Ipv6Prefix64(a_address) : std::nullopt;
		if (!prefix) {
			WriteRefusal(a_reason, a_reasonSize, "Unreadable peer address.");
			return 0;
		}
		return AdmitPeer(*context, a_address, F4T::ConnectionPolicy::Ipv6PeerKey(*prefix), a_reason, a_reasonSize);
	}

	bool SendReliableJson(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		const nlohmann::json& a_packet)
	{
		const auto payload = JsonBytes(a_packet);
		return co_gns_server_send(
			a_server,
			a_connectionId,
			payload.data(),
			static_cast<std::uint32_t>(payload.size()),
			static_cast<std::uint32_t>(Delivery::ReliableOrdered)) == CO_GNS_SEND_SENT;
	}

	void SendWorldStateToConnection(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		const nlohmann::json& a_worldState,
		double a_unixTimeSeconds)
	{
		if (const auto time = a_worldState.find("timeHHmm"); time != a_worldState.end() && time->is_string()) {
			(void)SendReliableJson(a_server, a_connectionId, nlohmann::json{
				{ "type", "serverWorldState" },
				{ "timeHHmm", *time },
				{ "serverTime", a_unixTimeSeconds }
			});
		}
		if (const auto form = a_worldState.find("weatherFormId"); form != a_worldState.end() && form->is_string()) {
			nlohmann::json packet{
				{ "type", "serverWorldState" },
				{ "weatherFormId", *form },
				{ "serverTime", a_unixTimeSeconds }
			};
			(void)SendReliableJson(a_server, a_connectionId, packet);
		}
	}

	void BroadcastWorldPacket(
		co_gns_server_handle a_server,
		const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections,
		const nlohmann::json& a_packet)
	{
		for (const auto& [connectionId, state] : a_connections) {
			if (state.active) {
				(void)SendReliableJson(a_server, connectionId, a_packet);
			}
		}
	}

#include "ServerFrameDelivery.inc"

#include "ServerSessionEnded.inc"
}
