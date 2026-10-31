#pragma once

#include <cstdint>
#include <string>

namespace F4T::Networking
{
	enum class ConnectFailureKind : std::uint32_t
	{
		AdmissionRefused = 1,
		SessionRejected = 2,
		LocalHandshakeFailure = 3
	};

	struct ConnectFailure
	{
		ConnectFailureKind kind{ ConnectFailureKind::AdmissionRefused };
		std::int32_t transportReason{};
		std::string code;
		std::string detail;
		std::string matchingModpackFingerprint;
		std::string modpackUrl;
	};

	inline int ConnectFailurePriority(ConnectFailureKind a_kind) noexcept
	{
		switch (a_kind) {
		case ConnectFailureKind::LocalHandshakeFailure:
			return 3;
		case ConnectFailureKind::SessionRejected:
			return 2;
		case ConnectFailureKind::AdmissionRefused:
		default:
			return 1;
		}
	}
}
