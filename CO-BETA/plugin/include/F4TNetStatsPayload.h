#pragma once

#include <cstdint>
#include <string>

namespace F4T::NetStatsPayload
{
	struct Sample
	{
		std::int32_t pingMs{};
		float qualityLocal{};
		float qualityRemote{};
		float inBytesPerSec{};
		float outBytesPerSec{};
		float inPacketsPerSec{};
		float outPacketsPerSec{};
		std::int32_t pendingBytes{};
	};

	[[nodiscard]] std::int32_t EffectivePingMs(std::int32_t a_rawPingMs, bool a_steamTunneled) noexcept;
	[[nodiscard]] std::string BuildJson(const Sample& a_sample, bool a_steamTunneled);
}
