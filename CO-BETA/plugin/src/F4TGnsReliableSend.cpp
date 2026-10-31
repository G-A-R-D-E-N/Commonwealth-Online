#include "pch.h"

#include "F4TGnsWinsockShimState.h"

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
		const auto result = a_api.send(
			a_state.client,
			a_payload.data(),
			static_cast<std::uint32_t>(a_payload.size()),
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
