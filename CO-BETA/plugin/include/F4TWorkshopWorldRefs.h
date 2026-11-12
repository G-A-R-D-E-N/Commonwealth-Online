#pragma once

#include "F4TWorkshopTransactions.h"

#include <cstdint>
#include <set>

namespace F4T::WorkshopWorldRefs
{
	void Hide(std::uint32_t a_formId, const Workshop::Transform& a_transform);
	[[nodiscard]] std::set<std::uint32_t> Detach();
	void Restore(const std::set<std::uint32_t>& a_formIds);
}
