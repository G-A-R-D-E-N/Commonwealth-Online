#include "F4TVoiceUiProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace F4T::Voice
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr int kMaxLevels = 8;

		std::string Dump(const Json& a_json)
		{
			return a_json.dump(-1, ' ', false, Json::error_handler_t::replace);
		}

		std::optional<int> IntegerIn(const Json& a_value, int a_min, int a_max)
		{
			if (!a_value.is_number_integer()) {
				return std::nullopt;
			}
			const auto value = a_value.get<double>();
			if (value < a_min || value > a_max) {
				return std::nullopt;
			}
			return static_cast<int>(value);
		}

		bool SetBool(const Json& a_value, bool& a_target)
		{
			if (!a_value.is_boolean()) {
				return false;
			}
			a_target = a_value.get<bool>();
			return true;
		}

		bool SetInt(const Json& a_value, int a_min, int a_max, int& a_target)
		{
			const auto value = IntegerIn(a_value, a_min, a_max);
			if (value) {
				a_target = *value;
			}
			return value.has_value();
		}

		std::optional<VoiceAction> ParseSetSetting(const Json& a_root)
		{
			const auto key = a_root.find("key");
			const auto value = a_root.find("value");
			if (key == a_root.end() || !key->is_string() || value == a_root.end()) {
				return std::nullopt;
			}
			return SetSettingAction{ key->get<std::string>(), *value };
		}

		std::optional<VoiceAction> ParseBeginKeyCapture(const Json& a_root)
		{
			const auto target = a_root.find("target");
			if (target == a_root.end() || !target->is_string()) {
				return std::nullopt;
			}
			const auto parsed = ParseKeyTarget(target->get_ref<const std::string&>());
			if (!parsed) {
				return std::nullopt;
			}
			return BeginKeyCaptureAction{ *parsed };
		}

		std::optional<VoiceAction> ParseMutePlayer(const Json& a_root)
		{
			const auto playerId = ParsePlayerId(a_root);
			const auto muted = a_root.find("muted");
			if (!playerId || muted == a_root.end() || !muted->is_boolean()) {
				return std::nullopt;
			}
			return MutePlayerAction{ *playerId, muted->get<bool>() };
		}

		std::optional<VoiceAction> ParseSetPlayerVolume(const Json& a_root)
		{
			const auto playerId = ParsePlayerId(a_root);
			const auto volume = a_root.find("volume");
			if (!playerId || volume == a_root.end()) {
				return std::nullopt;
			}
			const auto value = IntegerIn(*volume, kVolumeMin, kVolumeMax);
			if (!value) {
				return std::nullopt;
			}
			return SetPlayerVolumeAction{ *playerId, *value };
		}
		bool WithinLevels(const Json& a_json, int a_levels)
		{
			if (!a_json.is_structured()) {
				return true;
			}
			if (a_levels <= 0) {
				return false;
			}
			for (const auto& child : a_json) {
				if (!WithinLevels(child, a_levels - 1)) {
					return false;
				}
			}
			return true;
		}
	}

	std::string_view KeyTargetKey(KeyTarget a_target) noexcept
	{
		switch (a_target) {
		case KeyTarget::pushToTalk:
			return "voice.pushToTalkKey";
		case KeyTarget::whisper:
			return "voice.whisperKey";
		case KeyTarget::shout:
			return "voice.shoutKey";
		}
		return {};
	}

	std::optional<KeyTarget> ParseKeyTarget(std::string_view a_text) noexcept
	{
		for (const auto target : { KeyTarget::pushToTalk, KeyTarget::whisper, KeyTarget::shout }) {
			if (KeyTargetKey(target) == a_text) {
				return target;
			}
		}
		return std::nullopt;
	}

	std::optional<VoiceAction> ParseVoiceAction(std::string_view a_json)
	{
		bool tooDeep = false;
		const auto root = Json::parse(a_json, [&tooDeep](int a_depth, Json::parse_event_t a_event, Json&) {
			if (a_event == Json::parse_event_t::object_start || a_event == Json::parse_event_t::array_start) {
				tooDeep = tooDeep || a_depth >= kMaxLevels;
			}
			return !tooDeep;
		}, false);
		if (tooDeep || !root.is_object()) {
			return std::nullopt;
		}
		const auto op = root.find("op");
		if (op == root.end() || !op->is_string()) {
			return std::nullopt;
		}
		const auto& name = op->get_ref<const std::string&>();
		if (name == "setSetting") {
			return ParseSetSetting(root);
		}
		if (name == "beginKeyCapture") {
			return ParseBeginKeyCapture(root);
		}
		if (name == "mutePlayer") {
			return ParseMutePlayer(root);
		}
		if (name == "setPlayerVolume") {
			return ParseSetPlayerVolume(root);
		}
		return std::nullopt;
	}

	bool WithinNestingLimit(const Json& a_json)
	{
		return WithinLevels(a_json, kMaxLevels);
	}

	bool ApplySetting(VoiceSettings& a_settings, std::string_view a_key, const Json& a_value)
	{
		if (a_key == "voice.voiceChat") {
			return SetBool(a_value, a_settings.voiceChat);
		}
		if (a_key == "voice.pushToTalk") {
			return SetBool(a_value, a_settings.pushToTalk);
		}
		if (a_key == "voice.mutePlayersByDefault") {
			return SetBool(a_value, a_settings.mutePlayersByDefault);
		}
		if (a_key == "voice.deafened") {
			return SetBool(a_value, a_settings.deafened);
		}
		if (a_key == "voice.micMuted") {
			return SetBool(a_value, a_settings.micMuted);
		}
		if (a_key == "voice.voiceChatVolume") {
			return SetInt(a_value, kVolumeMin, kVolumeMax, a_settings.voiceChatVolume);
		}
		if (a_key == "voice.openMicSensitivity") {
			return SetInt(a_value, kSensitivityMin, kSensitivityMax, a_settings.openMicSensitivity);
		}
		return false;
	}

	void ApplyKeyCapture(VoiceSettings& a_settings, KeyTarget a_target, std::string a_name, std::uint8_t a_vk)
	{
		switch (a_target) {
		case KeyTarget::pushToTalk:
			a_settings.pushToTalkKey = std::move(a_name);
			a_settings.pushToTalkVirtualKey = a_vk;
			return;
		case KeyTarget::whisper:
			a_settings.whisperKey = std::move(a_name);
			a_settings.whisperVirtualKey = a_vk;
			return;
		case KeyTarget::shout:
			a_settings.shoutKey = std::move(a_name);
			a_settings.shoutVirtualKey = a_vk;
			return;
		}
	}

	std::string StatusEvent(bool a_active, VoiceStatus a_status, bool a_pending, bool a_micHot)
	{
		return Dump(Json{
			{ "kind", "status" },
			{ "active", a_active },
			{ "code", std::string{ VoiceStatusCode(a_status) } },
			{ "pending", a_pending },
			{ "micHot", a_micHot }
		});
	}

	std::string SettingsEvent(const VoiceSettings& a_settings)
	{
		const Json values{
			{ "voice.voiceChat", a_settings.voiceChat },
			{ "voice.pushToTalk", a_settings.pushToTalk },
			{ "voice.pushToTalkKey", a_settings.pushToTalkKey },
			{ "voice.whisperKey", a_settings.whisperKey },
			{ "voice.shoutKey", a_settings.shoutKey },
			{ "voice.openMicSensitivity", a_settings.openMicSensitivity },
			{ "voice.voiceChatVolume", a_settings.voiceChatVolume },
			{ "voice.mutePlayersByDefault", a_settings.mutePlayersByDefault },
			{ "voice.deafened", a_settings.deafened },
			{ "voice.micMuted", a_settings.micMuted }
		};
		return Dump(Json{ { "kind", "settings" }, { "values", values } });
	}

	std::string PlayersEvent(std::span<const VoicePlayerRow> a_players)
	{
		Json players = Json::array();
		for (const auto& row : a_players) {
			players.push_back(Json{
				{ "playerId", row.playerId },
				{ "name", row.name },
				{ "muted", row.muted },
				{ "volume", row.volume }
			});
		}
		return Dump(Json{ { "kind", "players" }, { "players", std::move(players) } });
	}

	std::string SpeakersEvent(std::span<const std::uint32_t> a_playerIds)
	{
		Json ids = Json::array();
		for (const auto playerId : a_playerIds) {
			ids.push_back(playerId);
		}
		return Dump(Json{ { "kind", "speakers" }, { "playerIds", std::move(ids) } });
	}

	std::string KeyCaptureEvent(KeyTarget a_target, bool a_captured, std::string_view a_name, std::uint8_t a_vk)
	{
		return Dump(Json{
			{ "kind", "keyCapture" },
			{ "target", std::string{ KeyTargetKey(a_target) } },
			{ "state", a_captured ? "captured" : "cancelled" },
			{ "name", a_captured ? std::string{ a_name } : std::string{} },
			{ "vk", a_captured ? a_vk : std::uint8_t{} }
		});
	}

	std::string LevelEvent(double a_dbfs)
	{
		return Dump(Json{ { "kind", "level" }, { "dbfs", a_dbfs } });
	}
}
