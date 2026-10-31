#include "F4TVoiceKeyCapture.h"

#include "F4TVoiceSettings.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace
{
	using F4T::Voice::KeyCapture;
	using F4T::Voice::KeyTarget;
	using F4T::Voice::VirtualKeyName;
	using KeyDown = std::function<bool(std::uint8_t)>;
	using KeySet = std::set<std::uint8_t>;

	KeyDown Down(const KeySet& a_down)
	{
		return [&a_down](std::uint8_t a_vk) {
			return a_down.contains(a_vk);
		};
	}

	void HeldAtBeginIsIgnoredUntilReleased()
	{
		KeySet down{ 0x42 };
		KeyCapture capture;
		capture.Begin(KeyTarget::pushToTalk, 0.0, Down(down));
		assert(capture.Active());
		assert(!capture.Poll(0.1, Down(down)).has_value());
		down.clear();
		assert(!capture.Poll(0.2, Down(down)).has_value());
		assert(capture.Active());
		down.insert(0x42);
		const auto result = capture.Poll(0.3, Down(down));
		assert(result && result->captured);
		assert(result->target == KeyTarget::pushToTalk && result->vk == 0x42 && result->name == "B");
		assert(!capture.Active());
		assert(!capture.Poll(0.4, Down(down)).has_value());
	}

	void EscapeCancelsCapture()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::shout, 0.0, Down(down));
		down.insert(0x1B);
		const auto result = capture.Poll(0.1, Down(down));
		assert(result && !result->captured);
		assert(result->target == KeyTarget::shout && result->vk == 0 && result->name.empty());
		assert(!capture.Active());
		assert(!capture.Poll(0.2, Down(down)).has_value());
	}

	void EscapeBeatsLetterInSamePoll()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::whisper, 0.0, Down(down));
		down = { 0x1B, 0x41 };
		const auto result = capture.Poll(0.1, Down(down));
		assert(result && !result->captured && result->vk == 0);
		assert(!capture.Active());
		assert(!capture.Poll(0.2, Down(down)).has_value());
	}

	void NonCapturableKeysAreIgnored()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::pushToTalk, 0.0, Down(down));
		down = { 0x01, 0x02, 0x10, 0x11, 0x12 };
		assert(!capture.Poll(0.1, Down(down)).has_value());
		assert(capture.Active());
		down.insert(0xA0);
		const auto result = capture.Poll(0.2, Down(down));
		assert(result && result->captured && result->vk == 0xA0 && result->name == "Left Shift");
		assert(!capture.Active());
	}

	void MenuNavigationKeysAreNotCapturable()
	{
		const std::uint8_t menuKeys[] = { 0x0D, 0x20, 0x26, 0x28 };
		for (const auto vk : menuKeys) {
			KeyCapture capture;
			KeySet down;
			capture.Begin(KeyTarget::pushToTalk, 0.0, Down(down));
			down = { vk };
			assert(!capture.Poll(0.1, Down(down)).has_value());
			assert(capture.Active());
			down.insert(0x41);
			const auto result = capture.Poll(0.2, Down(down));
			assert(result && result->captured && result->vk == 0x41);
		}
	}

	void MenuReservedKeysFailToLoadAsBindings()
	{
		const auto root = std::filesystem::temp_directory_path() / "voice-key-capture-load";
		const auto path = root / "voice-settings.json";
		std::filesystem::create_directories(root);
		const F4T::Voice::VoiceSettings defaults;
		for (const int vk : { 0x0D, 0x20, 0x26, 0x28 }) {
			{
				std::ofstream out(path, std::ios::trunc);
				out << "{\"pushToTalkKey\": \"Key\", \"pushToTalkVirtualKey\": " << vk << "}";
			}
			const auto loaded = F4T::Voice::LoadVoiceSettings(path);
			assert(loaded.pushToTalkKey == defaults.pushToTalkKey);
			assert(loaded.pushToTalkVirtualKey == defaults.pushToTalkVirtualKey);
		}
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}

	void LowestNewlyPressedKeyIsCaptured()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::pushToTalk, 0.0, Down(down));
		down = { 0x44, 0x41 };
		const auto result = capture.Poll(0.1, Down(down));
		assert(result && result->captured && result->vk == 0x41 && result->name == "A");
	}

	void TimeoutCancelsAtTenSeconds()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::pushToTalk, 0.0, Down(down));
		assert(!capture.Poll(9.99, Down(down)).has_value());
		assert(capture.Active());
		const auto result = capture.Poll(10.0, Down(down));
		assert(result && !result->captured && result->vk == 0 && result->name.empty());
		assert(!capture.Active());
		assert(!capture.Poll(10.1, Down(down)).has_value());
	}

	void BeginReplacesActiveCaptureSilently()
	{
		KeyCapture capture;
		KeySet down;
		capture.Begin(KeyTarget::whisper, 0.0, Down(down));
		capture.Begin(KeyTarget::shout, 0.5, Down(down));
		down = { 0x41 };
		const auto result = capture.Poll(1.0, Down(down));
		assert(result && result->captured);
		assert(result->target == KeyTarget::shout && result->vk == 0x41 && result->name == "A");
		assert(!capture.Active());
	}

	void PollWithoutCaptureReturnsNothing()
	{
		KeyCapture capture;
		KeySet down{ 0x41 };
		assert(!capture.Poll(0.0, Down(down)).has_value());
		assert(!capture.Active());
	}

	void NamesEveryTableGroupAndTheFallback()
	{
		const std::pair<int, std::string> names[] = {
			{ 0x41, "A" }, { 0x5A, "Z" }, { 0x30, "0" }, { 0x39, "9" },
			{ 0x70, "F1" }, { 0x87, "F24" },
			{ 0x60, "Num 0" }, { 0x69, "Num 9" }, { 0x6A, "Num *" }, { 0x6B, "Num +" },
			{ 0x6D, "Num -" }, { 0x6E, "Num ." }, { 0x6F, "Num /" },
			{ 0x08, "Backspace" }, { 0x09, "Tab" }, { 0x0D, "Enter" }, { 0x13, "Pause" },
			{ 0x14, "Caps Lock" }, { 0x20, "Space" }, { 0x21, "Page Up" }, { 0x22, "Page Down" },
			{ 0x23, "End" }, { 0x24, "Home" }, { 0x25, "Left" }, { 0x26, "Up" }, { 0x27, "Right" },
			{ 0x28, "Down" }, { 0x2D, "Insert" }, { 0x2E, "Delete" },
			{ 0xA0, "Left Shift" }, { 0xA1, "Right Shift" }, { 0xA2, "Left Ctrl" },
			{ 0xA3, "Right Ctrl" }, { 0xA4, "Left Alt" }, { 0xA5, "Right Alt" },
			{ 0x04, "Middle Mouse" }, { 0x05, "Mouse 4" }, { 0x06, "Mouse 5" },
			{ 0xBA, ";" }, { 0xBB, "=" }, { 0xBC, "," }, { 0xBD, "-" }, { 0xBE, "." },
			{ 0xBF, "/" }, { 0xC0, "`" }, { 0xDB, "[" }, { 0xDC, "\\" }, { 0xDD, "]" }, { 0xDE, "'" },
			{ 0xE5, "Key 0xE5" }, { 0x07, "Key 0x07" }
		};
		for (const auto& [vk, name] : names) {
			assert(VirtualKeyName(static_cast<std::uint8_t>(vk)) == name);
		}
		const F4T::Voice::VoiceSettings defaults;
		assert(VirtualKeyName(defaults.pushToTalkVirtualKey) == defaults.pushToTalkKey);
		assert(VirtualKeyName(0x42) == "B");
	}
}

void RunKeyCaptureTests()
{
	HeldAtBeginIsIgnoredUntilReleased();
	EscapeCancelsCapture();
	EscapeBeatsLetterInSamePoll();
	NonCapturableKeysAreIgnored();
	MenuNavigationKeysAreNotCapturable();
	MenuReservedKeysFailToLoadAsBindings();
	LowestNewlyPressedKeyIsCaptured();
	TimeoutCancelsAtTenSeconds();
	BeginReplacesActiveCaptureSilently();
	PollWithoutCaptureReturnsNothing();
	NamesEveryTableGroupAndTheFallback();
}
