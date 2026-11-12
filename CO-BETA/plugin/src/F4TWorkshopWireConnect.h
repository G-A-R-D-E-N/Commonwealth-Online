#pragma once

#include "F4TWorkshopReplicationInternal.h"

namespace F4T::WorkshopReplicationClient
{
	void QueueWireConnect(const ZoneState& a_zone, Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_wireId, RE::ObjectRefHandle a_wireHandle);
}
