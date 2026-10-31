#pragma once

#include "F4TSettingsTypes.h"

#include <span>
#include <string_view>

namespace F4T::Settings
{
	class SettingsRegistry
	{
	public:
		static void Initialize();
		static bool IsInitialized();

		[[nodiscard]] static const SettingDefinition* Find(std::string_view a_id);
		[[nodiscard]] static std::span<const SettingDefinition> GetAll();
		[[nodiscard]] static std::vector<const SettingDefinition*> GetByCategory(std::string_view a_category);
		[[nodiscard]] static std::span<const AudioChannelSetting> GetAudioChannels();
	};
}
