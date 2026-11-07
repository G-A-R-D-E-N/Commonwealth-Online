#include "pch.h"

#include "F4TWorkshopReplicationInternal.h"

#include "F4TRejectToastThrottle.h"
#include "F4TWorkshopProtocol.h"
#include "RE/S/SendHUDMessage.h"

#include <chrono>
#include <mutex>

namespace F4T::WorkshopReplicationClient
{
	namespace
	{
		RejectToastThrottle::Gate g_rejectToast;
	}

	bool HandleReject(const nlohmann::json& a_packet)
	{
		const auto reject = F4T::WorkshopProtocol::NormalizeReject(a_packet);
		if (!reject) {
			return false;
		}
		std::scoped_lock lock(g_mutex);
		auto& zone = g_zones[reject->buildZoneId];
		zone.revision = reject->revision;
		std::string form{ "unavailable" };
		if (const auto pending = g_pending.find(reject->operationId); pending != g_pending.end()) {
			const auto& key = pending->second.request.form;
			form = std::format("{}:{:08X}", key.plugin, key.localFormId);
			RollbackPending(pending->second);
			g_pending.erase(pending);
		}
		F4T::DebugLogging::Log("Workshop", "op {} rejected: {}, form {}", reject->operationId, F4T::WorkshopProtocol::StatusName(reject->status), form);
		if (g_rejectToast.ShouldShow(std::chrono::steady_clock::now())) {
			RE::SendHUDMessage::ShowHUDMessage(F4T::WorkshopProtocol::RejectNotice(reject->status), nullptr, true, true);
		}
		return true;
	}
}
