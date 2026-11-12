#include "pch.h"

#include "F4TSavePluginPreflight.h"

#include "F4TLoadOrderSync.h"
#include "F4TSaveFilePlugins.h"
#include "F4TServerProfileStore.h"

#include "RE/S/Setting.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::size_t kMaximumHeaderReadBytes = 4 * 1024 * 1024;

	std::optional<std::filesystem::path> ResolveSaveFilePath(std::string_view a_saveFile)
	{
		const auto saveFolder = F4T::ServerProfileStore::GetSaveFolderDirectory();
		if (saveFolder.empty() || a_saveFile.empty()) {
			return std::nullopt;
		}

		const auto* setting = RE::GetINISetting("sLocalSavePath:General");
		if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kString) {
			return std::nullopt;
		}
		const std::string_view localSavePath = setting->GetString();
		if (localSavePath.empty()) {
			return std::nullopt;
		}

		const auto path = saveFolder / std::filesystem::path{ std::string{ localSavePath } } /
		                  std::filesystem::path{ std::string{ a_saveFile } + ".fos" };
		std::error_code error;
		if (!std::filesystem::exists(path, error)) {
			return std::nullopt;
		}
		return path;
	}

	std::optional<std::vector<std::uint8_t>> ReadHeaderBytes(const std::filesystem::path& a_path)
	{
		std::ifstream input{ a_path, std::ios::binary };
		if (!input.is_open()) {
			return std::nullopt;
		}

		std::vector<std::uint8_t> bytes(kMaximumHeaderReadBytes);
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (input.bad()) {
			return std::nullopt;
		}
		bytes.resize(static_cast<std::size_t>(input.gcount()));
		if (bytes.empty()) {
			return std::nullopt;
		}
		return bytes;
	}

	F4T::SaveFilePlugins::PluginList LoadedPlugins()
	{
		F4T::SaveFilePlugins::PluginList loaded;
		const auto local = F4T::LoadOrderSync::GatherLocalPlugins();
		loaded.full.reserve(local.full.size());
		for (const auto& plugin : local.full) {
			loaded.full.push_back(plugin.name);
		}
		loaded.light.reserve(local.light.size());
		for (const auto& plugin : local.light) {
			loaded.light.push_back(plugin.name);
		}
		return loaded;
	}
}

namespace F4T::SavePluginPreflight
{
	bool AllowSaveLoad(std::string_view a_saveFile, std::string& a_error)
	{
		try {
			const auto saveFilePath = ResolveSaveFilePath(a_saveFile);
			if (!saveFilePath) {
				REX::WARN(
					"Save load preflight skipped for '{}': the save file could not be located.",
					a_saveFile);
				return true;
			}

			const auto headerBytes = ReadHeaderBytes(*saveFilePath);
			if (!headerBytes) {
				REX::WARN("Save load preflight skipped for '{}': the save file could not be read.", a_saveFile);
				return true;
			}

			const auto savedPlugins = SaveFilePlugins::ParseSavePlugins(*headerBytes);
			if (!savedPlugins) {
				REX::WARN("Save load preflight skipped for '{}': the save header did not parse.", a_saveFile);
				return true;
			}

			const auto loaded = LoadedPlugins();
			if (loaded.full.empty() && loaded.light.empty()) {
				REX::WARN("Save load preflight skipped for '{}': the loaded plugin list is unavailable.", a_saveFile);
				return true;
			}

			const auto missing = SaveFilePlugins::FindMissingPlugins(*savedPlugins, loaded);
			if (missing.empty()) {
				REX::INFO(
					"Save load preflight passed for '{}': {} full and {} light saved plugins are loaded.",
					a_saveFile,
					savedPlugins->full.size(),
					savedPlugins->light.size());
				return true;
			}

			a_error = SaveFilePlugins::BuildMissingPluginsMessage(missing);
			REX::WARN(
				"Save load preflight blocked '{}': {} saved plugin(s) are not loaded: {}",
				a_saveFile,
				missing.size(),
				a_error);
			return false;
		} catch (...) {
			REX::WARN("Save load preflight skipped for '{}' after an unexpected error.", a_saveFile);
			return true;
		}
	}
}
