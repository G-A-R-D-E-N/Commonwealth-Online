#include "F4TProbeVersionGate.h"

namespace F4T::ProbeVersionGate
{
	bool IsSupported(const RuntimeVersion& a_version) noexcept
	{
		if (a_version.major != 1 || a_version.build != 0) {
			return false;
		}
		if (a_version.minor == 10) {
			return a_version.patch == 163 || a_version.patch == 984;
		}
		return a_version.minor == 11 && a_version.patch == 240;
	}

	std::optional<Runtime> RuntimeOf(const RuntimeVersion& a_version) noexcept
	{
		if (!IsSupported(a_version)) {
			return std::nullopt;
		}
		if (a_version.minor == 10) {
			return a_version.patch == 163 ? Runtime::Og : Runtime::Ng;
		}
		return Runtime::Ae;
	}
}
