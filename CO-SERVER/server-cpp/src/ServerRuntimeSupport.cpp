#include "ServerRuntimeSupport.h"
#include "ServerReadySurvivalPolicy.h"

#include "ServerEnforcement.h"
#include "ServerPacketDecode.h"
#include "ServerTextHelpers.h"
#include "F4TPayloadCodec.h"
#include "F4TSteamId.h"
#include "F4TVoiceWire.h"
#include "F4TPayloadCodec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

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

	std::uint64_t RemoteIpv6PeerKey(co_gns_server_handle a_server, std::uint32_t a_connectionId)
	{
		std::array<char, 64> addressBuffer{};
		std::uint16_t remotePort = 0;
		if (a_server == nullptr || co_gns_server_remote_address(a_server, a_connectionId, addressBuffer.data(), addressBuffer.size(), &remotePort) != 1) {
			return 0;
		}
		const auto prefix = F4T::ConnectionPolicy::Ipv6Prefix64(addressBuffer.data());
		return prefix ? F4T::ConnectionPolicy::Ipv6PeerKey(*prefix) : 0;
	}

	RemoteEndpoint ResolveRemoteEndpoint(co_gns_server_handle a_server, std::uint32_t a_connectionId)
	{
		RemoteEndpoint endpoint{};
		std::uint16_t addressPort = 0;
		const bool hasIpv4 = co_gns_server_remote_ipv4(a_server, a_connectionId, &endpoint.ipv4, &endpoint.port) == 1;
		std::array<char, 64> addressBuffer{};
		const bool hasAddress = co_gns_server_remote_address(
			a_server, a_connectionId, addressBuffer.data(), addressBuffer.size(), &addressPort) == 1;
		if (hasAddress) {
			endpoint.address = addressBuffer.data();
		}
		if (!hasIpv4) {
			endpoint.port = addressPort;
		}
		if (hasIpv4) {
			endpoint.banKey = Ipv4Text(endpoint.ipv4);
		} else if (hasAddress) {
			endpoint.banKey = BanStore::NormalizeAddress(endpoint.address);
		}
		return endpoint;
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

	std::optional<nlohmann::json> CaptureAcceptedPlayerState(
		PersistenceGate* a_persistence,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_payload,
		const std::optional<nlohmann::json>* a_parsed)
	{
		if (a_persistence == nullptr) {
			return std::nullopt;
		}
		return a_persistence->CapturePlayerStateCandidate(a_connectionId, a_payload, a_parsed);
	}

	void QueueAcceptedPlayerState(
		PersistenceGate* a_persistence,
		std::uint32_t a_connectionId,
		const std::optional<nlohmann::json>& a_playerState,
		bool a_packetAccepted)
	{
		if (a_persistence == nullptr || !a_playerState || !a_packetAccepted) {
			return;
		}
		try {
			(void)a_persistence->QueueAcceptedPlayerState(a_connectionId, *a_playerState);
		} catch (const std::exception& exception) {
			std::cerr << "Failed to persist accepted playerState for connection " << a_connectionId << ": " << exception.what() << '\n';
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
		std::string Field(std::string_view a_value)
		{
			return SanitizeLogField(a_value);
		}

		std::mutex g_refusalMutex;
		RefusalWindows g_refusalWindows;
		std::mutex g_logMutex;

		void WriteLogLine(std::string_view a_line)
		{
			const std::lock_guard lock{ g_logMutex };
			std::cout << a_line << '\n';
		}

		void WriteRefusalLine(
			std::string_view a_stage,
			std::string_view a_peer,
			std::string_view a_code,
			std::string_view a_reason)
		{
			std::ostringstream line;
			line << "Connection refused at " << Field(a_stage)
			     << ": peer=" << Field(a_peer)
			     << " code=" << Field(a_code)
			     << " reason=\"" << Field(a_reason) << "\"";
			WriteLogLine(line.str());
		}

		void WriteRefusalTallies(const std::vector<RefusalTally>& a_closed)
		{
			for (const auto& tally : a_closed) {
				if (tally.suppressed == 0) {
					continue;
				}
				std::ostringstream line;
				line << "Connection refused: peer=" << Field(tally.peer)
				     << " suppressed " << tally.suppressed << " further refusals in the previous "
				     << static_cast<std::int64_t>(kRefusalWindowSeconds) << " s.";
				WriteLogLine(line.str());
			}
		}
	}

	std::string ConnectionAddressText(const RuntimeConnectionState& a_state)
	{
		return a_state.remoteAddress.empty() ? Ipv4Text(a_state.remoteIpv4) : a_state.remoteAddress;
	}

	void LogDisconnect(
		const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections,
		std::uint32_t a_connectionId,
		std::string_view a_endedBy,
		std::string_view a_detail,
		std::optional<int> a_endReason)
	{
		const auto found = a_connections.find(a_connectionId);
		if (found == a_connections.end()) {
			return;
		}
		const auto& state = found->second;
		const auto nowUnix = UnixTimeSeconds();
		std::ostringstream line;
		line << "Connection " << a_connectionId << " ended by " << a_endedBy
		     << ": player=" << Field(state.nickname)
		     << " profile=" << Field(state.profileId)
		     << " playerId=" << state.playerId
		     << " remote=" << ConnectionAddressText(state) << ':' << state.remotePort
		     << " sessionSeconds=" << static_cast<std::int64_t>(nowUnix - state.connectedAtUnixSeconds)
		     << " secondsSinceLastPacket=";
		if (state.lastPacketAtUnixSeconds) {
			line << static_cast<std::int64_t>(nowUnix - *state.lastPacketAtUnixSeconds);
		} else {
			line << "never";
		}
		line << " active=" << (state.active ? 1 : 0)
		     << " endReason=";
		if (a_endReason) {
			line << *a_endReason;
		} else {
			line << "none";
		}
		line << " detail=" << Field(a_detail);
		WriteLogLine(line.str());
	}

	void LogRefusal(std::string_view a_stage, std::string_view a_peer, std::string_view a_code, std::string_view a_reason)
	{
		const std::lock_guard lock{ g_refusalMutex };
		const auto report = NoteRefusal(g_refusalWindows, a_peer, MonotonicSeconds());
		if (report.writeLine) {
			WriteRefusalLine(a_stage, a_peer, a_code, a_reason);
		}
		WriteRefusalTallies(report.closed);
	}

	void SweepRefusals()
	{
		const std::lock_guard lock{ g_refusalMutex };
		WriteRefusalTallies(CloseRefusalWindows(g_refusalWindows, MonotonicSeconds()));
	}

	void LogConnectionStatus(co_gns_server_handle a_server, std::uint32_t a_connectionId, const RuntimeConnectionState& a_state)
	{
		std::ostringstream line;
		line << "Connection " << a_connectionId << " status: player=" << Field(a_state.nickname)
		     << " profile=" << Field(a_state.profileId)
		     << " playerId=" << a_state.playerId
		     << " remote=" << ConnectionAddressText(a_state) << ':' << a_state.remotePort;
		co_gns_connection_status status{};
		if (a_server != nullptr && co_gns_server_connection_status(a_server, a_connectionId, &status) == 1) {
			line << " pingMs=" << status.ping_ms
			     << " lossPenaltyPercent=" << status.loss_penalty_percent
			     << " pendingSendBytes=" << status.pending_send_bytes;
		} else {
			line << " transportStatus=unavailable";
		}
		WriteLogLine(line.str());
	}

	void LogMainLoopStall(double a_durationSeconds, std::string_view a_what)
	{
		std::ostringstream line;
		line << "Main loop stall: iteration took "
		     << static_cast<std::int64_t>(a_durationSeconds * 1000.0)
		     << " ms, ran " << Field(a_what);
		WriteLogLine(line.str());
	}

	namespace
	{
		double g_iterationWorkStartMonotonic;
		double g_iterationWorkSeconds;
	}

	// constraint: the poll wait is not main-loop work, so it must not count as a stall.
	void LogLoopIteration()
	{
		g_iterationWorkSeconds = 0.0;
		g_iterationWorkStartMonotonic = MonotonicSeconds();
	}

	void LogLoopIterationPause()
	{
		if (g_iterationWorkStartMonotonic != 0.0) {
			g_iterationWorkSeconds += MonotonicSeconds() - g_iterationWorkStartMonotonic;
			g_iterationWorkStartMonotonic = 0.0;
		}
	}

	void LogLoopIterationResume()
	{
		if (g_iterationWorkStartMonotonic == 0.0) {
			g_iterationWorkStartMonotonic = MonotonicSeconds();
		}
	}

	void LogLoopIterationEnd()
	{
		LogLoopIterationPause();
		const auto saves = DurableStateFileStore::TakeMainThreadSaveTotals();
		if (g_iterationWorkSeconds > F4T::ConnectionPolicy::DefaultMainLoopStallSeconds) {
			std::ostringstream what;
			what << "this iteration; durableSaves=" << saves.saves << " durableSaveMs=" << static_cast<std::int64_t>(saves.saveSeconds * 1000.0)
			     << " commitWaits=" << saves.commitWaits << " commitWaitMs=" << static_cast<std::int64_t>(saves.commitWaitSeconds * 1000.0);
			LogMainLoopStall(g_iterationWorkSeconds, what.str());
		}
		g_iterationWorkSeconds = 0.0;
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

		int AdmitPeer(
			AdmissionContext& a_context,
			const std::string& a_banKey,
			std::uint64_t a_peerKey,
			char* a_reason,
			std::size_t a_reasonSize,
			std::string_view a_steamId = {})
		{
			try {
				if (a_context.bans != nullptr) {
					std::optional<std::string> banReason;
					if (!a_steamId.empty()) {
						if (const auto ban = a_context.bans->GetSteamId(a_steamId)) banReason = ban->reason;
					} else if (const auto ban = a_context.bans->Get(a_banKey)) {
						banReason = ban->reason;
					}
					if (banReason) {
						LogRefusal("admission", a_banKey, "banned", *banReason);
						const auto plain = "Banned: " + *banReason;
						const auto linked = CommonwealthOnline::Server::WithAppealLink(plain, a_context.appealLink, a_reasonSize - 1);
						const auto shortLinked = CommonwealthOnline::Server::WithAppealLink("Banned.", a_context.appealLink, a_reasonSize - 1);
						WriteRefusal(a_reason, a_reasonSize, linked != plain ? linked : shortLinked != "Banned." ? shortLinked : plain);
						return 0;
					}
				}
				if (a_context.attempts != nullptr && !a_context.attempts->Allow(a_peerKey, MonotonicSeconds())) {
					LogRefusal("admission", a_banKey, "connect_rate_limit", "Too many connection attempts.");
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
						LogRefusal("admission", a_banKey, "pending_limit", "Too many pending connections.");
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
		auto* context = static_cast<AdmissionContext*>(a_user);
		if (context == nullptr) {
			return 1;
		}
		if (context->steamLink != nullptr) {
			if (const auto tunnelGuest = context->steamLink->Resolve(a_ipv4, a_port, {})) {
				if (const auto key = F4T::ConnectionPolicy::SteamPeerKey(F4T::SteamId::ParseIndividual(tunnelGuest->steamId).value_or(0))) {
					return AdmitPeer(*context, "steam:" + tunnelGuest->steamId, key, a_reason, a_reasonSize, tunnelGuest->steamId);
				}
			}
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

	nlohmann::json RuntimeClientsJson(const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections)
	{
		nlohmann::json clients = nlohmann::json::array();
		for (const auto& [connectionId, state] : a_connections) {
			(void)connectionId;
			if (!state.active || state.playerId == 0) {
				continue;
			}
			nlohmann::json client{
				{ "player_id", state.playerId },
				{ "address", state.remoteAddress.empty() ? Ipv4Text(state.remoteIpv4) : state.remoteAddress },
				{ "port", state.remotePort },
				{ "connected_at", state.connectedAtUnixSeconds }
			};
			if (state.lastPacketAtUnixSeconds) {
				client["last_packet_at"] = *state.lastPacketAtUnixSeconds;
			}
			if (!state.steamId.empty()) client["steam_id"] = state.steamId;
			clients.push_back(std::move(client));
		}
		return nlohmann::json{
			{ "total_clients", clients.size() },
			{ "clients", std::move(clients) }
		};
	}
}
