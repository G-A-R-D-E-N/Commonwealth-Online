#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::Voice
{
	enum class VoiceStatus : std::uint8_t
	{
		ready,
		transmitting,
		idle,
		serverNoVoice,
		micFailed,
		micRecovering,
		micSilentHint,
		noOutput,
		serverMuted,
		rateLimited,
		profileMismatch,
		deafened,
		micMuted,
		codecFailed
	};

	struct VoiceStatusInputs
	{
		bool profileMismatch{};
		bool serverNoVoice{};
		bool serverMuted{};
		bool rateLimited{};
		bool codecFailed{};
		bool micRecovering{};
		bool micFailed{};
		bool noOutput{};
		bool micMuted{};
		bool deafened{};
		bool micSilentHint{};
		bool transmitting{};
		bool idle{};
	};

	[[nodiscard]] constexpr std::string_view VoiceStatusCode(VoiceStatus a_status) noexcept
	{
		switch (a_status) {
		case VoiceStatus::ready:
			return "ready";
		case VoiceStatus::transmitting:
			return "transmitting";
		case VoiceStatus::idle:
			return "idle";
		case VoiceStatus::serverNoVoice:
			return "serverNoVoice";
		case VoiceStatus::micFailed:
			return "micFailed";
		case VoiceStatus::micRecovering:
			return "micRecovering";
		case VoiceStatus::micSilentHint:
			return "micSilentHint";
		case VoiceStatus::noOutput:
			return "noOutput";
		case VoiceStatus::serverMuted:
			return "serverMuted";
		case VoiceStatus::rateLimited:
			return "rateLimited";
		case VoiceStatus::profileMismatch:
			return "profileMismatch";
		case VoiceStatus::deafened:
			return "deafened";
		case VoiceStatus::micMuted:
			return "micMuted";
		case VoiceStatus::codecFailed:
			return "codecFailed";
		}
		return {};
	}

	[[nodiscard]] constexpr std::string_view VoiceStatusText(VoiceStatus a_status) noexcept
	{
		switch (a_status) {
		case VoiceStatus::serverNoVoice:
			return "This server does not support voice";
		case VoiceStatus::micFailed:
			return "Microphone not available";
		case VoiceStatus::micRecovering:
			return "Reconnecting microphone";
		case VoiceStatus::micSilentHint:
			return "Your microphone may be muted in Windows";
		case VoiceStatus::noOutput:
			return "Voice playback failed: no audio output device";
		case VoiceStatus::codecFailed:
			return "Voice encoding failed; retry voice";
		default:
			return {};
		}
	}

	[[nodiscard]] constexpr VoiceStatus SelectVoiceStatus(const VoiceStatusInputs& a_inputs) noexcept
	{
		if (a_inputs.profileMismatch) {
			return VoiceStatus::profileMismatch;
		}
		if (a_inputs.serverNoVoice) {
			return VoiceStatus::serverNoVoice;
		}
		if (a_inputs.micRecovering) {
			return VoiceStatus::micRecovering;
		}
		if (a_inputs.micFailed) {
			return VoiceStatus::micFailed;
		}
		if (a_inputs.codecFailed) {
			return VoiceStatus::codecFailed;
		}
		if (a_inputs.serverMuted) {
			return VoiceStatus::serverMuted;
		}
		if (a_inputs.rateLimited) {
			return VoiceStatus::rateLimited;
		}
		if (a_inputs.noOutput) {
			return VoiceStatus::noOutput;
		}
		if (a_inputs.micMuted) {
			return VoiceStatus::micMuted;
		}
		if (a_inputs.deafened) {
			return VoiceStatus::deafened;
		}
		if (a_inputs.micSilentHint) {
			return VoiceStatus::micSilentHint;
		}
		if (a_inputs.transmitting) {
			return VoiceStatus::transmitting;
		}
		if (a_inputs.idle) {
			return VoiceStatus::idle;
		}
		return VoiceStatus::ready;
	}
}
