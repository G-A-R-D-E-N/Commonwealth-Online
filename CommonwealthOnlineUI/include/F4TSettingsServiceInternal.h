#pragma once

#include "F4TSettingsTypes.h"

#include <RE/Fallout.h>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace F4T::Settings::detail
{
	inline std::string GetLogPrefix()
	{
		return "[Settings]";
	}

	inline SettingResult Fail(std::string a_code, std::string a_message)
	{
		SettingResult result;
		result.success = false;
		result.error = SettingError{ std::move(a_code), std::move(a_message) };
		return result;
	}

	inline SettingResult Ok(nlohmann::json a_data = nullptr)
	{
		SettingResult result;
		result.success = true;
		result.data = std::move(a_data);
		return result;
	}

	inline RE::Setting* LookupKey(std::string_view a_engineKey)
	{
		// constraint: INI collections only; GameSettingCollection::GetSetting asserts on non-null-terminated views.
		const auto findInList = [](RE::INISettingCollection* collection, const std::string& key) -> RE::Setting* {
			if (!collection || key.empty()) {
				return nullptr;
			}
			return collection->GetSetting(key);
		};

		const std::string fullKey{ a_engineKey };
		if (auto* setting = findInList(RE::INIPrefSettingCollection::GetSingleton(), fullKey)) {
			return setting;
		}
		if (auto* setting = findInList(RE::INISettingCollection::GetSingleton(), fullKey)) {
			return setting;
		}

		const auto colon = fullKey.find(':');
		if (colon != std::string::npos) {
			const std::string bareKey = fullKey.substr(0, colon);
			if (auto* setting = findInList(RE::INIPrefSettingCollection::GetSingleton(), bareKey)) {
				return setting;
			}
			if (auto* setting = findInList(RE::INISettingCollection::GetSingleton(), bareKey)) {
				return setting;
			}
		}
		return nullptr;
	}
}
