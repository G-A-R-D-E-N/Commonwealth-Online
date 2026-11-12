#pragma once

#include "F4TVoiceUiProtocol.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace F4T::Voice
{
	inline constexpr double kKeyCaptureTimeoutSeconds = 10.0;

	struct KeyCaptureResult
	{
		KeyTarget target{};
		bool captured{};
		std::uint8_t vk{};
		std::string name;
	};

	[[nodiscard]] std::string VirtualKeyName(std::uint8_t a_vk);
	[[nodiscard]] bool VirtualKeyMenuReserved(std::uint8_t a_vk);

	class KeyCapture
	{
	public:
		void Begin(KeyTarget a_target, double a_now, const std::function<bool(std::uint8_t)>& a_keyDown);
		[[nodiscard]] std::optional<KeyCaptureResult> Poll(double a_now, const std::function<bool(std::uint8_t)>& a_keyDown);
		[[nodiscard]] bool Active() const noexcept;

	private:
		KeyTarget m_target{};
		double m_startedAt{};
		bool m_active{};
		std::array<bool, 256> m_wasDown{};
	};
}
