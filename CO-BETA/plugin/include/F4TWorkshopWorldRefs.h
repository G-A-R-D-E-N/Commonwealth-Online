#pragma once

#include "F4TWorkshopTransactions.h"

#include <cstdint>

namespace F4T::WorkshopWorldRefs
{
	void Hide(std::uint32_t a_formId, const Workshop::Transform& a_transform);
	void Restore();
}
