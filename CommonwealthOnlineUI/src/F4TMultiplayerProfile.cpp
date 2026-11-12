#ifndef COUI_PROFILE_TESTING
#include "pch.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#endif
#include "F4TViewJson.h"
#ifdef _WIN32
#include <Windows.h>
#endif

#include "F4TMultiplayerProfile.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

namespace F4T::MultiplayerProfile
{
	namespace
	{
		constexpr std::array<std::string_view, 3> kOnlineStatus{ "Everyone", "Friends", "Nobody" };
		constexpr std::array<std::string_view, 3> kAllowFriendJoin{ "Always", "Ask First", "Never" };
		constexpr std::array<std::string_view, 3> kMarkerSize{ "Small", "Medium", "Large" };
		constexpr std::array<std::string_view, 3> kFriendRequests{ "Everyone", "Friends of Friends", "Nobody" };
		constexpr std::array<std::string_view, 5> kRegionPreference{ "Auto", "North America", "Europe", "Asia", "Oceania" };
		constexpr std::array<std::string_view, 3> kInterpolationQuality{ "Low", "Medium", "High" };

		std::mutex g_mutex;
		bool g_loaded{ false };
		std::string g_name;
		Preferences g_prefs;
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

		template <std::size_t N>
		bool ReadChoice(const std::array<std::string_view, N>& a_options, const nlohmann::json& a_value, std::uint8_t& a_out)
		{
			if (!a_value.is_string()) return false;
			const auto text = a_value.get<std::string>();
			for (std::size_t i = 0; i < N; ++i) {
				if (a_options[i] == std::string_view{ text }) {
					a_out = static_cast<std::uint8_t>(i);
					return true;
				}
			}
			return false;
		}

		bool ReadFlag(const nlohmann::json& a_value, bool& a_out)
		{
			if (!a_value.is_boolean()) return false;
			a_out = a_value.get<bool>();
			return true;
		}

		bool ApplyPreference(Preferences& a_prefs, std::string_view a_key, const nlohmann::json& a_value)
		{
			if (a_key == "profile.onlineStatus") return ReadChoice(kOnlineStatus, a_value, a_prefs.onlineStatus);
			if (a_key == "profile.allowFriendJoin") return ReadChoice(kAllowFriendJoin, a_value, a_prefs.allowFriendJoin);
			if (a_key == "profile.showLocationToFriends") return ReadFlag(a_value, a_prefs.showLocationToFriends);
			if (a_key == "map.showPlayerMarkers") return ReadFlag(a_value, a_prefs.showPlayerMarkers);
			if (a_key == "map.showFriendMarkers") return ReadFlag(a_value, a_prefs.showFriendMarkers);
			if (a_key == "map.showNameplates") return ReadFlag(a_value, a_prefs.showNameplates);
			if (a_key == "map.mapMarkerSize") return ReadChoice(kMarkerSize, a_value, a_prefs.mapMarkerSize);
			if (a_key == "map.friendRequests") return ReadChoice(kFriendRequests, a_value, a_prefs.friendRequests);
			if (a_key == "network.regionPreference") return ReadChoice(kRegionPreference, a_value, a_prefs.regionPreference);
			if (a_key == "network.networkSmoothing") return ReadFlag(a_value, a_prefs.networkSmoothing);
			if (a_key == "network.interpolationQuality") return ReadChoice(kInterpolationQuality, a_value, a_prefs.interpolationQuality);
			if (a_key == "network.showPing") return ReadFlag(a_value, a_prefs.showPing);
			if (a_key == "network.networkStatsOverlay") return ReadFlag(a_value, a_prefs.networkStatsOverlay);
			return false;
		}

		nlohmann::json PreferencesToJson(const Preferences& a_prefs)
		{
			return nlohmann::json{
				{ "profile.onlineStatus", std::string{ kOnlineStatus[a_prefs.onlineStatus] } },
				{ "profile.allowFriendJoin", std::string{ kAllowFriendJoin[a_prefs.allowFriendJoin] } },
				{ "profile.showLocationToFriends", a_prefs.showLocationToFriends },
				{ "map.showPlayerMarkers", a_prefs.showPlayerMarkers },
				{ "map.showFriendMarkers", a_prefs.showFriendMarkers },
				{ "map.showNameplates", a_prefs.showNameplates },
				{ "map.mapMarkerSize", std::string{ kMarkerSize[a_prefs.mapMarkerSize] } },
				{ "map.friendRequests", std::string{ kFriendRequests[a_prefs.friendRequests] } },
				{ "network.regionPreference", std::string{ kRegionPreference[a_prefs.regionPreference] } },
				{ "network.networkSmoothing", a_prefs.networkSmoothing },
				{ "network.interpolationQuality", std::string{ kInterpolationQuality[a_prefs.interpolationQuality] } },
				{ "network.showPing", a_prefs.showPing },
				{ "network.networkStatsOverlay", a_prefs.networkStatsOverlay },
			};
		}

		void Load()
		{
			if (g_loaded) return;
			g_loaded = true;
			try {
				const auto path = StorePath();
				if (path.empty() || !std::filesystem::exists(path) || std::filesystem::file_size(path) > 4096) return;
				std::ifstream input(path, std::ios::binary);
				const auto data = nlohmann::json::parse(input);
				if (!data.is_object() || data.value("version", 0) != 1) return;
				const auto nameIt = data.find("displayName");
				if (nameIt != data.end() && nameIt->is_string()) {
					const auto name = nameIt->get<std::string>();
					if (ValidName(name)) g_name = name;
				}
				const auto prefsIt = data.find("prefs");
				if (prefsIt != data.end() && prefsIt->is_object()) {
					for (const auto& item : prefsIt->items()) {
						ApplyPreference(g_prefs, item.key(), item.value());
					}
				}
			} catch (const std::exception&) {}
		}

		bool Persist(std::string_view a_name, const Preferences& a_prefs, std::string& a_error)
		{
			std::string text;
			try {
				text = nlohmann::json{ { "version", 1 }, { "displayName", a_name }, { "prefs", PreferencesToJson(a_prefs) } }.dump();
			} catch (const nlohmann::json::type_error&) {
				a_error = "Display name must be valid UTF-8.";
				return false;
			}
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
		const std::scoped_lock lock(g_mutex);
		Load();
		if (!Persist(a_name, g_prefs, a_error)) return false;
		g_name = a_name;
		return true;
	}

	Preferences GetPreferences()
	{
		const std::scoped_lock lock(g_mutex);
		Load();
		return g_prefs;
	}

	bool SetPreference(std::string_view a_key, std::string_view a_valueJson, std::string& a_error)
	{
		a_error.clear();
		nlohmann::json value;
		try {
			value = nlohmann::json::parse(std::string{ a_valueJson });
		} catch (const nlohmann::json::exception&) {
			a_error = "Value must be valid JSON.";
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		Load();
		auto prefs = g_prefs;
		if (!ApplyPreference(prefs, a_key, value)) {
			a_error = "Unknown setting or invalid value.";
			return false;
		}
		if (!Persist(g_name, prefs, a_error)) return false;
		g_prefs = prefs;
		return true;
	}

	std::string PreferencesJson()
	{
		const std::scoped_lock lock(g_mutex);
		Load();
		return F4T::ViewJson::Dump(PreferencesToJson(g_prefs));
	}

#ifdef COUI_PROFILE_TESTING
	void SetStorePathForTesting(const std::string& a_path)
	{
		const std::scoped_lock lock(g_mutex);
		g_testPath = a_path;
		g_loaded = false;
		g_name.clear();
		g_prefs = Preferences{};
	}
#endif
}
