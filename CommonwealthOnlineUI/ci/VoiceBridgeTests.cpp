#include "F4TVoiceBridgeState.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

using F4T::VoiceBridge::AllowedActionOp;
using F4T::VoiceBridge::EventCache;
using F4T::VoiceBridge::EventScript;
using F4T::VoiceBridge::HudWanted;
using F4T::VoiceBridge::kActionMaxBytes;
using F4T::VoiceBridge::kEventMaxBytes;
using F4T::VoiceBridge::ParseEvent;

namespace
{
	constexpr std::string_view kStatusReady = R"({"kind":"status","active":true,"code":"ready","pending":false})";
	constexpr std::string_view kStatusIdle = R"({"kind":"status","active":false,"code":"idle","pending":false})";
	constexpr std::string_view kSettingsOn = R"({"kind":"settings","values":{"voice.voiceChat":true}})";
	constexpr std::string_view kSettingsOff = R"({"kind":"settings","values":{"voice.voiceChat":false}})";
	constexpr std::string_view kPlayers = R"({"kind":"players","players":[]})";
	constexpr std::string_view kSpeakers = R"({"kind":"speakers","playerIds":[2]})";
	constexpr std::string_view kKeyCapture = R"({"kind":"keyCapture","target":"voice.whisperKey","state":"cancelled","name":"","vk":0})";
	constexpr std::string_view kLevel = R"({"kind":"level","dbfs":-30})";

	nlohmann::json Event(std::string_view a_json)
	{
		return *ParseEvent(a_json);
	}

	std::string Padded(std::string_view a_head, std::size_t a_size)
	{
		const std::string tail = R"("})";
		return std::string(a_head) + std::string(a_size - a_head.size() - tail.size(), 'x') + tail;
	}

	std::string Nested(std::string_view a_head, std::size_t a_levels)
	{
		return std::string(a_head) + std::string(a_levels, '[') + std::string(a_levels, ']') + "}";
	}

	void TestAllowedOps()
	{
		assert(AllowedActionOp(R"({"op":"setSetting","key":"voice.pushToTalk","value":true})") == "setSetting");
		assert(AllowedActionOp(R"({"op":"beginKeyCapture","target":"voice.whisperKey"})") == "beginKeyCapture");
		assert(AllowedActionOp(R"({"op":"mutePlayer","playerId":3,"muted":true})") == "mutePlayer");
		assert(AllowedActionOp(R"({"op":"setPlayerVolume","playerId":3,"volume":50})") == "setPlayerVolume");

		assert(!AllowedActionOp(R"({"op":"deafen"})").has_value());
		assert(!AllowedActionOp(R"({"op":"SetSetting"})").has_value());
		assert(!AllowedActionOp(R"({"op":"setSettingX"})").has_value());
		assert(!AllowedActionOp(R"({"op":"beginKeyCapturex"})").has_value());
		assert(!AllowedActionOp(R"({"op":"mutePlayer2"})").has_value());
		assert(!AllowedActionOp(R"({"op":"setPlayerVolumeX"})").has_value());
		assert(!AllowedActionOp(R"({"key":"voice.pushToTalk"})").has_value());
		assert(!AllowedActionOp(R"({"op":7})").has_value());
		assert(!AllowedActionOp(R"(["op","setSetting"])").has_value());
		assert(!AllowedActionOp(R"({"op":"mutePlayer")").has_value());
		assert(!AllowedActionOp("").has_value());
		assert(!AllowedActionOp(Nested(R"({"op":"setSetting","value":)", 1000)).has_value());
		assert(AllowedActionOp(Nested(R"({"op":"setSetting","value":)", 7)) == "setSetting");
		assert(!AllowedActionOp(Nested(R"({"op":"setSetting","value":)", 8)).has_value());
	}

	void TestActionSizeLimit()
	{
		assert(kActionMaxBytes == 4096);
		const auto exact = Padded(R"({"op":"mutePlayer","pad":")", kActionMaxBytes);
		assert(exact.size() == kActionMaxBytes);
		assert(AllowedActionOp(exact) == "mutePlayer");

		const auto over = Padded(R"({"op":"mutePlayer","pad":")", kActionMaxBytes + 1);
		assert(over.size() == kActionMaxBytes + 1);
		assert(!AllowedActionOp(over).has_value());
	}

	void TestEventKinds()
	{
		assert(kEventMaxBytes == 32768);
		assert(ParseEvent(kStatusReady).has_value());
		assert(ParseEvent(kSettingsOn).has_value());
		assert(ParseEvent(kPlayers).has_value());
		assert(ParseEvent(kSpeakers).has_value());
		assert(ParseEvent(kKeyCapture).has_value());
		assert(ParseEvent(kLevel).has_value());
		assert(ParseEvent(R"({"kind":"players","players":[{"playerId":3,"name":"a","muted":false,"volume":50}]})").has_value());

		assert(!ParseEvent(R"({"kind":"mystery"})").has_value());
		assert(!ParseEvent(R"({"active":true})").has_value());
		assert(!ParseEvent(R"([{"kind":"level"}])").has_value());
		assert(!ParseEvent(R"({"kind":"level")").has_value());
		assert(!ParseEvent("").has_value());

		const auto exact = Padded(R"({"kind":"level","pad":")", kEventMaxBytes);
		assert(exact.size() == kEventMaxBytes);
		assert(ParseEvent(exact).has_value());
		assert(!ParseEvent(Padded(R"({"kind":"level","pad":")", kEventMaxBytes + 1)).has_value());
	}

	void TestEventNestingIsBounded()
	{
		constexpr std::string_view head = R"({"kind":"level","deep":)";
		assert(ParseEvent(Nested(head, 7)).has_value());
		assert(!ParseEvent(Nested(head, 8)).has_value());
		assert(!ParseEvent(Nested(head, 1000)).has_value());
	}

	void TestCacheKeepsLastAndReplaysInOrder()
	{
		EventCache cache;
		assert(cache.ReplayScripts().empty());

		cache.Store(Event(kSpeakers));
		cache.Store(Event(kPlayers));
		cache.Store(Event(kSettingsOn));
		cache.Store(Event(kStatusIdle));
		cache.Store(Event(kStatusReady));
		cache.Store(Event(kSettingsOff));
		cache.Store(Event(R"({"kind":"speakers","playerIds":[5]})"));

		const auto scripts = cache.ReplayScripts();
		assert(scripts.size() == 4);
		assert(scripts[0] == EventScript(Event(kStatusReady)));
		assert(scripts[1] == EventScript(Event(kSettingsOff)));
		assert(scripts[2] == EventScript(Event(kPlayers)));
		assert(scripts[3] == EventScript(Event(R"({"kind":"speakers","playerIds":[5]})")));
	}

	void TestCacheSkipsUncacheableKinds()
	{
		EventCache cache;
		cache.Store(Event(kKeyCapture));
		cache.Store(Event(kLevel));
		assert(cache.ReplayScripts().empty());
		assert(!cache.SessionActive());
	}

	void TestSessionActive()
	{
		EventCache cache;
		assert(!cache.SessionActive());

		cache.Store(Event(R"({"kind":"status","active":"true","code":"ready","pending":false})"));
		assert(!cache.SessionActive());
		cache.Store(Event(R"({"kind":"status","active":1,"code":"ready","pending":false})"));
		assert(!cache.SessionActive());
		cache.Store(Event(R"({"kind":"status","code":"ready","pending":false})"));
		assert(!cache.SessionActive());

		cache.Store(Event(kStatusReady));
		assert(cache.SessionActive());
		cache.Store(Event(kStatusIdle));
		assert(!cache.SessionActive());
	}

	void TestEventScriptEscaping()
	{
		const auto script = EventScript(Event(R"({"kind":"level","dbfs":-1,"name":"a\"b\\c </script>"})"));
		assert(script.starts_with("if(window.coVoiceEvent){window.coVoiceEvent("));
		assert(script.ends_with(");}"));
		const std::string expected = std::string(R"(if(window.coVoiceEvent){window.coVoiceEvent({"dbfs":-1,"kind":"level","name":"a\"b\\c)") + "\\u2028" + R"(</script>"});})";
		assert(script == expected);
	}

	void TestHudWanted()
	{
		EventCache active;
		active.Store(Event(kStatusReady));
		const EventCache inactive{};

		const EventCache* const caches[]{ &active, &inactive };
		for (const auto* cache : caches) {
			for (int mask = 0; mask < 8; ++mask) {
				const bool mainMenu = (mask & 1) != 0;
				const bool browser = (mask & 2) != 0;
				const bool lockout = (mask & 4) != 0;
				const bool expected = cache == &active && !mainMenu && !browser && !lockout;
				assert(HudWanted(*cache, mainMenu, browser, lockout) == expected);
			}
		}
	}

	void TestMicHot()
	{
		EventCache hot;
		hot.Store(Event(R"({"kind":"status","active":true,"code":"ready","pending":false,"micHot":true})"));
		assert(hot.MicHot());
		for (int mask = 0; mask < 8; ++mask) {
			const bool mainMenu = (mask & 1) != 0;
			const bool browser = (mask & 2) != 0;
			const bool lockout = (mask & 4) != 0;
			assert(HudWanted(hot, mainMenu, browser, lockout));
		}

		EventCache idleHot;
		idleHot.Store(Event(R"({"kind":"status","active":false,"code":"idle","pending":false,"micHot":true})"));
		assert(idleHot.MicHot());
		assert(!HudWanted(idleHot, false, false, false));

		EventCache stringHot;
		stringHot.Store(Event(R"({"kind":"status","active":true,"code":"ready","pending":false,"micHot":"true"})"));
		assert(!stringHot.MicHot());
		assert(!HudWanted(stringHot, true, false, false));

		EventCache numberHot;
		numberHot.Store(Event(R"({"kind":"status","active":true,"code":"ready","pending":false,"micHot":1})"));
		assert(!numberHot.MicHot());
		assert(!HudWanted(numberHot, false, true, false));

		EventCache missing;
		missing.Store(Event(kStatusReady));
		assert(!missing.MicHot());
	}
}

int main()
{
	TestAllowedOps();
	TestActionSizeLimit();
	TestEventKinds();
	TestEventNestingIsBounded();
	TestCacheKeepsLastAndReplaysInOrder();
	TestCacheSkipsUncacheableKinds();
	TestSessionActive();
	TestEventScriptEscaping();
	TestHudWanted();
	TestMicHot();
	return 0;
}
