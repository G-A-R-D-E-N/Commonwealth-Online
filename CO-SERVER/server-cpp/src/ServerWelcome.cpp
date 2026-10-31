#include "ServerWelcome.h"

#include "F4TShotReplay.h"
#include "F4TWorldPolicy.h"

#include <string>

namespace CommonwealthOnline::Server
{
	nlohmann::json BaseWelcomeCapabilities(bool a_componentSnapshots)
	{
		auto capabilities = nlohmann::json::array({
			"interest-v1",
			"hello-v2",
			"bounded-framing",
			"rate-limit-v1",
			"movement-correction-v1",
			"npc-authority-epoch-v1",
			"player-state-v1",
			"gns-message-transport-v1",
			"gns-snapshot-sequence-v1",
			"session-lifecycle-v1",
			"voice-v1",
			std::string{ F4T::WorldPolicy::kCapabilityName },
			std::string{ F4T::ShotReplay::kCapabilityName }
		});
		if (a_componentSnapshots) {
			capabilities.push_back("component-snapshot-v1");
			capabilities.push_back("component-snapshot-channels-v1");
		}
		return capabilities;
	}
}
