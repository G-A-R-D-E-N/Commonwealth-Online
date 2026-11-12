#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::Voice
{
	inline constexpr std::size_t VoiceFrameSamples = 960;
	inline constexpr double VoiceTickSeconds = 0.020;
	inline constexpr double DefaultOpenMicThresholdDbfs = -45.0;
	inline constexpr std::size_t GateHangoverTicks = 10;
	inline constexpr std::size_t GatePreRollFrames = 3;
	inline constexpr double MicCallbackTimeoutSeconds = 0.5;
	inline constexpr double MicSilentPeakDbfs = -80.0;
	inline constexpr double MicSilentHoldSeconds = 3.0;

	struct FrameLevel
	{
		double rmsDbfs{};
		double peakDbfs{};
	};

	struct GateTick
	{
		bool send{};
		bool burstStart{};
		bool burstEnd{};
		std::size_t preRoll{};
	};

	[[nodiscard]] FrameLevel FrameLevelDbfs(std::span<const std::int16_t, VoiceFrameSamples> a_samples) noexcept;

	class OpenMicGate
	{
	public:
		explicit OpenMicGate(double a_thresholdDbfs) noexcept;

		[[nodiscard]] GateTick Tick(std::span<const std::int16_t, VoiceFrameSamples> a_frame) noexcept;
		[[nodiscard]] const std::int16_t* Frame(std::size_t a_ticksBack) const noexcept;

	private:
		double m_thresholdDbfs;
		std::size_t m_quietTicks{ GateHangoverTicks + 1 };
		std::size_t m_unsentRun{};
		bool m_inBurst{};
		std::size_t m_head{};
		std::array<std::array<std::int16_t, VoiceFrameSamples>, GatePreRollFrames + 1> m_frames{};
	};

	class MicHealth
	{
	public:
		explicit MicHealth(double a_startSeconds) noexcept;

		void OnCallback(double a_nowSeconds, double a_peakDbfs, bool a_pttHeld) noexcept;
		void OnDeviceError() noexcept;
		void OnReopenAttempt() noexcept;
		[[nodiscard]] bool Failed(double a_nowSeconds) const noexcept;
		[[nodiscard]] bool SilentHint(double a_nowSeconds) const noexcept;
		[[nodiscard]] bool Recovering() const noexcept;
		[[nodiscard]] bool Idle(double a_nowSeconds, bool a_transmitting) const noexcept;

	private:
		double m_lastCallbackSeconds;
		double m_quietSinceSeconds{};
		bool m_deviceError{};
		bool m_quiet{};
		bool m_reopening{};
	};
}
