// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "pch.h"

#include "F4TPreviousLog.h"

#include "F4TF4SECapabilities.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	bool ResolveLogFolder(std::string_view a_saveFolderName, std::filesystem::path& a_folder)
	{
		wchar_t* knownBuffer{ nullptr };
		const auto knownResult =
			REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(knownBuffer, REX::W32::CoTaskMemFree);
		if (!knownPath || knownResult != 0) {
			return false;
		}

		a_folder = knownPath.get();
		a_folder /= "My Games";
		a_folder /= a_saveFolderName;
		a_folder /= "F4SE";
		return true;
	}

	std::string PreInitPluginName()
	{
		if (const auto* data = F4SE::PluginVersionData::GetSingleton()) {
			return std::string{ data->GetPluginName() };
		}
		char path[MAX_PATH]{};
		const auto length = REX::W32::GetModuleFileNameA(REX::W32::GetCurrentModule(), path, MAX_PATH);
		return std::filesystem::path(path, path + length).stem().string();
	}
}

namespace F4T::PreviousLog
{
	void KeepPreviousLog(const F4SE::LoadInterface* a_f4se) noexcept
	{
		if (a_f4se == nullptr) {
			return;
		}
		const auto version = a_f4se->F4SEVersion();
		const std::string saveFolder = F4T::F4SECapabilities::SupportsSaveFolderName(version.major(), version.minor(), version.patch())
			? std::string{ a_f4se->GetSaveFolderName() }
			: std::string{ "Fallout4" };
		std::filesystem::path folder;
		if (!ResolveLogFolder(saveFolder, folder)) {
			return;
		}
		const auto name = PreInitPluginName();
		const auto current = folder / (name + ".log");
		std::error_code error;
		const auto size = std::filesystem::file_size(current, error);
		if (error || size == 0) {
			return;
		}
		std::filesystem::rename(current, folder / (name + "-previous.log"), error);
	}
}
