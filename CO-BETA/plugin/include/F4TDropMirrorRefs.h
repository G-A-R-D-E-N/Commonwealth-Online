#pragma once

#include "F4TDropLedger.h"

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::DropMirrorRefs
{
	bool IsMirrored(F4T::Drops::EntityId a_entityId);
	void Tick();
	std::optional<std::uint32_t> ResolveItemFormId(const std::string& a_itemKey);
}
