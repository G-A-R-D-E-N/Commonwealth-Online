#pragma once

#include "F4TVoiceSession.h"
#include "F4TVoiceSettings.h"
#include "F4TVoiceStatus.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace F4T::Voice
{
	enum class KeyTarget : std::uint8_t
	{
		pushToTalk,
		whisper,
		shout
	};

	struct SetSettingAction
	{
		std::string key;
		nlohmann::json value;
	};

	struct BeginKeyCaptureAction
	{
		KeyTarget target{};
	};

	struct MutePlayerAction
	{
		std::uint32_t playerId{};
		bool muted{};
	};

	struct SetPlayerVolumeAction
	{
		std::uint32_t playerId{};
		int volume{};
	};

	using VoiceAction = std::variant<SetSettingAction, BeginKeyCaptureAction, MutePlayerAction, SetPlayerVolumeAction>;

	[[nodiscard]] std::string_view KeyTargetKey(KeyTarget a_target) noexcept;
	[[nodiscard]] std::optional<KeyTarget> ParseKeyTarget(std::string_view a_text) noexcept;
	[[nodiscard]] std::optional<VoiceAction> ParseVoiceAction(std::string_view a_json);
	[[nodiscard]] bool WithinNestingLimit(const nlohmann::json& a_json);
	[[nodiscard]] bool ApplySetting(VoiceSettings& a_settings, std::string_view a_key, const nlohmann::json& a_value);
	void ApplyKeyCapture(VoiceSettings& a_settings, KeyTarget a_target, std::string a_name, std::uint8_t a_vk);

	[[nodiscard]] std::string StatusEvent(bool a_active, VoiceStatus a_status, bool a_pending, bool a_micHot);
	[[nodiscard]] std::string SettingsEvent(const VoiceSettings& a_settings);
	[[nodiscard]] std::string PlayersEvent(std::span<const VoicePlayerRow> a_players);
	[[nodiscard]] std::string SpeakersEvent(std::span<const std::uint32_t> a_playerIds);
	[[nodiscard]] std::string KeyCaptureEvent(KeyTarget a_target, bool a_captured, std::string_view a_name, std::uint8_t a_vk);
	[[nodiscard]] std::string LevelEvent(double a_dbfs);
}
