#include "pch.h"

#include "F4TNetworkingInternal.h"
#include "F4TNetworkValidation.h"
#include "F4TWorldStateSync.h"

#include <utility>

namespace F4T::Networking::Detail
{
	void HandleServerWorldStatePacket(const Json& a_packet)
	{
		F4T::WorldStateSync::PendingServerWorldState pendingState{};
		const auto timeHHmm = ReadString(a_packet, "timeHHmm");
		if (timeHHmm && F4T::NetworkValidation::ParseServerTimeHHmm(*timeHHmm)) {
			pendingState.timeHHmm = *timeHHmm;
		}
		const auto weatherFormId = ReadString(a_packet, "weatherFormId");
		if (weatherFormId && F4T::NetworkValidation::IsValidHexFormId(*weatherFormId, false, false)) {
			pendingState.weatherFormId = *weatherFormId;
		}
		if (!pendingState.timeHHmm && !pendingState.weatherFormId) {
			LogThrottledWarning("server_world_state_invalid", "Ignoring serverWorldState packet without validated time or weather values.");
			return;
		}
		F4T::WorldStateSync::SetPendingServerWorldState(std::move(pendingState));
	}
}
