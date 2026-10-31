#pragma once

#include "F4TVoiceCodec.h"
#include "F4TVoiceJitter.h"
#include "F4TVoiceMixer.h"
#include "F4TVoiceRuntimeTypes.h"
#include "F4TVoiceWasapiRender.h"
#include "F4TVoiceWire.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

namespace F4T::Voice
{
	inline constexpr std::size_t ReceiveRingSlots = 256;
	inline constexpr std::size_t ReceiveSpeakerIdCount = 65536;
	inline constexpr std::size_t ReceiveSpeakerWords = ReceiveSpeakerIdCount / 64;
	inline constexpr int ReceiveDefaultVolume = 100;

	struct ReceiveEntry
	{
		std::uint32_t generation{};
		std::uint32_t sequence{};
		double arrivalSeconds{};
		std::uint16_t speakerId{};
		Tier tier{ Tier::Whisper };
		bool burstStart{};
		bool burstEnd{};
		std::size_t size{};
		std::array<std::byte, kVoiceMaxPayloadBytes> payload{};
	};

	class ReceiveRing
	{
	public:
		[[nodiscard]] bool Push(const ReceiveEntry& a_entry) noexcept;
		[[nodiscard]] bool Pop(ReceiveEntry& a_out) noexcept;
		[[nodiscard]] std::uint64_t Dropped() const noexcept;

	private:
		std::array<ReceiveEntry, ReceiveRingSlots> m_entries{};
		std::atomic<std::uint64_t> m_head{};
		std::atomic<std::uint64_t> m_tail{};
		std::atomic<std::uint64_t> m_dropped{};
	};

	struct ReceiveStats
	{
		std::uint64_t played{};
		std::uint64_t recovered{};
		std::uint64_t concealed{};
		std::uint64_t ringDropped{};
	};

	struct ListenerPose
	{
		double x{};
		double y{};
		double z{};
		double angleZ{};
	};

	struct ReceiveSpeakerPose
	{
		std::uint16_t speakerId{};
		double x{};
		double y{};
		double z{};
		bool used{};
	};

	struct ReceivePoses
	{
		double listenerX{};
		double listenerY{};
		double listenerZ{};
		double listenerHeading{};
		std::array<ReceiveSpeakerPose, VoiceSavedSpeakerCount> speakers{};
	};

	class Receiver
	{
	public:
		explicit Receiver(std::function<double()> a_clock);
		~Receiver();
		Receiver(const Receiver&) = delete;
		Receiver& operator=(const Receiver&) = delete;

		[[nodiscard]] bool Open();
		[[nodiscard]] bool Start();
		void Stop();
		void RequestReopen();
		[[nodiscard]] bool OutputFailed() const;

		[[nodiscard]] bool OnPacket(const VoicePacket& a_packet, double a_arrivalSeconds) noexcept;
		void ResetSession() noexcept;
		void SetDeafened(bool a_deafened) noexcept;
		void SetSpeakerMuted(std::uint16_t a_speakerId, bool a_muted);
		void SetSpeakerVolume(std::uint16_t a_speakerId, int a_volume) noexcept;
		void SetMasterVolume(int a_volume) noexcept;
		void SetPositions(const ListenerPose& a_listener, std::span<const SpeakerPosition> a_speakers);
		void RemoveSpeaker(std::uint16_t a_speakerId);
		[[nodiscard]] std::vector<std::uint16_t> Speakers() const;
		[[nodiscard]] ReceiveStats Stats() const noexcept;

		void Fill(float* a_interleavedStereo, std::uint32_t a_frames) noexcept;

	private:
		void Tick() noexcept;
		void Silence() noexcept;
		void AdoptGeneration(std::uint32_t a_generation) noexcept;
		void ResetState() noexcept;
		void RefreshPoses() noexcept;
		void Drain() noexcept;
		void DecodeSlot(std::size_t a_index, const Decision& a_decision) noexcept;
		void PublishSpeaking() noexcept;
		[[nodiscard]] bool IsMuted(std::uint16_t a_speakerId) const noexcept;

		std::function<double()> m_clock;
		WasapiRender m_render;
		ReceiveRing m_ring;
		std::atomic<std::uint32_t> m_generation{};
		std::atomic<bool> m_deafened{};
		std::atomic<int> m_masterVolume{ ReceiveDefaultVolume };
		std::array<std::atomic<std::uint64_t>, ReceiveSpeakerWords> m_muted{};
		std::array<std::atomic<std::uint8_t>, ReceiveSpeakerIdCount> m_volumes{};
		std::atomic<std::uint64_t> m_played{};
		std::atomic<std::uint64_t> m_recovered{};
		std::atomic<std::uint64_t> m_concealed{};

		mutable std::mutex m_poseMutex;
		ReceivePoses m_poses{};
		std::array<std::uint64_t, ReceiveSpeakerWords> m_removals{};

		mutable std::mutex m_speakingMutex;
		std::array<std::uint16_t, VoiceSlotCount> m_speaking{};
		std::size_t m_speakingCount{};

		std::array<Decoder, VoiceSlotCount> m_decoders{};
		VoiceSlots m_slots{};
		std::array<Tier, VoiceSlotCount> m_tiers{};
		std::array<bool, VoiceSlotCount> m_realAudio{};
		ReceivePoses m_activePoses{};
		std::array<MixerSpeaker, MixerSpeakerCount> m_speakers{};
		std::array<float, MixerFrameSamples * 2> m_out{};
		std::size_t m_mixPosition{ MixerFrameSamples };
		std::uint32_t m_seenGeneration{};
	};
}
