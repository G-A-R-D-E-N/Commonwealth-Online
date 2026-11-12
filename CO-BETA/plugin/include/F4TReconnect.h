#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::Reconnect
{
	struct Target
	{
		std::string host;
		std::uint16_t port{};
		std::string joinId;
		std::optional<std::string> serverPassword;
		std::optional<std::string> modpackFingerprint;
	};

	void Remember(Target a_target);
	void Forget();
	bool TryArm();
	bool IsActive();
	void Cancel();
	void MarkTransportLost();
	bool OnWorkerThread();
}
