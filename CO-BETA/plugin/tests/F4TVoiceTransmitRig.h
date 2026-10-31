#pragma once

#include "F4TVoiceCodec.h"
#include "F4TVoiceDevices.h"
#include "F4TVoiceGate.h"
#include "F4TVoiceTransmit.h"
#include "F4TVoiceWire.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace TransmitRig
{
	using F4T::Voice::DecodeVoicePacket;
	using F4T::Voice::Encoder;
	using F4T::Voice::EncodeResult;
	using F4T::Voice::TransmitConfig;
	using F4T::Voice::Tier;
	using F4T::Voice::TransmitDeps;
	using F4T::Voice::TransmitLevelFloorDbfs;
	using F4T::Voice::Transmitter;
	using F4T::Voice::VoiceDecodeResult;
	using F4T::Voice::VoiceDirection;
	using F4T::Voice::VoiceFrameSamples;
	using F4T::Voice::VoiceHeader;
	using F4T::Voice::VoicePacket;
	using F4T::Voice::VoiceTickSeconds;
	using Frame = std::array<std::int16_t, VoiceFrameSamples>;

	inline constexpr std::uint8_t kPttKey = 0x42;
	inline constexpr std::uint8_t kWhisperKey = 0x43;
	inline constexpr std::uint8_t kShoutKey = 0x44;
	inline constexpr std::int16_t kTone = 8000;
	inline constexpr std::int16_t kSilence = 0;

	struct Rig
	{
		double now{};
		std::map<std::uint8_t, bool> keys{};
		TransmitConfig config{};
		std::vector<VoiceHeader> packets{};
		std::vector<std::vector<std::byte>> payloads{};
		bool sendOk{ true };
		Transmitter tx;

		Rig() : tx(TransmitDeps{
					[this](std::span<const std::byte> a_datagram) {
						if (!sendOk) {
							return false;
						}
						VoicePacket packet{};
						const VoiceDecodeResult result = DecodeVoicePacket(a_datagram, VoiceDirection::ClientToServer, packet);
						assert(result == VoiceDecodeResult::Ok);
						packets.push_back(packet.header);
						payloads.emplace_back(packet.payload.begin(), packet.payload.end());
						return true;
					},
					[this](std::uint8_t a_vk) { return keys[a_vk]; },
					[this] { return now; } })
		{
			config.pttVk = kPttKey;
			config.whisperVk = kWhisperKey;
			config.shoutVk = kShoutKey;
			tx.Configure(config);
			tx.SetCanSend(true);
		}
	};

	inline Frame SineFrame(std::int16_t a_amplitude)
	{
		Frame pcm{};
		for (std::size_t s = 0; s < pcm.size(); ++s) {
			pcm[s] = static_cast<std::int16_t>(a_amplitude * std::sin(0.1 * static_cast<double>(s)));
		}
		return pcm;
	}

	inline Frame PeakFrame(std::int16_t a_peak)
	{
		Frame pcm{};
		pcm[0] = a_peak;
		return pcm;
	}

	inline void RunFrame(Rig& a_rig, const Frame& a_pcm)
	{
		a_rig.tx.OnSamples(a_pcm.data(), static_cast<std::uint32_t>(a_pcm.size()));
		a_rig.tx.OnTick();
		a_rig.now += VoiceTickSeconds;
	}

	inline void Run(Rig& a_rig, int a_frames, std::int16_t a_amplitude)
	{
		const Frame pcm = SineFrame(a_amplitude);
		for (int frame = 0; frame < a_frames; ++frame) {
			RunFrame(a_rig, pcm);
		}
	}

	void RunDeviceTests();
}
