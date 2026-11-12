#pragma once

#include "F4TMutationRing.h"

#include <cstdint>

// source:CO-BETA/plugin/lib/commonlibf4/include/RE/B/BSCoreTypes.h
namespace F4T::CrashForensics
{
	void Install();

	inline void Record(MutationRing::MutationSite a_site, std::uint32_t a_formId, std::uint64_t a_address, std::uint32_t a_aux)
	{
		MutationRing::Record(a_site, a_formId, a_address, a_aux);
	}
}
