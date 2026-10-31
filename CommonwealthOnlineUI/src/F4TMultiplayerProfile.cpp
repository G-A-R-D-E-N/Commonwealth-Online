#ifndef COUI_PROFILE_TESTING
#include "pch.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#endif
#ifdef _WIN32
#include <Windows.h>
#endif

#include "F4TMultiplayerProfile.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

namespace F4T::MultiplayerProfile
{
	namespace
	{
		std::mutex g_mutex;
		bool g_loaded{ false };
		std::string g_name;
#ifdef COUI_PROFILE_TESTING
		std::filesystem::path g_testPath;
#endif

		std::filesystem::path StorePath()
		{
#ifdef COUI_PROFILE_TESTING
			return g_testPath;
#else
			wchar_t* buffer{ nullptr };
			const auto result = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &buffer);
			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> documents(buffer, REX::W32::CoTaskMemFree);
			if (result != 0 || !documents) return {};
			return std::filesystem::path(documents.get()) / "My Games" /
				std::string{ F4SE::GetSaveFolderName() } / "CommonwealthOnline" / "multiplayer-profile.json";
#endif
		}

		bool ValidName(std::string_view a_name)
		{
			if (a_name.size() > 128) return false;
			for (const unsigned char ch : a_name) {
				if (ch < 0x20 || ch == 0x7f) return false;
			}
			return a_name.empty() || (a_name.front() != ' ' && a_name.back() != ' ');
		}

		void Load()
		{
			if (g_loaded) return;
			g_loaded = true;
			try {
				const auto path = StorePath();
				if (path.empty() || !std::filesystem::exists(path) || std::filesystem::file_size(path) > 2048) return;
				std::ifstream input(path, std::ios::binary);
				const auto data = nlohmann::json::parse(input);
				const auto name = data.value("displayName", std::string{});
				if (data.value("version", 0) == 1 && ValidName(name)) g_name = name;
			} catch (const std::exception&) {}
		}
	}

	std::string GetDisplayName()
	{
		const std::scoped_lock lock(g_mutex);
		Load();
		return g_name;
	}

	bool CopyDisplayName(char* a_buffer, std::size_t a_capacity)
	{
		if (!a_buffer || a_capacity == 0) return false;
		a_buffer[0] = '\0';
		const auto name = GetDisplayName();
		if (name.empty() || name.size() >= a_capacity) return false;
		std::memcpy(a_buffer, name.data(), name.size());
		a_buffer[name.size()] = '\0';
		return true;
	}

	bool SaveDisplayName(std::string_view a_name, std::string& a_error)
	{
		a_error.clear();
		if (!ValidName(a_name)) {
			a_error = "Use at most 128 UTF-8 bytes, without control characters or surrounding spaces.";
			return false;
		}
		std::string text;
		try {
			text = nlohmann::json{ { "version", 1 }, { "displayName", a_name } }.dump();
		} catch (const nlohmann::json::type_error&) {
			a_error = "Display name must be valid UTF-8.";
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		std::filesystem::path temporary;
		try {
			const auto path = StorePath();
			if (path.empty()) throw std::runtime_error("Documents folder is unavailable.");
			std::filesystem::create_directories(path.parent_path());
			temporary = std::filesystem::path(path.wstring() + L".tmp");
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				output << text;
				output.flush();
				if (!output) throw std::runtime_error("Could not write multiplayer profile.");
			}
#ifdef _WIN32
			if (!::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				throw std::runtime_error("Could not replace multiplayer profile.");
			}
#else
			std::filesystem::rename(temporary, path);
#endif
			g_name = a_name;
			g_loaded = true;
			return true;
		} catch (const std::exception& ex) {
			a_error = ex.what();
			if (!temporary.empty()) {
				std::error_code ignored;
				std::filesystem::remove(temporary, ignored);
			}
			return false;
		}
	}

#ifdef COUI_PROFILE_TESTING
	void SetStorePathForTesting(const std::string& a_path)
	{
		const std::scoped_lock lock(g_mutex);
		g_testPath = a_path;
		g_loaded = false;
		g_name.clear();
	}
#endif
}
