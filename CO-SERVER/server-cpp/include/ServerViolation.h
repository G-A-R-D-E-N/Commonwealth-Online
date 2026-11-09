#pragma once

#include <cstdint>
#include <string>

namespace CommonwealthOnline::Server
{
	enum class Violation : std::uint8_t
	{
		RateLimited,
		MalformedPayload,
		IdentityMismatch,
		TeleportDistance,
		InboundBudget,
		PvpDamage,
		DifficultyMismatch,
		DifficultyUnreported
	};

	struct PendingViolation
	{
		std::uint32_t connectionId{};
		Violation violation{};
		std::string detail;
	};
}
