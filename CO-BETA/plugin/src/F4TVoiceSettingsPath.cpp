#include "F4TVoiceSettings.h"

#include "F4TServerProfileStore.h"

namespace F4T::Voice
{
	std::filesystem::path DefaultVoiceSettingsPath()
	{
		return VoiceSettingsPath(F4T::ServerProfileStore::GetStoreDirectory());
	}
}
