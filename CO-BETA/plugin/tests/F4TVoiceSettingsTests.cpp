#include "F4TVoiceSettings.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

namespace
{
	using F4T::Voice::LoadVoiceSettings;
	using F4T::Voice::PlayerVoiceSettings;
	using F4T::Voice::SaveVoiceSettings;
	using F4T::Voice::VoiceSettings;
	using F4T::Voice::VoiceSettingsPath;
	namespace fs = std::filesystem;

	fs::path MakeRoot()
	{
		auto root = fs::temp_directory_path() / "co-voice-settings-tests";
		std::error_code ec;
		fs::remove_all(root, ec);
		return root;
	}

	void WriteText(const fs::path& a_path, const std::string& a_text)
	{
		fs::create_directories(a_path.parent_path());
		std::ofstream output(a_path, std::ios::binary | std::ios::trunc);
		output << a_text;
		output.close();
		assert(output.good());
	}

	void DefaultsMatchPlan()
	{
		const VoiceSettings settings;
		assert(settings.voiceChat);
		assert(settings.pushToTalk);
		assert(settings.pushToTalkKey == "B");
		assert(settings.pushToTalkVirtualKey == 0x42);
		assert(settings.whisperKey.empty());
		assert(settings.shoutKey.empty());
		assert(settings.whisperVirtualKey == 0);
		assert(settings.shoutVirtualKey == 0);
		assert(settings.voiceChatVolume == 85);
		assert(!settings.mutePlayersByDefault);
		assert(settings.openMicSensitivity == -45);
		assert(!settings.deafened);
		assert(!settings.micMuted);
		assert(settings.players.empty());
	}

	void MissingFileLoadsDefaults(const fs::path& a_root)
	{
		assert(LoadVoiceSettings(a_root / "missing" / "voice-settings.json") == VoiceSettings{});
		assert(VoiceSettingsPath(fs::path{}).empty());
		assert(!SaveVoiceSettings(VoiceSettingsPath(fs::path{}), VoiceSettings{}));
		assert(LoadVoiceSettings(VoiceSettingsPath(fs::path{})) == VoiceSettings{});
		assert(!SaveVoiceSettings("voice-settings.json", VoiceSettings{}));
		assert(!fs::exists("voice-settings.json"));
	}

	void SaveThenLoadRoundTrips(const fs::path& a_root)
	{
		VoiceSettings settings;
		settings.voiceChat = false;
		settings.pushToTalk = false;
		settings.pushToTalkKey = "F9";
		settings.pushToTalkVirtualKey = 0x78;
		settings.whisperKey = "LeftAlt";
		settings.whisperVirtualKey = 0xA4;
		settings.shoutKey = "LeftShift";
		settings.shoutVirtualKey = 0xA0;
		settings.voiceChatVolume = 40;
		settings.mutePlayersByDefault = true;
		settings.openMicSensitivity = -62;
		settings.deafened = true;
		settings.micMuted = true;
		settings.players["alpha-profile"] = PlayerVoiceSettings{ 15, true };
		settings.players["bravo-profile"] = PlayerVoiceSettings{ 100, false };
		settings.players["charlie-profile"] = PlayerVoiceSettings{ 0, true };

		const auto path = VoiceSettingsPath(a_root / "CommonwealthOnline");
		assert(path.filename() == "voice-settings.json");

		assert(SaveVoiceSettings(path, VoiceSettings{}));
		assert(LoadVoiceSettings(path) == VoiceSettings{});

		assert(SaveVoiceSettings(path, settings));
		assert(!fs::exists(fs::path(path).concat(".tmp")));
		const auto loaded = LoadVoiceSettings(path);
		assert(loaded == settings);
		assert(loaded.players.size() == 3);
	}

	void InvalidUtf8SaveFailsCleanly(const fs::path& a_root)
	{
		VoiceSettings settings;
		settings.whisperKey = std::string(1, '\xFF');
		const auto path = a_root / "utf8" / "voice-settings.json";
		fs::create_directories(path.parent_path());
		assert(!SaveVoiceSettings(path, settings));
		assert(!fs::exists(path));
		assert(!fs::exists(fs::path(path).concat(".tmp")));
	}

	void MalformedJsonLoadsDefaults(const fs::path& a_root)
	{
		const auto path = a_root / "malformed" / "voice-settings.json";
		WriteText(path, "{ \"voiceChat\": false, ");
		assert(LoadVoiceSettings(path) == VoiceSettings{});
		WriteText(path, "[1, 2, 3]");
		assert(LoadVoiceSettings(path) == VoiceSettings{});
		WriteText(path, "");
		assert(LoadVoiceSettings(path) == VoiceSettings{});
	}

	void OutOfRangeValuesAreClamped(const fs::path& a_root)
	{
		const auto path = a_root / "clamp" / "voice-settings.json";
		WriteText(path, R"({
			"voiceChatVolume": 150,
			"openMicSensitivity": -200,
			"players": { "low": { "volume": -5, "muted": true }, "high": { "volume": 500 } }
		})");
		const auto loaded = LoadVoiceSettings(path);
		assert(loaded.voiceChatVolume == 100);
		assert(loaded.openMicSensitivity == -80);
		assert(loaded.players.at("low") == (PlayerVoiceSettings{ 0, true }));
		assert(loaded.players.at("high") == (PlayerVoiceSettings{ 100, false }));

		WriteText(path, R"({"voiceChatVolume": -1, "openMicSensitivity": 12})");
		const auto low = LoadVoiceSettings(path);
		assert(low.voiceChatVolume == 0);
		assert(low.openMicSensitivity == 0);
	}

	void PushToTalkKeyAndCodeLoadAsPair(const fs::path& a_root)
	{
		const auto path = a_root / "ptt-pair" / "voice-settings.json";
		WriteText(path, R"({"pushToTalkKey": "F9"})");
		const auto nameOnly = LoadVoiceSettings(path);
		assert(nameOnly.pushToTalkKey == "B");
		assert(nameOnly.pushToTalkVirtualKey == 0x42);
		WriteText(path, R"({"pushToTalkVirtualKey": 120})");
		const auto codeOnly = LoadVoiceSettings(path);
		assert(codeOnly.pushToTalkKey == "B");
		assert(codeOnly.pushToTalkVirtualKey == 0x42);
		WriteText(path, R"({"pushToTalkKey": 5, "pushToTalkVirtualKey": 120})");
		const auto wrongNameType = LoadVoiceSettings(path);
		assert(wrongNameType.pushToTalkKey == "B");
		assert(wrongNameType.pushToTalkVirtualKey == 0x42);
		WriteText(path, R"({"pushToTalkKey": "F9", "pushToTalkVirtualKey": 120})");
		const auto both = LoadVoiceSettings(path);
		assert(both.pushToTalkKey == "F9");
		assert(both.pushToTalkVirtualKey == 120);
		WriteText(path, R"({"pushToTalkKey": "F9", "pushToTalkVirtualKey": 255})");
		const auto reserved = LoadVoiceSettings(path);
		assert(reserved.pushToTalkKey == "B");
		assert(reserved.pushToTalkVirtualKey == 0x42);
		WriteText(path, R"({"pushToTalkKey": "F9", "pushToTalkVirtualKey": 254})");
		const auto highest = LoadVoiceSettings(path);
		assert(highest.pushToTalkKey == "F9");
		assert(highest.pushToTalkVirtualKey == 254);
		WriteText(path, R"({"pushToTalkKey": "F9", "pushToTalkVirtualKey": 1})");
		const auto lowest = LoadVoiceSettings(path);
		assert(lowest.pushToTalkKey == "F9");
		assert(lowest.pushToTalkVirtualKey == 1);
		for (const auto* code : { "0", "300", "-1", "120.7", "\"120\"", "256" }) {
			WriteText(path, std::string(R"({"pushToTalkKey": "F9", "pushToTalkVirtualKey": )") + code + "}");
			const auto rejected = LoadVoiceSettings(path);
			assert(rejected.pushToTalkKey == "B");
			assert(rejected.pushToTalkVirtualKey == 0x42);
		}
	}

	void WhisperAndShoutKeysLoadAsPair(const fs::path& a_root)
	{
		const auto path = a_root / "whisper-shout-pair" / "voice-settings.json";
		WriteText(path, R"({"whisperKey": "F9", "whisperVirtualKey": 120, "shoutKey": "LeftShift", "shoutVirtualKey": 160})");
		const auto both = LoadVoiceSettings(path);
		assert(both.whisperKey == "F9");
		assert(both.whisperVirtualKey == 120);
		assert(both.shoutKey == "LeftShift");
		assert(both.shoutVirtualKey == 160);
		WriteText(path, R"({"whisperKey": "", "whisperVirtualKey": 0, "shoutKey": "", "shoutVirtualKey": 0})");
		assert(LoadVoiceSettings(path) == VoiceSettings{});
		WriteText(path, R"({"whisperKey": "F9", "shoutKey": "F10"})");
		const auto nameOnly = LoadVoiceSettings(path);
		assert(nameOnly.whisperKey.empty() && nameOnly.whisperVirtualKey == 0);
		assert(nameOnly.shoutKey.empty() && nameOnly.shoutVirtualKey == 0);
		WriteText(path, R"({"whisperKey": "F9", "whisperVirtualKey": 255, "shoutKey": "F10", "shoutVirtualKey": 0})");
		const auto outOfRange = LoadVoiceSettings(path);
		assert(outOfRange.whisperKey.empty() && outOfRange.whisperVirtualKey == 0);
		assert(outOfRange.shoutKey.empty() && outOfRange.shoutVirtualKey == 0);
		WriteText(path, R"({"whisperKey": "F9", "whisperVirtualKey": 120.7, "shoutKey": "F10", "shoutVirtualKey": "161"})");
		const auto wrongTypes = LoadVoiceSettings(path);
		assert(wrongTypes.whisperKey.empty() && wrongTypes.whisperVirtualKey == 0);
		assert(wrongTypes.shoutKey.empty() && wrongTypes.shoutVirtualKey == 0);

		VoiceSettings settings;
		settings.whisperKey = "F9";
		settings.whisperVirtualKey = 120;
		settings.shoutKey = "F10";
		settings.shoutVirtualKey = 121;
		assert(SaveVoiceSettings(path, settings));
		assert(LoadVoiceSettings(path) == settings);
	}

	void WrongTypesAndUnknownKeysKeepDefaults(const fs::path& a_root)
	{
		const auto path = a_root / "types" / "voice-settings.json";
		WriteText(path, R"({"voiceChat": "no", "voiceChatVolume": "loud", "unknownKey": 7})");
		assert(LoadVoiceSettings(path) == VoiceSettings{});
	}

	void MissingPlayerFieldsDefault(const fs::path& a_root)
	{
		const auto path = a_root / "players" / "voice-settings.json";
		WriteText(path, R"({"players": {"empty": {}, "mutedOnly": {"muted": true}, "volumeOnly": {"volume": 30}, "notAnObject": 5}})");
		const auto loaded = LoadVoiceSettings(path);
		assert(loaded.players.size() == 3);
		assert(loaded.players.at("empty") == PlayerVoiceSettings{});
		assert(loaded.players.at("mutedOnly") == (PlayerVoiceSettings{ 100, true }));
		assert(loaded.players.at("volumeOnly") == (PlayerVoiceSettings{ 30, false }));
	}

	void LegacyPlayerIdKeysAreDroppedOnLoadAndSave(const fs::path& a_root)
	{
		const auto path = a_root / "legacy-keys" / "voice-settings.json";
		WriteText(path, R"({"players": {"playerId:7": {"volume": 100, "muted": true}, "Alpha": {"volume": 40, "muted": true}}})");
		const auto loaded = LoadVoiceSettings(path);
		assert(loaded.players.size() == 1);
		assert(loaded.players.at("Alpha") == (PlayerVoiceSettings{ 40, true }));
		VoiceSettings dirty;
		dirty.players["playerId:9"] = PlayerVoiceSettings{ 100, true };
		dirty.players["Bravo"] = PlayerVoiceSettings{ 50, false };
		assert(SaveVoiceSettings(path, dirty));
		assert(LoadVoiceSettings(path).players.size() == 1);
		assert(LoadVoiceSettings(path).players.at("Bravo") == (PlayerVoiceSettings{ 50, false }));
	}

	void ConcurrentSavesAndLoadsStayConsistent(const fs::path& a_root)
	{
		VoiceSettings first;
		first.voiceChatVolume = 10;
		first.players["alpha-profile"] = PlayerVoiceSettings{ 10, true };
		VoiceSettings second;
		second.voiceChatVolume = 90;
		second.players["bravo-profile"] = PlayerVoiceSettings{ 90, false };

		const auto path = VoiceSettingsPath(a_root / "concurrent");
		assert(SaveVoiceSettings(path, first));

		constexpr int kIterations = 200;
		std::atomic<int> failures{ 0 };
		const auto writer = [&](const VoiceSettings& a_settings) {
			for (int i = 0; i < kIterations; ++i) {
				if (!SaveVoiceSettings(path, a_settings)) {
					failures++;
				}
			}
		};
		std::thread firstWriter(writer, first);
		std::thread secondWriter(writer, second);
		std::thread reader([&] {
			for (int i = 0; i < kIterations * 4; ++i) {
				const auto loaded = LoadVoiceSettings(path);
				if (!(loaded == first || loaded == second)) {
					failures++;
				}
			}
		});
		firstWriter.join();
		secondWriter.join();
		reader.join();
		assert(failures == 0);
	}
}

int main()
{
	const auto root = MakeRoot();
	DefaultsMatchPlan();
	MissingFileLoadsDefaults(root);
	SaveThenLoadRoundTrips(root);
	InvalidUtf8SaveFailsCleanly(root);
	MalformedJsonLoadsDefaults(root);
	OutOfRangeValuesAreClamped(root);
	WrongTypesAndUnknownKeysKeepDefaults(root);
	MissingPlayerFieldsDefault(root);
	LegacyPlayerIdKeysAreDroppedOnLoadAndSave(root);
	PushToTalkKeyAndCodeLoadAsPair(root);
	WhisperAndShoutKeysLoadAsPair(root);
	ConcurrentSavesAndLoadsStayConsistent(root);

	std::error_code ec;
	fs::remove_all(root, ec);
	return 0;
}
