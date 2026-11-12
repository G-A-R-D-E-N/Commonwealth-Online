#pragma once

#include "ServerDeferredFrames.h"

#include "co_gns_server_bridge.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace CommonwealthOnline::Server
{
	class DurableStateFileStore;
	class IdentityGate;
	class PersistenceGate;
	class ProtocolHost;

	// Constraint: SendFrames only tests this gate id for nonzero, never against a connection id.
	inline constexpr std::uint32_t kMaintenanceGateConnectionId = 0xFFFFFFFFU;

	// Constraint: replies that promise durability wait here, never on the main loop's disk wait.
	class DeferredReplyRuntime final
	{
	public:
		explicit DeferredReplyRuntime(DurableStateFileStore* a_store);

		void Erase(std::uint32_t a_connectionId);
		void BeginMessage(std::uint32_t a_connectionId);
		void BeginMaintenance();
		void EndMessage();
		[[nodiscard]] DeferredFramePolicy Policy(double a_monotonicSeconds) const;
		[[nodiscard]] bool Empty() const;

		void Flush(
			co_gns_server_handle a_server,
			ProtocolHost& a_protocol,
			IdentityGate* a_identityGate,
			PersistenceGate* a_persistenceGate,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const std::function<void(std::uint32_t, const char*)>& a_disconnect,
			std::vector<std::pair<std::uint32_t, std::uint32_t>>& a_sessionReadyDelivered);

		void AbandonAll(ProtocolHost& a_protocol);

	private:
		mutable DeferredFrameQueue _queue;
		DurableStateFileStore* _store{};
		std::uint32_t _gateConnectionId{};
		std::uint64_t _ticketBefore{};
	};
}
