#pragma once

#include "BanStore.h"
#include "DurableStateFileStore.h"
#include "F4TConnectionPolicy.h"
#include "F4TMessageFragments.h"
#include "F4TPacketRateLimit.h"
#include "IdentityGate.h"
#include "PersistenceGate.h"
#include "ServerDeferredFrames.h"
#include "ServerLogSanitizer.h"
#include "ServerPacketDecode.h"
#include "ServerRefusalWindow.h"
#include "ServerTextHelpers.h"
#include "ServerProtocol.h"

#include "co_gns_server_bridge.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server::RuntimeSupport
{
	extern volatile std::sig_atomic_t g_running;

	using PacketDecode::FramesContainType;
	using PacketDecode::JsonBytes;
	using PacketDecode::PlayerIdFromFrames;
	using PacketDecode::RawPacketType;
	using TextHelpers::Ipv4Text;

	struct RuntimeConnectionState
	{
		double connectedAtUnixSeconds{};
		std::optional<double> lastPacketAtUnixSeconds;
		std::optional<double> lastActivityAtUnixSeconds;
		double nextStatusReportMonotonic{};
		std::uint32_t remoteIpv4{};
		std::uint16_t remotePort{};
		std::string remoteAddress;
		std::uint32_t playerId{};
		std::string profileId;
		std::string nickname;
		std::string steamId;
		bool active{};
		bool steamHost{};
		std::uint64_t admissionTicket{};
		F4T::PacketRateLimit::State rate;
		F4T::PacketRateLimit::State voiceRate;
		F4T::PacketRateLimit::State wireRate;
		double joinedAtMonotonicSeconds{ std::numeric_limits<double>::infinity() };
		std::map<std::string, std::uint64_t> inboundWindowBytes;
		F4T::MessageFragments::Assembler fragments;
	};

	struct AdmissionContext
	{
		BanStore* bans{};
		F4T::ConnectionPolicy::ConnectAttemptLimiter* attempts{};
		const std::unordered_map<std::uint32_t, RuntimeConnectionState>* connections{};
		std::size_t maximumPlayers{};
		std::size_t reservedPending{};
		std::string appealLink;
	};

	void HandleSignal(int);

	double UnixTimeSeconds();

	double MonotonicSeconds();

	std::string AppendPersistenceWarning(std::string a_message, std::optional<std::string> a_warning);

	void ReportPersistenceWarning(DurableStateFileStore& a_store);

	void LogDisconnect(
		const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections,
		std::uint32_t a_connectionId,
		std::string_view a_endedBy,
		std::string_view a_detail,
		std::optional<int> a_endReason = std::nullopt);

	void LogRefusal(std::string_view a_stage, std::string_view a_peer, std::string_view a_code, std::string_view a_reason);

	void SweepRefusals();

	void LogConnectionStatus(co_gns_server_handle a_server, std::uint32_t a_connectionId, const RuntimeConnectionState& a_state);

	void LogMainLoopStall(double a_durationSeconds, std::string_view a_what);

	void LogLoopIteration();
	void LogLoopIterationPause();
	void LogLoopIterationResume();
	void LogLoopIterationEnd();

	// constraint: main loop thread only; the poll wait between Pause and Resume is not counted as work.
	template <typename Poll>
	auto TimedLoopPoll(Poll&& a_poll)
	{
		LogLoopIterationPause();
		const auto result = a_poll();
		LogLoopIterationResume();
		return result;
	}

	[[nodiscard]] std::string ConnectionAddressText(const RuntimeConnectionState& a_state);

	void FlushPersistence(PersistenceGate* a_persistence, DurableStateFileStore* a_store, bool a_awaitCommit);

	[[nodiscard]] std::optional<nlohmann::json> CaptureAcceptedPlayerState(
		PersistenceGate* a_persistence,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_payload);

	void QueueAcceptedPlayerState(
		PersistenceGate* a_persistence,
		std::uint32_t a_connectionId,
		const std::optional<nlohmann::json>& a_playerState,
		bool a_packetAccepted);

	int AdmitConnection(void* a_user, std::uint32_t a_ipv4, std::uint16_t a_port, char* a_reason, std::size_t a_reasonSize);
	int AdmitAddressConnection(void* a_user, const char* a_address, std::uint16_t a_port, char* a_reason, std::size_t a_reasonSize);

	[[nodiscard]] std::function<std::optional<std::uint32_t>(std::uint32_t)> MakeRemoteIpv4Lookup(co_gns_server_handle a_server);

	[[nodiscard]] std::uint64_t RemoteIpv6PeerKey(co_gns_server_handle a_server, std::uint32_t a_connectionId);

	struct RemoteEndpoint
	{
		std::uint32_t ipv4{};
		std::uint16_t port{};
		std::string address;
		std::optional<std::string> banKey;
	};

	[[nodiscard]] RemoteEndpoint ResolveRemoteEndpoint(co_gns_server_handle a_server, std::uint32_t a_connectionId);

	bool SendReliableJson(co_gns_server_handle a_server, std::uint32_t a_connectionId, const nlohmann::json& a_packet);

	void SendWorldStateToConnection(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		const nlohmann::json& a_worldState,
		double a_unixTimeSeconds);

	void BroadcastWorldPacket(
		co_gns_server_handle a_server,
		const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections,
		const nlohmann::json& a_packet);

	void SendFrames(
		co_gns_server_handle a_server,
		ProtocolHost& a_protocol,
		std::span<OutboundFrame> a_frames,
		IdentityGate* a_identityGate,
		PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections,
		const DeferredFramePolicy& a_deferred);

	[[nodiscard]] std::vector<std::uint32_t> SendDeferredFrames(
		co_gns_server_handle a_server,
		ProtocolHost& a_protocol,
		IdentityGate* a_identityGate,
		PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections,
		const DeferredFramePolicy& a_deferred);

	void SendSessionEnded(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::string_view a_code,
		std::string_view a_reason,
		double a_unixTimeSeconds,
		const nlohmann::json& a_requiredModpack,
		std::string_view a_modpackFingerprint = {},
		const nlohmann::json& a_modpackChunks = nlohmann::json{},
		std::string_view a_modpackName = {});

	inline void SendSessionEnded(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::string_view a_code,
		std::string_view a_reason,
		double a_unixTimeSeconds)
	{
		SendSessionEnded(a_server, a_connectionId, a_code, a_reason, a_unixTimeSeconds, nlohmann::json{});
	}
	[[nodiscard]] nlohmann::json RuntimeClientsJson(const std::unordered_map<std::uint32_t, RuntimeConnectionState>& a_connections);
}
