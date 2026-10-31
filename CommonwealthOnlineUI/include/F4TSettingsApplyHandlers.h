#pragma once

#include "F4TSettingsTypes.h"

#include <nlohmann/json.hpp>

namespace F4T::Settings
{
	[[nodiscard]] bool ApplyHUDSettings(const SettingDefinition& a_definition);
	[[nodiscard]] bool ApplyAudioSetting(const SettingDefinition& a_definition, float a_previousValue, float a_value);
	[[nodiscard]] bool ApplyDifficulty(const SettingDefinition& a_definition, std::int32_t a_oldDifficulty, std::int32_t a_newDifficulty);
	[[nodiscard]] bool ApplyInputSettings(const SettingDefinition& a_definition, const nlohmann::json& a_newValue);
	[[nodiscard]] bool ApplyPipBoyColour();
	[[nodiscard]] bool ApplyAutosaveSettings();
	[[nodiscard]] bool ApplyDisplaySetting(const SettingDefinition& a_definition);

	[[nodiscard]] bool ApplySettingChange(
		const SettingDefinition& a_definition,
		const nlohmann::json& a_oldValue,
		const nlohmann::json& a_newValue);
}
