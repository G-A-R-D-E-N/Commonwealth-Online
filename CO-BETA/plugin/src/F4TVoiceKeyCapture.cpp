#include "F4TVoiceKeyCapture.h"

#include <algorithm>
#include <format>
#include <string_view>

namespace F4T::Voice
{
	namespace
	{
		constexpr std::uint8_t kFirstKey = 0x01;
		constexpr std::uint8_t kLastKey = 0xFE;
		constexpr std::uint8_t kEscapeKey = 0x1B;
		constexpr std::array<std::uint8_t, 4> kMenuReserved{ 0x0D, 0x20, 0x26, 0x28 };
		constexpr std::array<std::uint8_t, 6> kNotCapturable{ 0x01, 0x02, 0x10, 0x11, 0x12, kEscapeKey };

		std::string_view NamedKeyName(std::uint8_t a_vk)
		{
			switch (a_vk) {
			case 0x04:
				return "Middle Mouse";
			case 0x05:
				return "Mouse 4";
			case 0x06:
				return "Mouse 5";
			case 0x08:
				return "Backspace";
			case 0x09:
				return "Tab";
			case 0x0D:
				return "Enter";
			case 0x13:
				return "Pause";
			case 0x14:
				return "Caps Lock";
			case 0x20:
				return "Space";
			case 0x21:
				return "Page Up";
			case 0x22:
				return "Page Down";
			case 0x23:
				return "End";
			case 0x24:
				return "Home";
			case 0x25:
				return "Left";
			case 0x26:
				return "Up";
			case 0x27:
				return "Right";
			case 0x28:
				return "Down";
			case 0x2D:
				return "Insert";
			case 0x2E:
				return "Delete";
			case 0x6A:
				return "Num *";
			case 0x6B:
				return "Num +";
			case 0x6D:
				return "Num -";
			case 0x6E:
				return "Num .";
			case 0x6F:
				return "Num /";
			case 0xA0:
				return "Left Shift";
			case 0xA1:
				return "Right Shift";
			case 0xA2:
				return "Left Ctrl";
			case 0xA3:
				return "Right Ctrl";
			case 0xA4:
				return "Left Alt";
			case 0xA5:
				return "Right Alt";
			case 0xBA:
				return ";";
			case 0xBB:
				return "=";
			case 0xBC:
				return ",";
			case 0xBD:
				return "-";
			case 0xBE:
				return ".";
			case 0xBF:
				return "/";
			case 0xC0:
				return "`";
			case 0xDB:
				return "[";
			case 0xDC:
				return "\\";
			case 0xDD:
				return "]";
			case 0xDE:
				return "'";
			default:
				return {};
			}
		}

		bool Capturable(std::uint8_t a_vk)
		{
			return !VirtualKeyMenuReserved(a_vk) && std::find(kNotCapturable.begin(), kNotCapturable.end(), a_vk) == kNotCapturable.end();
		}
	}

	bool VirtualKeyMenuReserved(std::uint8_t a_vk)
	{
		return std::find(kMenuReserved.begin(), kMenuReserved.end(), a_vk) != kMenuReserved.end();
	}

	std::string VirtualKeyName(std::uint8_t a_vk)
	{
		if ((a_vk >= 0x41 && a_vk <= 0x5A) || (a_vk >= 0x30 && a_vk <= 0x39)) {
			return std::string(1, static_cast<char>(a_vk));
		}
		if (a_vk >= 0x70 && a_vk <= 0x87) {
			return std::format("F{}", a_vk - 0x6F);
		}
		if (a_vk >= 0x60 && a_vk <= 0x69) {
			return std::format("Num {}", a_vk - 0x60);
		}
		if (const auto named = NamedKeyName(a_vk); !named.empty()) {
			return std::string{ named };
		}
		return std::format("Key 0x{:02X}", static_cast<unsigned>(a_vk));
	}

	void KeyCapture::Begin(KeyTarget a_target, double a_now, const std::function<bool(std::uint8_t)>& a_keyDown)
	{
		m_target = a_target;
		m_startedAt = a_now;
		m_active = true;
		for (std::uint8_t vk = kFirstKey; vk <= kLastKey; ++vk) {
			m_wasDown[vk] = a_keyDown(vk);
		}
	}

	std::optional<KeyCaptureResult> KeyCapture::Poll(double a_now, const std::function<bool(std::uint8_t)>& a_keyDown)
	{
		if (!m_active) {
			return std::nullopt;
		}
		if (a_now - m_startedAt >= kKeyCaptureTimeoutSeconds) {
			m_active = false;
			return KeyCaptureResult{ m_target, false, 0, {} };
		}

		bool escapePressed = false;
		std::uint8_t captured = 0;
		for (std::uint8_t vk = kFirstKey; vk <= kLastKey; ++vk) {
			const bool down = a_keyDown(vk);
			const bool pressed = down && !m_wasDown[vk];
			m_wasDown[vk] = down;
			if (!pressed) {
				continue;
			}
			if (vk == kEscapeKey) {
				escapePressed = true;
			} else if (captured == 0 && Capturable(vk)) {
				captured = vk;
			}
		}

		if (escapePressed) {
			m_active = false;
			return KeyCaptureResult{ m_target, false, 0, {} };
		}
		if (captured == 0) {
			return std::nullopt;
		}
		m_active = false;
		return KeyCaptureResult{ m_target, true, captured, VirtualKeyName(captured) };
	}

	bool KeyCapture::Active() const noexcept
	{
		return m_active;
	}
}
