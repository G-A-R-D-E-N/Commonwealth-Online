#include "F4TPlayerKey.h"

#include "F4TContentManifest.h"

namespace F4T::PlayerKey
{
	std::string Derive(std::string_view a_serverSecret, std::string_view a_steamId, std::string_view a_profileId)
	{
		// Constraint: an empty secret must yield no key, or the key would be computable from the public identity.
		if (a_serverSecret.empty()) {
			return {};
		}
		if (!a_steamId.empty()) {
			return ContentManifest::Sha256Hex("player-key-steam\n" + std::string{ a_serverSecret } + "\n" + std::string{ a_steamId });
		}
		if (a_profileId.empty()) {
			return {};
		}
		return ContentManifest::Sha256Hex("player-key-profile\n" + std::string{ a_serverSecret } + "\n" + std::string{ a_profileId });
	}
}
