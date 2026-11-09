#pragma once

#include "F4TVoiceGate.h"

#include <cstdint>

namespace F4T::Voice
{
	struct TransmitConfig
	{
		bool voiceChat{ true };
		bool pushToTalk{ true };
		float openMicThresholdDbfs{ static_cast<float>(DefaultOpenMicThresholdDbfs) };
		std::uint8_t pttVk{};
		std::uint8_t whisperVk{};
		std::uint8_t shoutVk{};
		bool micMuted{};
	};

	struct SpeakerPosition
	{
		std::uint16_t playerId{};
		double x{};
		double y{};
		double z{};
	};
}
