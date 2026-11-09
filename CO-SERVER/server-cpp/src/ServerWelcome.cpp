#include "ServerWelcome.h"

#include "F4TFurnitureSync.h"
#include "F4TShotReplay.h"
#include "F4TSleepBedIdentity.h"
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
			"social-names-v1",
			"friends-v1",
			"privacy-v1",
			"gns-message-transport-v1",
			"gns-snapshot-sequence-v1",
			"session-lifecycle-v1",
			"voice-v1",
			"state-bag-v1",
			std::string{ F4T::WorldPolicy::kCapabilityName },
			std::string{ F4T::WorldPolicy::kCapabilityNameV2 },
			std::string{ F4T::ShotReplay::kCapabilityName },
			std::string{ F4T::ShotReplay::kExplosionCapabilityName },
			std::string{ F4T::FurnitureSync::kCapabilityName },
			std::string{ F4T::SleepBedIdentity::kCapabilityName }
		});
		if (a_componentSnapshots) {
			capabilities.push_back("component-snapshot-v1");
			capabilities.push_back("component-snapshot-channels-v1");
		}
		return capabilities;
	}
}
