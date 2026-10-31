#include "pch.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TObjectSync.h"

namespace F4T::Networking
{
	bool SendObjectActivateRequest(const nlohmann::json& a_request)
	{
		if (!IsConnectedToServer()) {
			return false;
		}
		const auto normalized = F4T::ObjectWire::NormalizeActivateRequest(a_request);
		return normalized && Detail::SendPacketBytes(normalized->dump());
	}

	bool SendObjectLockRequest(const nlohmann::json& a_request)
	{
		if (!IsConnectedToServer()) {
			return false;
		}
		const auto normalized = F4T::ObjectWire::NormalizeLockRequest(a_request);
		return normalized && Detail::SendPacketBytes(normalized->dump());
	}
}

namespace F4T::Networking::Detail
{
	void HandleObjectPacket(const Json& a_packet)
	{
		std::optional<Json> normalized;
		const auto type = a_packet.value("type", std::string{});
		if (type == "objectState") {
			normalized = F4T::ObjectWire::NormalizeObjectState(a_packet);
		} else if (type == "objectStateSnapshot") {
			normalized = F4T::ObjectWire::NormalizeObjectStateSnapshot(a_packet);
		} else if (type == "objectStateRejected") {
			normalized = F4T::ObjectWire::NormalizeObjectStateRejected(a_packet);
			if (!normalized) {
				LogThrottledWarning("object_packet_malformed", "Ignoring malformed object state packet.");
				return;
			}
			auto reason = normalized->value("reason", std::string{});
			if (reason.size() > 64) {
				reason.resize(64);
			}
			LogThrottledWarning("object_state_rejected", std::string("Server rejected an object request: ").append(reason));
			return;
		}
		if (!normalized) {
			LogThrottledWarning("object_packet_malformed", "Ignoring malformed object state packet.");
			return;
		}
		F4T::ObjectSync::EnqueuePacket(*normalized);
	}
}
