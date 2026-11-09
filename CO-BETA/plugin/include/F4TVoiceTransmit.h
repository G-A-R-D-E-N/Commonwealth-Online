#pragma once

#include "F4TVoiceCodec.h"
#include "F4TVoiceGate.h"
#include "F4TVoicePtt.h"
#include "F4TVoiceRuntimeTypes.h"
#include "F4TVoiceStatus.h"
#include "F4TVoiceWasapiCapture.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>

namespace F4T::Voice
{
	using SendFn = std::function<bool(std::span<const std::byte>)>;
	using KeyDownFn = std::function<bool(std::uint8_t)>;
	using ClockFn = std::function<double()>;

	struct TransmitDeps
	{
		SendFn send;
		KeyDownFn keyDown;
		ClockFn clock;
	};

	inline constexpr double TransmitLevelFloorDbfs = -120.0;

	struct TransmitStatus
	{
		VoiceStatusInputs inputs{};
		double levelDbfs{};
		std::uint64_t sent{};
		std::uint64_t dropped{};
		std::uint64_t codecFailures{};
	};

	[[nodiscard]] bool IsVoiceKeyDown(std::uint8_t a_vk);

	class Transmitter
	{
	public:
		explicit Transmitter(TransmitDeps a_deps);
		Transmitter(const Transmitter&) = delete;
		Transmitter& operator=(const Transmitter&) = delete;
		~Transmitter();

		[[nodiscard]] bool Start();
		void Stop();
		void Configure(const TransmitConfig& a_config);
		void SetCanSend(bool a_canSend) noexcept;
		void SetInputAllowed(bool a_allowed) noexcept;
		void ResetSession() noexcept;
		void RequestReopen();
		[[nodiscard]] TransmitStatus Status() const;

		void OnSamples(const std::int16_t* a_mono, std::uint32_t a_frames);
		void OnTick();
		void OnDeviceError();
		void OnReopenAttempt();

	private:
		[[nodiscard]] TransmitConfig Snapshot() const;
		void PollPtt(const TransmitConfig& a_config);
		void ResetBurstState(const TransmitConfig& a_config);
		void EnsureEncoder();
		bool BeginBurst();
		void ProcessFrame(const std::int16_t* a_frame);
		void SendFrame(const std::int16_t* a_pcm, std::uint32_t a_sequence, Tier a_tier, bool a_burstEnd);

		TransmitDeps m_deps;
		WasapiCapture m_capture;
		mutable std::mutex m_mutex;
		TransmitConfig m_config{};
		MicHealth m_health;
		std::atomic<bool> m_canSend{};
		std::atomic<bool> m_inputAllowed{ true };
		std::atomic<bool> m_sessionReset{};
		std::atomic<bool> m_transmitting{};
		std::atomic<bool> m_codecFailed{};
		std::atomic<double> m_levelDbfs{ TransmitLevelFloorDbfs };
		std::atomic<std::uint64_t> m_sent{};
		std::atomic<std::uint64_t> m_dropped{};
		std::atomic<std::uint64_t> m_codecFailures{};

		Encoder m_encoder;
		bool m_encoderOpen{};
		double m_gateThresholdDbfs{ DefaultOpenMicThresholdDbfs };
		OpenMicGate m_gate{ DefaultOpenMicThresholdDbfs };
		PttState m_ptt;
		bool m_pttHeld{};
		std::array<std::int16_t, VoiceFrameSamples> m_pending{};
		std::size_t m_pendingCount{};
		std::uint32_t m_sequence{};
		std::array<std::uint32_t, GatePreRollFrames + 1> m_recentSequences{};
		bool m_inBurst{};
		bool m_needBurstStart{};
	};
}
