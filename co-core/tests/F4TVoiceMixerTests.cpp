#include "F4TVoiceMixer.h"
#include "F4TVoiceTiers.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>

int main()
{
	using namespace F4T::Voice;

	using Mixer = std::array<MixerSpeaker, MixerSpeakerCount>;
	using Output = std::array<float, MixerFrameSamples * 2>;

	const auto range = TierRange(Tier::Normal);
	assert(DistanceGain(0.0, range) == 1.0);
	assert(DistanceGain(range / 2.0, range) == 0.5);
	assert(DistanceGain(range, range) == 0.0);
	assert(DistanceGain(range * 2.0, range) == 0.0);
	assert(DistanceGain(0.0, 0.0) == 0.0);
	assert(DistanceGain(std::numeric_limits<double>::quiet_NaN(), range) == 0.0);
	assert(DistanceGain(-1.0, range) == 1.0);

	const auto gainsFor = [](double a_heading, double a_x, double a_y) {
		return ConstantPowerGains(SpeakerPan(0.0, 0.0, a_heading, a_x, a_y));
	};

	const auto ahead = gainsFor(0.0, 0.0, 10.0);
	assert(std::abs(ahead.left - ahead.right) < 1e-9);
	const auto toRight = gainsFor(0.0, 10.0, 0.0);
	assert(toRight.right > toRight.left);
	const auto toLeft = gainsFor(0.0, -10.0, 0.0);
	assert(toLeft.left > toLeft.right);

	const auto aheadRotated = gainsFor(std::numbers::pi / 2.0, 10.0, 0.0);
	assert(std::abs(aheadRotated.left - aheadRotated.right) < 1e-9);
	const auto rightRotated = gainsFor(std::numbers::pi / 2.0, 0.0, -10.0);
	assert(rightRotated.right > rightRotated.left);
	const auto leftRotated = gainsFor(std::numbers::pi / 2.0, 0.0, 10.0);
	assert(leftRotated.left > leftRotated.right);
	assert(std::abs(SpeakerPan(0.0, 0.0, 0.0, 10.0, 10.0) - std::sin(std::numbers::pi / 4.0)) < 1e-9);
	assert(std::abs(SpeakerPan(5.0, 5.0, 0.0, 15.0, 5.0) - 1.0) < 1e-9);
	assert(std::abs(SpeakerPan(5.0, 5.0, 0.0, 5.0, 5.0)) < 1e-9);

	const auto atListener = gainsFor(1.0, 0.0, 0.0);
	assert(std::abs(atListener.left - std::sqrt(0.5)) < 1e-9);
	assert(std::abs(atListener.right - std::sqrt(0.5)) < 1e-9);

	for (const double pan : { -1.0, -0.3, 0.0, 0.5, 1.0 }) {
		const auto g = ConstantPowerGains(pan);
		assert(std::abs(g.left * g.left + g.right * g.right - 1.0) < 1e-6);
	}
	const auto arbitrary = gainsFor(0.7, 3.0, -4.0);
	assert(std::abs(arbitrary.left * arbitrary.left + arbitrary.right * arbitrary.right - 1.0) < 1e-6);

	Mixer speakers{};
	Output out{};
	speakers[0].pcm.fill(16384);
	speakers[0].gain = StereoGain{ 1.0, 1.0 };
	MixFrame(speakers, 100, out);
	assert(out[0] == 0.0F);
	assert(out[1] == 0.0F);
	assert(out[2 * 240] == 0.25F);
	assert(out[2 * 240 + 1] == 0.25F);
	assert(out[2 * 479] < 0.5F);
	assert(std::abs(out[2 * 479] - 0.5F * 479.0F / 480.0F) < 1e-6F);
	assert(out[2 * 480] == 0.5F);
	assert(out[2 * 959] == 0.5F);
	MixFrame(speakers, 100, out);
	assert(out[0] == 0.5F);

	Mixer loud{};
	for (auto& speaker : loud) {
		speaker.pcm.fill(32767);
		speaker.gain = StereoGain{ 1.0, 1.0 };
		speaker.volume = 100;
	}
	MixFrame(loud, 100, out);
	MixFrame(loud, 100, out);
	for (const auto v : out) {
		assert(v == 1.0F);
	}
	for (auto& speaker : loud) {
		speaker.pcm.fill(-32768);
	}
	MixFrame(loud, 100, out);
	MixFrame(loud, 100, out);
	for (const auto v : out) {
		assert(v == -1.0F);
	}

	Mixer silent{};
	for (auto& speaker : silent) {
		speaker.gain = StereoGain{ 1.0, 1.0 };
	}
	MixFrame(silent, 100, out);
	for (const auto v : out) {
		assert(v == 0.0F);
	}

	const int masterVolume = 85;
	Mixer scaled{};
	scaled[0].pcm.fill(16384);
	scaled[0].gain = StereoGain{ 1.0, 1.0 };
	scaled[0].volume = 50;
	MixFrame(scaled, masterVolume, out);
	MixFrame(scaled, masterVolume, out);
	assert(std::abs(out[0] - 0.5F * 0.85F * 0.5F) < 1e-6F);
	assert(std::abs(out[1] - 0.5F * 0.85F * 0.5F) < 1e-6F);

	scaled[0].volume = 0;
	MixFrame(scaled, masterVolume, out);
	assert(std::abs(out[0] - 0.5F * 0.85F * 0.5F) < 1e-6F);
	assert(std::abs(out[2 * 240] - 0.5F * 0.85F * 0.5F * 0.5F) < 1e-6F);
	MixFrame(scaled, masterVolume, out);
	assert(out[0] == 0.0F);
	assert(out[1] == 0.0F);

	const auto settledFirstSample = [](int a_masterVolume, int a_volume) {
		Mixer clamped{};
		clamped[0].pcm.fill(16384);
		clamped[0].gain = StereoGain{ 0.5, 0.5 };
		clamped[0].volume = a_volume;
		Output settled{};
		MixFrame(clamped, a_masterVolume, settled);
		MixFrame(clamped, a_masterVolume, settled);
		return settled[0];
	};
	assert(settledFirstSample(100, 150) == settledFirstSample(100, 100));
	assert(settledFirstSample(100, -5) == settledFirstSample(100, 0));
	assert(settledFirstSample(150, 100) == settledFirstSample(100, 100));
	assert(settledFirstSample(-5, -5) == 0.0F);

	const auto skipsBadGain = [](StereoGain a_bad) {
		Mixer speakers{};
		Output speakerOut{};
		speakers[0].pcm.fill(16384);
		speakers[0].gain = a_bad;
		speakers[1].pcm.fill(16384);
		speakers[1].gain = StereoGain{ 1.0, 1.0 };
		MixFrame(speakers, 100, speakerOut);
		MixFrame(speakers, 100, speakerOut);
		for (const auto v : speakerOut) {
			assert(std::isfinite(v));
		}
		assert(speakerOut[0] == 0.5F);
		assert(speakerOut[1] == 0.5F);
		return speakerOut;
	};
	for (const auto v : skipsBadGain(StereoGain{ std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() })) {
		assert(v >= -1.0F && v <= 1.0F);
	}
	skipsBadGain(StereoGain{ std::numeric_limits<double>::quiet_NaN(), 1.0 });
	skipsBadGain(StereoGain{ 1.0, std::numeric_limits<double>::quiet_NaN() });
	skipsBadGain(StereoGain{ std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() });

	Mixer recovered{};
	recovered[0].pcm.fill(16384);
	recovered[0].gain = StereoGain{ std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
	MixFrame(recovered, 100, out);
	recovered[0].gain = StereoGain{ 1.0, 1.0 };
	MixFrame(recovered, 100, out);
	for (const auto v : out) {
		assert(std::isfinite(v));
	}
	assert(out[0] == 0.0F);
	assert(out[2 * 480] == 0.5F);

	Mixer routed{};
	routed[0].pcm.fill(16384);
	routed[0].gain = StereoGain{ 0.0, 1.0 };
	MixFrame(routed, 100, out);
	MixFrame(routed, 100, out);
	assert(out[2 * 480] == 0.0F);
	assert(out[2 * 480 + 1] == 0.5F);
	routed[0].gain = StereoGain{ 1.0, 0.0 };
	MixFrame(routed, 100, out);
	assert(out[0] == 0.0F);
	assert(out[1] == 0.5F);
	MixFrame(routed, 100, out);
	assert(out[2 * 480] == 0.5F);
	assert(out[2 * 480 + 1] == 0.0F);

	Mixer summed{};
	for (auto& speaker : summed) {
		speaker.pcm.fill(4096);
		speaker.gain = StereoGain{ 1.0, 1.0 };
	}
	MixFrame(summed, 100, out);
	MixFrame(summed, 100, out);
	assert(std::abs(out[0] - 0.5F) < 1e-6F);

	Mixer overRange{};
	overRange[0].gain = StereoGain{ 1e39, 1e39 };
	MixFrame(overRange, 100, out);
	MixFrame(overRange, 100, out);
	for (const auto v : out) {
		assert(std::isfinite(v));
	}
	overRange[0].pcm.fill(16384);
	MixFrame(overRange, 100, out);
	assert(out[0] == 0.5F);
	assert(out[2 * 959] == 0.5F);

	Mixer settledThenSkipped{};
	settledThenSkipped[0].pcm.fill(16384);
	settledThenSkipped[0].gain = StereoGain{ 1.0, 1.0 };
	MixFrame(settledThenSkipped, 100, out);
	MixFrame(settledThenSkipped, 100, out);
	settledThenSkipped[0].gain = StereoGain{ std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
	MixFrame(settledThenSkipped, 100, out);
	settledThenSkipped[0].gain = StereoGain{ 1.0, 1.0 };
	MixFrame(settledThenSkipped, 100, out);
	assert(out[0] == 0.5F);
	assert(out[2 * 479 + 1] == 0.5F);

	return 0;
}
