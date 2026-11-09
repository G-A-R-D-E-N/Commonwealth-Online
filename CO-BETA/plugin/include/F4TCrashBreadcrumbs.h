#pragma once

#include "F4TCrashBreadcrumbRing.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::CrashBreadcrumbs
{
	void Record(Code a_code, std::uint32_t a_formId = 0, std::uint32_t a_aux = 0);
	std::size_t FormatDump(std::span<char> a_out);
}
