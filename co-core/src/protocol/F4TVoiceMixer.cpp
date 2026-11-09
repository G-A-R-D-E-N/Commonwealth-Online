#include "F4TVoiceMixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace F4T::Voice
{
	namespace
	{
		constexpr float Int16Scale = 1.0F / 32768.0F;
		constexpr std::size_t MixerRampSamples = 480;

		double VolumeScale(int a_volume) noexcept
		{
			return std::clamp(a_volume, 0, 100) / 100.0;
		}

		StereoGain RampValueAt(StereoGain a_from, StereoGain a_to, std::size_t a_sample) noexcept
		{
			const auto t = static_cast<double>(std::min(a_sample, MixerRampSamples)) / static_cast<double>(MixerRampSamples);
			return {
				std::lerp(a_from.left, a_to.left, t),
				std::lerp(a_from.right, a_to.right, t)
			};
		}
	}

	double DistanceGain(double a_distance, double a_range) noexcept
	{
		if (!(a_distance < a_range)) {
			return 0.0;
		}
		return std::clamp(1.0 - a_distance / a_range, 0.0, 1.0);
	}

	double SpeakerPan(
		double a_listenerX,
		double a_listenerY,
		double a_listenerHeading,
		double a_speakerX,
		double a_speakerY) noexcept
	{
		const auto dx = a_speakerX - a_listenerX;
		const auto dy = a_speakerY - a_listenerY;
		const auto forwardX = std::sin(a_listenerHeading);
		const auto forwardY = std::cos(a_listenerHeading);
		const auto rightX = std::cos(a_listenerHeading);
		const auto rightY = -std::sin(a_listenerHeading);
		const auto along = dx * forwardX + dy * forwardY;
		const auto across = dx * rightX + dy * rightY;
		return std::sin(std::atan2(across, along));
	}

	StereoGain ConstantPowerGains(double a_pan) noexcept
	{
		const auto angle = (a_pan + 1.0) * std::numbers::pi / 4.0;
		return { std::cos(angle), std::sin(angle) };
	}

	void MixFrame(
		std::array<MixerSpeaker, MixerSpeakerCount>& a_speakers,
		int a_masterVolume,
		std::array<float, MixerFrameSamples * 2>& a_out) noexcept
	{
		a_out.fill(0.0F);
		const auto master = VolumeScale(a_masterVolume);
		for (auto& speaker : a_speakers) {
			const auto volume = master * VolumeScale(speaker.volume);
			const StereoGain raw{ speaker.gain.left * volume, speaker.gain.right * volume };
			if (!std::isfinite(raw.left) || !std::isfinite(raw.right)) {
				continue;
			}
			const StereoGain target{ std::clamp(raw.left, 0.0, 1.0), std::clamp(raw.right, 0.0, 1.0) };
			for (std::size_t n = 0; n < MixerFrameSamples; ++n) {
				const auto gain = RampValueAt(speaker.applied, target, n);
				const auto sample = static_cast<float>(speaker.pcm[n]) * Int16Scale;
				a_out[2 * n] += sample * static_cast<float>(gain.left);
				a_out[2 * n + 1] += sample * static_cast<float>(gain.right);
			}
			speaker.applied = target;
		}
		for (auto& sample : a_out) {
			sample = std::clamp(sample, -1.0F, 1.0F);
		}
	}
}
