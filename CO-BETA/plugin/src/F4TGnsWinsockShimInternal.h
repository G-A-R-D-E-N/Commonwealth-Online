#pragma once

#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TGnsWinsockShimState.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::GnsWinsockShim
{
	constexpr std::size_t kMaximumMessageBytes = 64uz * 1024uz;

	extern thread_local std::optional<ConnectFailure> g_currentThreadConnectFailure;

	F4T::GnsWinsockShimState::BridgeApi* GetBridgeApi(std::string& a_error);
	void PreserveThreadConnectFailure(ConnectFailure a_failure);
	void RecordConnectFailure(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state, ConnectFailure a_failure);
	std::optional<ConnectFailure> TakeStateConnectFailure(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state);
	std::atomic<std::uint32_t>& IncomingSnapshotSequence(F4T::GnsWinsockShimState::SocketState& a_state, F4T::GnsSnapshotEnvelope::Family a_family) noexcept;
	std::string EventDebugText(const F4T::GnsWinsockShimState::GnsEvent& a_event);
	int ServePendingReceive(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state, char* a_buffer, int a_length);
	void QueueLegacyMessage(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state, std::string a_message);
	bool FlushReliableOutbox(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state);
	bool SendAdaptedMessages(
		const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state,
		const std::vector<F4T::GnsProtocolAdapter::OutboundMessage>& a_messages);
	void DisconnectForProtocolViolation(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state, std::string_view a_error);
	void DisconnectForSessionRejection(
		const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state,
		const F4T::GnsProtocolAdapter::SessionRejection& a_rejection);
	void DisconnectForLocalHandshakeFailure(const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state, std::string_view a_error);
}
