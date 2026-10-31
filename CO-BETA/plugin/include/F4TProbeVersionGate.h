#pragma once

#include <cstdint>
#include <optional>

namespace F4T::ProbeVersionGate
{
	enum class Runtime
	{
		Og,
		Ng,
		Ae
	};

	struct RuntimeVersion
	{
		std::uint16_t major{ 0 };
		std::uint16_t minor{ 0 };
		std::uint16_t patch{ 0 };
		std::uint16_t build{ 0 };
	};

	[[nodiscard]] bool IsSupported(const RuntimeVersion& a_version) noexcept;
	[[nodiscard]] std::optional<Runtime> RuntimeOf(const RuntimeVersion& a_version) noexcept;
}
