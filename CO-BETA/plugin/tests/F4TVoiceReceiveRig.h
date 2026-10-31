#pragma once

#include "F4TVoiceCodec.h"
#include "F4TVoiceJitter.h"
#include "F4TVoiceMixer.h"
#include "F4TVoiceReceive.h"
#include "F4TVoiceWire.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <span>
#include <vector>

namespace ReceiveRig
{
	using F4T::Voice::Encoder;
	using F4T::Voice::EncodeResult;
	using F4T::Voice::ListenerPose;
	using F4T::Voice::MixerFrameSamples;
	using F4T::Voice::Receiver;
	using F4T::Voice::ReceiveEntry;
	using F4T::Voice::ReceiveRing;
	using F4T::Voice::ReceiveRingSlots;
	using F4T::Voice::ReceiveStats;
	using F4T::Voice::SpeakerPosition;
	using F4T::Voice::Tier;
	using F4T::Voice::VoicePacket;
	using Frame = std::array<std::int16_t, MixerFrameSamples>;

	inline constexpr std::uint32_t kTickFrames = MixerFrameSamples;
	inline constexpr double kTickSeconds = 0.02;
	inline constexpr std::int16_t kLoud = 30000;
	inline constexpr std::size_t kRampEnd = 2U * (MixerFrameSamples / 2U);

	inline void Require(bool a_condition, std::source_location a_where = std::source_location::current())
	{
		if (!a_condition) {
			std::printf("requirement failed at %s:%u\n", a_where.file_name(), a_where.line());
			std::exit(EXIT_FAILURE);
		}
	}

	struct Talker
	{
		Talker()
		{
			Require(encoder.Open());
		}

		Encoder encoder{};
	};

	struct Bench
	{
		Bench() : receiver([this] { return now; })
		{
			Require(receiver.Open());
		}

		Bench(const Bench&) = delete;
		Bench& operator=(const Bench&) = delete;

		double now{};
		std::uint64_t frames{};
		Receiver receiver;
	};

	inline std::vector<std::byte> Encode(Encoder& a_encoder, const Frame& a_pcm)
	{
		std::array<std::uint8_t, F4T::Voice::kMaxEncodedBytes> out{};
		const EncodeResult result = a_encoder.Encode(a_pcm.data(), out.data());
		Require(result.sendable);
		const auto bytes = std::as_bytes(std::span<const std::uint8_t>(out.data(), static_cast<std::size_t>(result.bytes)));
		return std::vector<std::byte>(bytes.begin(), bytes.end());
	}

	inline Frame Tone(std::int16_t a_amplitude, std::size_t a_firstSample)
	{
		Frame pcm{};
		for (std::size_t s = 0; s < pcm.size(); ++s) {
			pcm[s] = static_cast<std::int16_t>(a_amplitude * std::sin(0.05 * static_cast<double>(a_firstSample + s)));
		}
		return pcm;
	}

	inline VoicePacket MakePacket(
		std::uint16_t a_speakerId,
		std::uint32_t a_sequence,
		std::span<const std::byte> a_payload,
		bool a_burstStart = false,
		bool a_burstEnd = false)
	{
		VoicePacket packet{};
		packet.header.speakerId = a_speakerId;
		packet.header.sequence = a_sequence;
		packet.header.tier = Tier::Normal;
		packet.header.burstStart = a_burstStart;
		packet.header.burstEnd = a_burstEnd;
		packet.payload = a_payload;
		return packet;
	}

	inline bool Send(
		Bench& a_bench,
		std::uint16_t a_speakerId,
		std::uint32_t a_sequence,
		const std::vector<std::byte>& a_payload,
		bool a_burstStart = false,
		bool a_burstEnd = false)
	{
		return a_bench.receiver.OnPacket(MakePacket(a_speakerId, a_sequence, a_payload, a_burstStart, a_burstEnd), a_bench.now);
	}

	inline std::vector<float> Pull(Bench& a_bench, std::uint32_t a_frames = kTickFrames)
	{
		a_bench.now = std::ceil(static_cast<double>(a_bench.frames) / static_cast<double>(kTickFrames)) * kTickSeconds;
		std::vector<float> out(static_cast<std::size_t>(a_frames) * 2U, 1.0F);
		a_bench.receiver.Fill(out.data(), a_frames);
		a_bench.frames += a_frames;
		return out;
	}

	inline float MaxAbs(const std::vector<float>& a_interleaved, std::size_t a_fromIndex, std::size_t a_toIndex)
	{
		float peak = 0.0F;
		for (std::size_t i = a_fromIndex; i < a_toIndex; ++i) {
			peak = (std::max)(peak, std::fabs(a_interleaved[i]));
		}
		return peak;
	}

	inline float MaxAbs(const std::vector<float>& a_interleaved)
	{
		return MaxAbs(a_interleaved, 0, a_interleaved.size());
	}

	inline float ChannelPeak(const std::vector<float>& a_interleaved, std::size_t a_channel)
	{
		float peak = 0.0F;
		for (std::size_t i = a_channel; i < a_interleaved.size(); i += 2U) {
			peak = (std::max)(peak, std::fabs(a_interleaved[i]));
		}
		return peak;
	}

	inline float LargestStep(const std::vector<float>& a_interleaved)
	{
		float largest = 0.0F;
		for (std::size_t i = 2U; i < a_interleaved.size(); i += 2U) {
			largest = (std::max)(largest, std::fabs(a_interleaved[i] - a_interleaved[i - 2U]));
		}
		return largest;
	}

	inline float BoundaryStep(const std::vector<float>& a_before, const std::vector<float>& a_after)
	{
		return std::fabs(a_after[0] - a_before[a_before.size() - 2U]);
	}

	inline bool Contains(const std::vector<std::uint16_t>& a_list, std::uint16_t a_speakerId)
	{
		return std::find(a_list.begin(), a_list.end(), a_speakerId) != a_list.end();
	}

	void RunSpeakerTests();
	void RunDecoderTests();
}
