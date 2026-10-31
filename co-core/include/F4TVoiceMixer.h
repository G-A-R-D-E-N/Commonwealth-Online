#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace F4T::Voice
{
	inline constexpr std::size_t MixerSpeakerCount = 4;
	inline constexpr std::size_t MixerFrameSamples = 960;

	struct StereoGain
	{
		double left{};
		double right{};
	};

	struct MixerSpeaker
	{
		std::array<std::int16_t, MixerFrameSamples> pcm{};
		StereoGain gain{};
		int volume{ 100 };
		StereoGain applied{};
	};

	[[nodiscard]] double DistanceGain(double a_distance, double a_range) noexcept;

	[[nodiscard]] double SpeakerPan(
		double a_listenerX,
		double a_listenerY,
		double a_listenerHeading,
		double a_speakerX,
		double a_speakerY) noexcept;

	[[nodiscard]] StereoGain ConstantPowerGains(double a_pan) noexcept;

	void MixFrame(
		std::array<MixerSpeaker, MixerSpeakerCount>& a_speakers,
		int a_masterVolume,
		std::array<float, MixerFrameSamples * 2>& a_out) noexcept;
}
