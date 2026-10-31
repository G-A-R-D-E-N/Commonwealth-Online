#pragma once

#include "F4TVoiceGate.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace F4T::Voice
{
	inline constexpr int kVolumeMin = 0;
	inline constexpr int kVolumeMax = 100;
	inline constexpr int kSensitivityMin = -80;
	inline constexpr int kSensitivityMax = 0;

	struct PlayerVoiceSettings
	{
		int volume{ 100 };
		bool muted{};

		bool operator==(const PlayerVoiceSettings&) const = default;
	};

	struct VoiceSettings
	{
		bool voiceChat{ true };
		bool pushToTalk{ true };
		std::string pushToTalkKey{ "B" };
		std::uint8_t pushToTalkVirtualKey{ 0x42 };
		std::string whisperKey;
		std::uint8_t whisperVirtualKey{};
		std::string shoutKey;
		std::uint8_t shoutVirtualKey{};
		int voiceChatVolume{ 85 };
		bool mutePlayersByDefault{};
		int openMicSensitivity{ static_cast<int>(DefaultOpenMicThresholdDbfs) };
		bool deafened{};
		bool micMuted{};
		std::map<std::string, PlayerVoiceSettings> players;

		bool operator==(const VoiceSettings&) const = default;
	};

	std::filesystem::path VoiceSettingsPath(const std::filesystem::path& a_configDirectory);
	std::filesystem::path DefaultVoiceSettingsPath();
	VoiceSettings LoadVoiceSettings(const std::filesystem::path& a_path);
	bool SaveVoiceSettings(const std::filesystem::path& a_path, const VoiceSettings& a_settings);
}
