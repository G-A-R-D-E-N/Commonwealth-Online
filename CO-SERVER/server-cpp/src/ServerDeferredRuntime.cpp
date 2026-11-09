#include "ServerDeferredRuntime.h"

#include "DurableStateFileStore.h"
#include "ServerRuntimeSupport.h"

#include <memory>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	DeferredReplyRuntime::DeferredReplyRuntime(DurableStateFileStore* a_store) :
		_store(a_store)
	{
		if (_store != nullptr) {
			_store->SetDeferCommits(true);
		}
	}

	void DeferredReplyRuntime::Erase(std::uint32_t a_connectionId)
	{
		_queue.Erase(a_connectionId);
	}

	void DeferredReplyRuntime::BeginMessage(std::uint32_t a_connectionId)
	{
		_gateConnectionId = a_connectionId;
		_ticketBefore = _store != nullptr ? _store->LastPostedTicket() : 0;
	}

	void DeferredReplyRuntime::BeginMaintenance()
	{
		BeginMessage(kMaintenanceGateConnectionId);
	}

	void DeferredReplyRuntime::EndMessage()
	{
		_gateConnectionId = 0;
		_ticketBefore = 0;
	}

	DeferredFramePolicy DeferredReplyRuntime::Policy(double a_monotonicSeconds) const
	{
		return DeferredFramePolicy{
			.queue = _store != nullptr ? std::addressof(_queue) : nullptr,
			.store = _store,
			.expiresAtMonotonic = a_monotonicSeconds + kHeldReplyLifetimeSeconds,
			.gateConnectionId = _gateConnectionId,
			.ticketBefore = _ticketBefore
		};
	}

	bool DeferredReplyRuntime::Empty() const
	{
		return _queue.Empty();
	}

	void DeferredReplyRuntime::Flush(
		co_gns_server_handle a_server,
		ProtocolHost& a_protocol,
		IdentityGate* a_identityGate,
		PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const std::function<void(std::uint32_t, const char*)>& a_disconnect,
		std::vector<std::pair<std::uint32_t, std::uint32_t>>& a_sessionReadyDelivered)
	{
		if (_queue.Empty()) {
			return;
		}
		auto policy = Policy(a_monotonicSeconds);
		policy.sessionReadyDelivered = std::addressof(a_sessionReadyDelivered);
		std::vector<std::uint32_t> retire;
		const auto abandoned = RuntimeSupport::SendDeferredFrames(
			a_server,
			a_protocol,
			a_identityGate,
			a_persistenceGate,
			a_monotonicSeconds,
			a_unixTimeSeconds,
			std::addressof(retire),
			policy);
		for (const auto connectionId : retire) {
			a_disconnect(connectionId, "Held reply send failed");
		}
		for (const auto connectionId : abandoned) {
			RuntimeSupport::SendSessionEnded(
				a_server,
				connectionId,
				"persistence_unavailable",
				"The server could not confirm your last action was saved. Reconnect to continue.",
				a_unixTimeSeconds);
			a_disconnect(connectionId, "Durable commit not confirmed");
		}
	}

	void DeferredReplyRuntime::AbandonAll(ProtocolHost& a_protocol)
	{
		for (auto& entry : _queue.Entries()) {
			for (auto& pending : entry.frames) {
				a_protocol.FrameDeliveryCompleted(pending.frame, false);
			}
		}
		_queue.Entries().clear();
	}
}
