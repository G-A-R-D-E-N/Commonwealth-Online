#include "F4TVoiceSettings.h"

#include "F4TVoiceKeyCapture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace
{
	std::mutex g_settingsMutex;
	constexpr int kVirtualKeyMin = 1;
	constexpr int kVirtualKeyMax = 254;

	int ReadInt(const nlohmann::json& a_object, const char* a_key, int a_fallback, int a_min, int a_max)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end() || !found->is_number()) {
			return a_fallback;
		}
		const auto clamped = std::clamp(found->get<double>(), static_cast<double>(a_min), static_cast<double>(a_max));
		return static_cast<int>(std::lround(clamped));
	}

	bool ReadBool(const nlohmann::json& a_object, const char* a_key, bool a_fallback)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end() || !found->is_boolean()) {
			return a_fallback;
		}
		return found->get<bool>();
	}

	void ReadKeyBinding(
		const nlohmann::json& a_object,
		const char* a_nameKey,
		const char* a_codeKey,
		std::string& a_name,
		std::uint8_t& a_code)
	{
		const auto keyName = a_object.find(a_nameKey);
		const auto keyCode = a_object.find(a_codeKey);
		if (keyName != a_object.end() && keyName->is_string() && keyCode != a_object.end() && keyCode->is_number_integer()) {
			const auto code = keyCode->get<std::int64_t>();
			if (code >= kVirtualKeyMin && code <= kVirtualKeyMax && !F4T::Voice::VirtualKeyMenuReserved(static_cast<std::uint8_t>(code))) {
				a_name = keyName->get<std::string>();
				a_code = static_cast<std::uint8_t>(code);
			}
		}
	}

	bool CommitFileToDisk(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		int fileDescriptor = -1;
		if (_wsopen_s(&fileDescriptor, a_path.c_str(), _O_WRONLY | _O_BINARY, _SH_DENYNO, 0) != 0) {
			return false;
		}
		const auto committed = _commit(fileDescriptor) == 0;
		_close(fileDescriptor);
		return committed;
#else
		const auto fileDescriptor = open(a_path.c_str(), O_RDONLY);
		if (fileDescriptor < 0) {
			return false;
		}
		const auto committed = fsync(fileDescriptor) == 0;
		close(fileDescriptor);
		return committed;
#endif
	}
}

namespace F4T::Voice
{
	std::filesystem::path VoiceSettingsPath(const std::filesystem::path& a_configDirectory)
	{
		if (a_configDirectory.empty()) {
			return {};
		}
		return a_configDirectory / "voice-settings.json";
	}

	VoiceSettings LoadVoiceSettings(const std::filesystem::path& a_path)
	{
		std::scoped_lock lock(g_settingsMutex);
		VoiceSettings settings;
		std::ifstream input(a_path);

		const auto json = nlohmann::json::parse(input, nullptr, false);
		if (!json.is_object()) {
			return settings;
		}

		settings.voiceChat = ReadBool(json, "voiceChat", settings.voiceChat);
		settings.pushToTalk = ReadBool(json, "pushToTalk", settings.pushToTalk);
		ReadKeyBinding(json, "pushToTalkKey", "pushToTalkVirtualKey", settings.pushToTalkKey, settings.pushToTalkVirtualKey);
		ReadKeyBinding(json, "whisperKey", "whisperVirtualKey", settings.whisperKey, settings.whisperVirtualKey);
		ReadKeyBinding(json, "shoutKey", "shoutVirtualKey", settings.shoutKey, settings.shoutVirtualKey);
		settings.voiceChatVolume = ReadInt(json, "voiceChatVolume", settings.voiceChatVolume, kVolumeMin, kVolumeMax);
		settings.mutePlayersByDefault = ReadBool(json, "mutePlayersByDefault", settings.mutePlayersByDefault);
		settings.openMicSensitivity =
			ReadInt(json, "openMicSensitivity", settings.openMicSensitivity, kSensitivityMin, kSensitivityMax);
		settings.deafened = ReadBool(json, "deafened", settings.deafened);
		settings.micMuted = ReadBool(json, "micMuted", settings.micMuted);

		const auto players = json.find("players");
		if (players != json.end() && players->is_object()) {
			for (const auto& [profileId, entry] : players->items()) {
				if (!entry.is_object()) {
					continue;
				}
				PlayerVoiceSettings player;
				player.volume = ReadInt(entry, "volume", player.volume, kVolumeMin, kVolumeMax);
				player.muted = ReadBool(entry, "muted", player.muted);
				settings.players[profileId] = player;
			}
		}
		return settings;
	}

	bool SaveVoiceSettings(const std::filesystem::path& a_path, const VoiceSettings& a_settings)
	{
		std::scoped_lock lock(g_settingsMutex);
		if (!a_path.has_parent_path()) {
			return false;
		}
		nlohmann::json players = nlohmann::json::object();
		for (const auto& [profileId, player] : a_settings.players) {
			players[profileId] = nlohmann::json{ { "volume", player.volume }, { "muted", player.muted } };
		}

		const nlohmann::json json{
			{ "voiceChat", a_settings.voiceChat },
			{ "pushToTalk", a_settings.pushToTalk },
			{ "pushToTalkKey", a_settings.pushToTalkKey },
			{ "pushToTalkVirtualKey", a_settings.pushToTalkVirtualKey },
			{ "whisperKey", a_settings.whisperKey },
			{ "whisperVirtualKey", a_settings.whisperVirtualKey },
			{ "shoutKey", a_settings.shoutKey },
			{ "shoutVirtualKey", a_settings.shoutVirtualKey },
			{ "voiceChatVolume", a_settings.voiceChatVolume },
			{ "mutePlayersByDefault", a_settings.mutePlayersByDefault },
			{ "openMicSensitivity", a_settings.openMicSensitivity },
			{ "deafened", a_settings.deafened },
			{ "micMuted", a_settings.micMuted },
			{ "players", std::move(players) }
		};

		std::string text;
		try {
			text = json.dump(2);
		} catch (const nlohmann::json::exception&) {
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		if (ec) {
			return false;
		}

		auto tempPath = a_path;
		tempPath += ".tmp";
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				return false;
			}
			output << text << '\n';
			output.close();
			if (output.fail()) {
				std::filesystem::remove(tempPath, ec);
				return false;
			}
		}

		if (!CommitFileToDisk(tempPath)) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}

		std::filesystem::rename(tempPath, a_path, ec);
		if (ec) {
			std::error_code cleanupError;
			std::filesystem::remove(tempPath, cleanupError);
			return false;
		}
		return true;
	}
}
