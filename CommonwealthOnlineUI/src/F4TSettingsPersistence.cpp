#include "pch.h"

#include "F4TSettingsPersistence.h"

namespace F4T::Settings
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return "[Settings]";
		}
	}

	RE::Setting* ResolveCollectionSetting(const SettingDefinition& a_definition)
	{
		const std::string engineKey{ a_definition.engineKey };

		switch (a_definition.source) {
		case SettingSource::INIPref: {
			if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				if (auto* setting = prefs->GetSetting(engineKey)) {
					return setting;
				}
			}
			if (auto* ini = RE::INISettingCollection::GetSingleton()) {
				return ini->GetSetting(engineKey);
			}
			return nullptr;
		}
		case SettingSource::INI: {
			if (auto* ini = RE::INISettingCollection::GetSingleton()) {
				return ini->GetSetting(engineKey);
			}
			return nullptr;
		}
		case SettingSource::GameSetting: {
			return nullptr;
		}
		case SettingSource::Special:
			return nullptr;
		}
		return nullptr;
	}

	bool PersistSetting(const SettingDefinition& a_definition, RE::Setting& a_setting)
	{
		if (!a_definition.persist) {
			return true;
		}

		bool wrote = false;
		switch (a_definition.source) {
		case SettingSource::INIPref: {
			if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
				wrote = prefs->WriteSetting(a_setting);
			}
			break;
		}
		case SettingSource::INI: {
			if (auto* ini = RE::INISettingCollection::GetSingleton()) {
				wrote = ini->WriteSetting(a_setting);
			}
			break;
		}
		case SettingSource::GameSetting: {
			if (auto* games = RE::GameSettingCollection::GetSingleton()) {
				wrote = games->WriteSetting(a_setting);
			}
			break;
		}
		case SettingSource::Special:
			REX::WARN("{} PersistSetting called for special setting id={} — use composite persist path",
				GetLogPrefix(), a_definition.id);
			return false;
		}

		if (!wrote) {
			REX::WARN("{} Persist failed id={} engineKey={}",
				GetLogPrefix(), a_definition.id, a_definition.engineKey);
		}
		return wrote;
	}
}
