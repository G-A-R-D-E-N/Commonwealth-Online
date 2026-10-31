#pragma once

#include "F4TSettingsTypes.h"

namespace F4T::Settings
{
	[[nodiscard]] bool PersistSetting(const SettingDefinition& a_definition, RE::Setting& a_setting);
	[[nodiscard]] RE::Setting* ResolveCollectionSetting(const SettingDefinition& a_definition);
}
