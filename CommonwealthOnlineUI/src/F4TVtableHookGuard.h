#pragma once

#include "pch.h"

namespace F4T::VtableHookGuard
{
	std::uintptr_t TryWriteVfunc(
		REL::Relocation<std::uintptr_t>& a_vtable,
		std::size_t a_slotIndex,
		std::uintptr_t a_replacement,
		std::string_view a_hookName);
}
