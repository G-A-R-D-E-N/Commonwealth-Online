#include "F4TNetStatsPayload.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace F4T::NetStatsPayload
{
	namespace
	{
		constexpr float kBytesPerKilobyte = 1024.0F;
		constexpr std::int32_t kUnknownPingMs = -1;

		float Finite(float a_value) noexcept
		{
			return std::isfinite(a_value) ? a_value : 0.0F;
		}

		std::int32_t Percent(float a_quality) noexcept
		{
			return static_cast<std::int32_t>(std::lround(std::clamp(Finite(a_quality), 0.0F, 1.0F) * 100.0F));
		}
	}

	std::int32_t EffectivePingMs(std::int32_t a_rawPingMs, bool a_steamTunneled) noexcept
	{
		return a_steamTunneled || a_rawPingMs < 0 ? kUnknownPingMs : a_rawPingMs;
	}

	std::string BuildJson(const Sample& a_sample, bool a_steamTunneled)
	{
		return std::format(
			R"({{"pingMs":{},"qualityLocal":{},"qualityRemote":{},"inKBps":{:.1f},"outKBps":{:.1f},"inPps":{:.1f},"outPps":{:.1f},"pendingBytes":{}}})",
			EffectivePingMs(a_sample.pingMs, a_steamTunneled),
			Percent(a_sample.qualityLocal),
			Percent(a_sample.qualityRemote),
			Finite(a_sample.inBytesPerSec) / kBytesPerKilobyte,
			Finite(a_sample.outBytesPerSec) / kBytesPerKilobyte,
			Finite(a_sample.inPacketsPerSec),
			Finite(a_sample.outPacketsPerSec),
			std::max(0, a_sample.pendingBytes));
	}
}
