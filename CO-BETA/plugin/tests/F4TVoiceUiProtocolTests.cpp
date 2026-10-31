#include "F4TVoiceUiProtocol.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{
	using F4T::Voice::ApplyKeyCapture;
	using F4T::Voice::ApplySetting;
	using F4T::Voice::BeginKeyCaptureAction;
	using F4T::Voice::KeyCaptureEvent;
	using F4T::Voice::KeyTarget;
	using F4T::Voice::KeyTargetKey;
	using F4T::Voice::LevelEvent;
	using F4T::Voice::MutePlayerAction;
	using F4T::Voice::ParseKeyTarget;
	using F4T::Voice::ParseVoiceAction;
	using F4T::Voice::PlayerVoiceSettings;
	using F4T::Voice::PlayersEvent;
	using F4T::Voice::SetPlayerVolumeAction;
	using F4T::Voice::SetSettingAction;
	using F4T::Voice::SettingsEvent;
	using F4T::Voice::SpeakersEvent;
	using F4T::Voice::StatusEvent;
	using F4T::Voice::VoicePlayerRow;
	using F4T::Voice::VoiceSettings;
	using F4T::Voice::VoiceStatus;
	using F4T::Voice::WithinNestingLimit;
	using Json = nlohmann::json;

	void ParsesEveryValidOp()
	{
		const auto setting = ParseVoiceAction(R"({"op":"setSetting","key":"voice.voiceChatVolume","value":42})");
		assert(setting && std::holds_alternative<SetSettingAction>(*setting));
		const auto& set = std::get<SetSettingAction>(*setting);
		assert(set.key == "voice.voiceChatVolume");
		assert(set.value == 42);

		const auto flag = ParseVoiceAction(R"({"op":"setSetting","key":"voice.deafened","value":false})");
		assert(flag && std::get<SetSettingAction>(*flag).key == "voice.deafened");
		assert(std::get<SetSettingAction>(*flag).value == false);

		const auto shout = ParseVoiceAction(R"({"op":"beginKeyCapture","target":"voice.shoutKey"})");
		assert(shout && std::get<BeginKeyCaptureAction>(*shout).target == KeyTarget::shout);
		const auto whisper = ParseVoiceAction(R"({"op":"beginKeyCapture","target":"voice.whisperKey"})");
		assert(whisper && std::get<BeginKeyCaptureAction>(*whisper).target == KeyTarget::whisper);
		const auto push = ParseVoiceAction(R"({"op":"beginKeyCapture","target":"voice.pushToTalkKey"})");
		assert(push && std::get<BeginKeyCaptureAction>(*push).target == KeyTarget::pushToTalk);

		const auto mute = ParseVoiceAction(R"({"op":"mutePlayer","playerId":4294967295,"muted":true})");
		assert(mute && std::holds_alternative<MutePlayerAction>(*mute));
		assert(std::get<MutePlayerAction>(*mute).playerId == 0xFFFFFFFFu);
		assert(std::get<MutePlayerAction>(*mute).muted);

		const auto volume = ParseVoiceAction(R"({"op":"setPlayerVolume","playerId":7,"volume":0})");
		assert(volume && std::holds_alternative<SetPlayerVolumeAction>(*volume));
		assert(std::get<SetPlayerVolumeAction>(*volume).playerId == 7);
		assert(std::get<SetPlayerVolumeAction>(*volume).volume == 0);
		const auto top = ParseVoiceAction(R"({"op":"setPlayerVolume","playerId":7,"volume":100})");
		assert(top && std::get<SetPlayerVolumeAction>(*top).volume == 100);
	}

	void RejectsMalformedAndInvalidActions()
	{
		const char* const rejected[] = {
			"",
			"{",
			"[]",
			"42",
			"null",
			R"("setSetting")",
			"{}",
			R"({"op":5})",
			R"({"op":"explode"})",
			R"({"op":"setSetting","value":1})",
			R"({"op":"setSetting","key":5,"value":1})",
			R"({"op":"setSetting","key":"voice.voiceChat"})",
			R"({"op":"beginKeyCapture","target":"voice.voiceChat"})",
			R"({"op":"beginKeyCapture","target":5})",
			R"({"op":"beginKeyCapture"})",
			R"({"op":"mutePlayer","playerId":-1,"muted":true})",
			R"({"op":"mutePlayer","playerId":1.5,"muted":true})",
			R"({"op":"mutePlayer","playerId":"7","muted":true})",
			R"({"op":"mutePlayer","playerId":true,"muted":true})",
			R"({"op":"mutePlayer","playerId":4294967296,"muted":true})",
			R"({"op":"mutePlayer","muted":true})",
			R"({"op":"mutePlayer","playerId":7,"muted":"true"})",
			R"({"op":"mutePlayer","playerId":7,"muted":1})",
			R"({"op":"mutePlayer","playerId":7})",
			R"({"op":"setPlayerVolume","playerId":7,"volume":50.5})",
			R"({"op":"setPlayerVolume","playerId":7,"volume":"50"})",
			R"({"op":"setPlayerVolume","playerId":7,"volume":101})",
			R"({"op":"setPlayerVolume","playerId":7,"volume":-1})",
			R"({"op":"setPlayerVolume","playerId":7})",
		};
		for (const auto* text : rejected) {
			assert(!ParseVoiceAction(text).has_value());
		}
	}

	std::string NestedSetSetting(std::size_t a_levels)
	{
		return R"({"op":"setSetting","key":"voice.deafened","value":)" + std::string(a_levels, '[') + "1" + std::string(a_levels, ']') + "}";
	}

	void RejectsActionsNestedDeeperThanEightLevels()
	{
		assert(ParseVoiceAction(NestedSetSetting(7)).has_value());
		assert(!ParseVoiceAction(NestedSetSetting(8)).has_value());
		assert(!ParseVoiceAction(NestedSetSetting(9)).has_value());
		assert(!ParseVoiceAction(NestedSetSetting(1000)).has_value());
		assert(!ParseVoiceAction(NestedSetSetting(10000)).has_value());
	}

	void PacketNestedDeeperThanEightLevelsIsNotWithinLimit()
	{
		assert(WithinNestingLimit(Json::parse(NestedSetSetting(7))));
		assert(!WithinNestingLimit(Json::parse(NestedSetSetting(8))));
		assert(!WithinNestingLimit(Json::parse(NestedSetSetting(10000))));
	}

	void GarbageNeverThrows()
	{
		assert(!ParseVoiceAction("").has_value());
		assert(!ParseVoiceAction(std::string(2048, '{')).has_value());
		assert(!ParseVoiceAction(std::string(2048, '\xff')).has_value());
		std::string mixed;
		for (int index = 0; index < 2048; ++index) {
			mixed.push_back(static_cast<char>(index * 37));
		}
		assert(!ParseVoiceAction(mixed).has_value());
	}

	void KeyTargetNamesRoundTrip()
	{
		assert(KeyTargetKey(KeyTarget::pushToTalk) == "voice.pushToTalkKey");
		assert(KeyTargetKey(KeyTarget::whisper) == "voice.whisperKey");
		assert(KeyTargetKey(KeyTarget::shout) == "voice.shoutKey");
		for (const auto target : { KeyTarget::pushToTalk, KeyTarget::whisper, KeyTarget::shout }) {
			assert(ParseKeyTarget(KeyTargetKey(target)) == target);
		}
		assert(!ParseKeyTarget("voice.voiceChat").has_value());
		assert(!ParseKeyTarget("").has_value());
	}

	void AppliesAcceptedSettings()
	{
		VoiceSettings settings;
		assert(ApplySetting(settings, "voice.voiceChat", false) && !settings.voiceChat);
		assert(ApplySetting(settings, "voice.pushToTalk", false) && !settings.pushToTalk);
		assert(ApplySetting(settings, "voice.mutePlayersByDefault", true) && settings.mutePlayersByDefault);
		assert(ApplySetting(settings, "voice.deafened", true) && settings.deafened);
		assert(ApplySetting(settings, "voice.micMuted", true) && settings.micMuted);
		assert(ApplySetting(settings, "voice.voiceChatVolume", 0) && settings.voiceChatVolume == 0);
		assert(ApplySetting(settings, "voice.voiceChatVolume", 100) && settings.voiceChatVolume == 100);
		assert(ApplySetting(settings, "voice.openMicSensitivity", -80) && settings.openMicSensitivity == -80);
		assert(ApplySetting(settings, "voice.openMicSensitivity", 0) && settings.openMicSensitivity == 0);
		assert(ApplySetting(settings, "voice.voiceChat", false));
	}

	void RejectsWithoutChangingSettings()
	{
		VoiceSettings base;
		base.voiceChat = false;
		base.pushToTalk = false;
		base.pushToTalkKey = "F9";
		base.pushToTalkVirtualKey = 0x78;
		base.whisperKey = "LeftAlt";
		base.whisperVirtualKey = 0xA4;
		base.shoutKey = "LeftShift";
		base.shoutVirtualKey = 0xA0;
		base.voiceChatVolume = 40;
		base.mutePlayersByDefault = true;
		base.openMicSensitivity = -62;
		base.deafened = true;
		base.micMuted = true;
		base.players["Alpha"] = PlayerVoiceSettings{ 15, true };

		const std::pair<const char*, Json> rejected[] = {
			{ "voice.pushToTalkKey", "F10" },
			{ "voice.whisperKey", "F10" },
			{ "voice.shoutKey", "F10" },
			{ "voice.status", "ready" },
			{ "voice.unknown", true },
			{ "", true },
			{ "voice.voiceChat", "no" },
			{ "voice.voiceChat", 1 },
			{ "voice.pushToTalk", Json{} },
			{ "voice.mutePlayersByDefault", 0 },
			{ "voice.deafened", "true" },
			{ "voice.micMuted", 1.0 },
			{ "voice.voiceChatVolume", 50.5 },
			{ "voice.voiceChatVolume", "50" },
			{ "voice.voiceChatVolume", 101 },
			{ "voice.voiceChatVolume", -1 },
			{ "voice.voiceChatVolume", true },
			{ "voice.voiceChatVolume", std::uint64_t{ 18446744073709551615ULL } },
			{ "voice.openMicSensitivity", -81 },
			{ "voice.openMicSensitivity", 1 },
			{ "voice.openMicSensitivity", -2.5 },
			{ "voice.openMicSensitivity", std::uint64_t{ 18446744073709551536ULL } },
		};
		for (const auto& [key, value] : rejected) {
			VoiceSettings settings = base;
			assert(!ApplySetting(settings, key, value));
			assert(settings == base);
		}
	}

	void KeyCaptureSetsOnlyItsTarget()
	{
		VoiceSettings settings;
		ApplyKeyCapture(settings, KeyTarget::pushToTalk, "F9", 0x78);
		assert(settings.pushToTalkKey == "F9" && settings.pushToTalkVirtualKey == 0x78);
		assert(settings.whisperKey.empty() && settings.whisperVirtualKey == 0);
		assert(settings.shoutKey.empty() && settings.shoutVirtualKey == 0);

		ApplyKeyCapture(settings, KeyTarget::whisper, "LeftAlt", 0xA4);
		assert(settings.whisperKey == "LeftAlt" && settings.whisperVirtualKey == 0xA4);
		assert(settings.pushToTalkKey == "F9" && settings.pushToTalkVirtualKey == 0x78);
		assert(settings.shoutKey.empty() && settings.shoutVirtualKey == 0);

		ApplyKeyCapture(settings, KeyTarget::shout, "LeftShift", 0xA0);
		assert(settings.shoutKey == "LeftShift" && settings.shoutVirtualKey == 0xA0);
		assert(settings.whisperKey == "LeftAlt" && settings.whisperVirtualKey == 0xA4);
		assert(settings.pushToTalkKey == "F9" && settings.pushToTalkVirtualKey == 0x78);

		ApplyKeyCapture(settings, KeyTarget::whisper, "F9", 0x78);
		assert(settings.whisperKey == "F9" && settings.whisperVirtualKey == 0x78);
		assert(settings.pushToTalkKey == "F9" && settings.pushToTalkVirtualKey == 0x78);
	}

	void StatusEventCarriesEveryField()
	{
		auto event = Json::parse(StatusEvent(true, VoiceStatus::serverMuted, true, false));
		assert(event.size() == 5);
		assert(event["kind"] == "status");
		assert(event["active"] == true);
		assert(event["code"] == "serverMuted");
		assert(event["pending"] == true);
		assert(event["micHot"] == false);

		event = Json::parse(StatusEvent(false, VoiceStatus::ready, false, true));
		assert(event["active"] == false);
		assert(event["code"] == "ready");
		assert(event["pending"] == false);
		assert(event["micHot"] == true);
	}

	void SettingsEventHasTenKeysAndRoundTrips()
	{
		VoiceSettings settings;
		settings.voiceChat = false;
		settings.pushToTalk = false;
		settings.pushToTalkKey = "F9";
		settings.whisperKey = "LeftAlt";
		settings.shoutKey = "LeftShift";
		settings.openMicSensitivity = -62;
		settings.voiceChatVolume = 40;
		settings.mutePlayersByDefault = true;
		settings.deafened = true;
		settings.micMuted = true;

		const auto event = Json::parse(SettingsEvent(settings));
		assert(event["kind"] == "settings");
		const auto& values = event.at("values");
		assert(values.size() == 10);
		assert(values.at("voice.voiceChat") == false);
		assert(values.at("voice.pushToTalk") == false);
		assert(values.at("voice.pushToTalkKey") == "F9");
		assert(values.at("voice.whisperKey") == "LeftAlt");
		assert(values.at("voice.shoutKey") == "LeftShift");
		assert(values.at("voice.openMicSensitivity") == -62);
		assert(values.at("voice.voiceChatVolume") == 40);
		assert(values.at("voice.mutePlayersByDefault") == true);
		assert(values.at("voice.deafened") == true);
		assert(values.at("voice.micMuted") == true);
	}

	void PlayersEventRoundTripsAndSurvivesInvalidUtf8()
	{
		const std::vector<VoicePlayerRow> rows{
			{ 1, "Alpha", true, 40 },
			{ 9, std::string("\xff\xfe"), false, 100 }
		};
		const auto event = Json::parse(PlayersEvent(rows));
		assert(event["kind"] == "players");
		const auto& players = event.at("players");
		assert(players.size() == 2);
		assert(players.at(0).at("playerId") == 1);
		assert(players.at(0).at("name") == "Alpha");
		assert(players.at(0).at("muted") == true);
		assert(players.at(0).at("volume") == 40);
		assert(players.at(1).at("playerId") == 9);
		assert(players.at(1).at("name").is_string());
		assert(players.at(1).at("muted") == false);
		assert(players.at(1).at("volume") == 100);
	}

	void SpeakersEventListsIds()
	{
		const std::vector<std::uint32_t> ids{ 3, 9 };
		const auto event = Json::parse(SpeakersEvent(ids));
		assert(event["kind"] == "speakers");
		assert(event["playerIds"] == Json::array({ 3, 9 }));
		const auto empty = Json::parse(SpeakersEvent(std::span<const std::uint32_t>{}));
		assert(empty["playerIds"].empty());
	}

	void KeyCaptureEventCarriesStateNameAndCode()
	{
		const auto captured = Json::parse(KeyCaptureEvent(KeyTarget::whisper, true, "F9", 0x78));
		assert(captured["kind"] == "keyCapture");
		assert(captured["target"] == "voice.whisperKey");
		assert(captured["state"] == "captured");
		assert(captured["name"] == "F9");
		assert(captured["vk"] == 0x78);

		const auto cancelled = Json::parse(KeyCaptureEvent(KeyTarget::shout, false, "Left Shift", 0xA0));
		assert(cancelled["target"] == "voice.shoutKey");
		assert(cancelled["state"] == "cancelled");
		assert(cancelled["name"] == "");
		assert(cancelled["vk"] == 0);
	}

	void LevelEventCarriesDbfs()
	{
		const auto event = Json::parse(LevelEvent(-37.5));
		assert(event["kind"] == "level");
		assert(event["dbfs"] == -37.5);
	}
}

void RunUiProtocolTests()
{
	ParsesEveryValidOp();
	RejectsMalformedAndInvalidActions();
	RejectsActionsNestedDeeperThanEightLevels();
	PacketNestedDeeperThanEightLevelsIsNotWithinLimit();
	GarbageNeverThrows();
	KeyTargetNamesRoundTrip();
	AppliesAcceptedSettings();
	RejectsWithoutChangingSettings();
	KeyCaptureSetsOnlyItsTarget();
	StatusEventCarriesEveryField();
	SettingsEventHasTenKeysAndRoundTrips();
	PlayersEventRoundTripsAndSurvivesInvalidUtf8();
	SpeakersEventListsIds();
	KeyCaptureEventCarriesStateNameAndCode();
	LevelEventCarriesDbfs();
}
