#pragma once

#include "F4TWorkshopTransactions.h"

namespace F4T::CampRegistration
{
	void RequestRegistration(Workshop::BuildZoneId a_buildZoneId, const Workshop::Transform& a_centre, bool a_ownCamp);
	void Forget(Workshop::BuildZoneId a_buildZoneId);
	void Tick();
	void Reset();
}
