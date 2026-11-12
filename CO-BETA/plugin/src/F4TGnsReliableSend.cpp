#include "pch.h"

#include "F4TGnsWinsockShimState.h"
#include "F4TMessageFragments.h"
#include "F4TPayloadCodec.h"
#include "F4TTransportLimits.h"

#include <span>
#include <vector>

namespace F4T::GnsWinsockShimState
{
	void SetSendFailure(SocketState& a_state, int a_result)
	{
		if (a_result == kSendTooLarge) {
			SetError(WSAEMSGSIZE);
		} else if (a_result == kSendNotConnected) {
			a_state.connected.store(false, std::memory_order_release);
			SetError(WSAENOTCONN);
		} else if (a_result == kSendBackpressure || a_result == kSendDropped) {
			SetError(WSAENOBUFS);
		} else {
			SetError(WSAECONNABORTED);
		}
	}

	F4T::ReliableOutbox::Attempt AttemptReliableSend(BridgeApi& a_api, SocketState& a_state, std::string_view a_payload)
	{
		const auto wire = F4T::PayloadCodec::Encode(std::span<const std::uint8_t>{
			reinterpret_cast<const std::uint8_t*>(a_payload.data()),
			a_payload.size() });
		const auto maxBytes = a_api.maximumMessageBytes
			? a_api.maximumMessageBytes()
			: F4T::TransportLimits::DefaultMaximumMessageBytes;
		if (wire.size() <= maxBytes) {
			const auto result = a_api.send(
				a_state.client,
				wire.data(),
				static_cast<std::uint32_t>(wire.size()),
				static_cast<std::uint32_t>(F4T::GnsProtocolAdapter::Delivery::ReliableOrdered));
			if (result == kSendSent) {
				return F4T::ReliableOutbox::Attempt::Sent;
			}
			if (result == kSendBackpressure) {
				return F4T::ReliableOutbox::Attempt::Backpressure;
			}
			SetSendFailure(a_state, result);
			return F4T::ReliableOutbox::Attempt::Failed;
		}

		const auto parts = F4T::MessageFragments::Split(
			a_state.nextFragmentMessageId.fetch_add(1, std::memory_order_relaxed),
			wire,
			maxBytes);
		if (parts.empty()) {
			SetError(WSAEMSGSIZE);
			return F4T::ReliableOutbox::Attempt::Failed;
		}
		for (const auto& part : parts) {
			const auto result = a_api.send(
				a_state.client,
				part.data(),
				static_cast<std::uint32_t>(part.size()),
				static_cast<std::uint32_t>(F4T::GnsProtocolAdapter::Delivery::ReliableOrdered));
			if (result == kSendSent) {
				continue;
			}
			if (result == kSendBackpressure) {
				return F4T::ReliableOutbox::Attempt::Backpressure;
			}
			SetSendFailure(a_state, result);
			return F4T::ReliableOutbox::Attempt::Failed;
		}
		return F4T::ReliableOutbox::Attempt::Sent;
	}

	bool DrainReliableOutboxLocked(BridgeApi& a_api, SocketState& a_state)
	{
		const auto result = a_state.reliableOutbox.Flush(
			std::chrono::steady_clock::now(),
			[&](std::string_view a_payload) { return AttemptReliableSend(a_api, a_state, a_payload); });
		if (result == F4T::ReliableOutbox::FlushResult::TimedOut) {
			SetError(WSAENOBUFS);
			return false;
		}
		return result != F4T::ReliableOutbox::FlushResult::Failed;
	}
}
