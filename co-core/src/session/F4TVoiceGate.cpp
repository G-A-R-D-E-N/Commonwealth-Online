#include "F4TVoiceGate.h"
#include "F4TVoicePtt.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace F4T::Voice
{
	namespace
	{
		constexpr double FullScaleAmplitude = 32768.0;
		constexpr double TimeToleranceSeconds = 1e-9;
		constexpr double TimeToleranceRelative = 4.0 * std::numeric_limits<double>::epsilon();

		[[nodiscard]] double AmplitudeToDbfs(double a_amplitude) noexcept
		{
			return 20.0 * std::log10(a_amplitude / FullScaleAmplitude);
		}

		[[nodiscard]] Tier TierFromKeys(const PttInput& a_input) noexcept
		{
			if (a_input.shoutDown) {
				return Tier::Shout;
			}
			return a_input.whisperDown ? Tier::Whisper : Tier::Normal;
		}

		[[nodiscard]] double TimeTolerance(double a_seconds) noexcept
		{
			return std::max(TimeToleranceSeconds, TimeToleranceRelative * std::abs(a_seconds));
		}
	}

	FrameLevel FrameLevelDbfs(std::span<const std::int16_t, VoiceFrameSamples> a_samples) noexcept
	{
		double sumSquares = 0.0;
		int peak = 0;
		for (const auto sample : a_samples) {
			const int value = sample;
			sumSquares += static_cast<double>(value) * static_cast<double>(value);
			peak = std::max(peak, std::abs(value));
		}
		const double rms = std::sqrt(sumSquares / static_cast<double>(VoiceFrameSamples));
		return FrameLevel{ AmplitudeToDbfs(rms), AmplitudeToDbfs(static_cast<double>(peak)) };
	}

	OpenMicGate::OpenMicGate(double a_thresholdDbfs) noexcept : m_thresholdDbfs(a_thresholdDbfs) {}

	GateTick OpenMicGate::Tick(std::span<const std::int16_t, VoiceFrameSamples> a_frame) noexcept
	{
		const bool loud = FrameLevelDbfs(a_frame).rmsDbfs >= m_thresholdDbfs;
		m_head = (m_head + 1) % m_frames.size();
		std::copy(a_frame.begin(), a_frame.end(), m_frames[m_head].begin());
		m_quietTicks = loud ? 0 : m_quietTicks + 1;

		GateTick tick{};
		tick.send = m_quietTicks <= GateHangoverTicks;
		if (tick.send) {
			tick.burstStart = !m_inBurst;
			tick.preRoll = tick.burstStart ? std::min(m_unsentRun, GatePreRollFrames) : 0;
			tick.burstEnd = m_quietTicks == GateHangoverTicks;
		}
		m_inBurst = tick.send && !tick.burstEnd;
		m_unsentRun = tick.send ? 0 : m_unsentRun + 1;
		return tick;
	}

	const std::int16_t* OpenMicGate::Frame(std::size_t a_ticksBack) const noexcept
	{
		return m_frames[(m_head + m_frames.size() - a_ticksBack) % m_frames.size()].data();
	}

	MicHealth::MicHealth(double a_startSeconds) noexcept : m_lastCallbackSeconds(a_startSeconds) {}

	void MicHealth::OnCallback(double a_nowSeconds, double a_peakDbfs, bool a_pttHeld) noexcept
	{
		if (a_nowSeconds - m_lastCallbackSeconds >= MicCallbackTimeoutSeconds - TimeTolerance(a_nowSeconds)) {
			m_quiet = false;
		}
		m_lastCallbackSeconds = a_nowSeconds;
		m_deviceError = false;
		m_reopening = false;
		if (!a_pttHeld || a_peakDbfs >= MicSilentPeakDbfs) {
			m_quiet = false;
			return;
		}
		if (!m_quiet) {
			m_quiet = true;
			m_quietSinceSeconds = a_nowSeconds;
		}
	}

	void MicHealth::OnDeviceError() noexcept
	{
		m_deviceError = true;
		m_quiet = false;
	}

	void MicHealth::OnReopenAttempt() noexcept
	{
		m_reopening = true;
	}

	bool MicHealth::Failed(double a_nowSeconds) const noexcept
	{
		return m_deviceError || a_nowSeconds - m_lastCallbackSeconds >= MicCallbackTimeoutSeconds - TimeTolerance(a_nowSeconds);
	}

	bool MicHealth::SilentHint(double a_nowSeconds) const noexcept
	{
		return m_quiet && a_nowSeconds - m_quietSinceSeconds >= MicSilentHoldSeconds - TimeTolerance(a_nowSeconds);
	}

	bool MicHealth::Recovering() const noexcept
	{
		return m_reopening;
	}

	bool MicHealth::Idle(double a_nowSeconds, bool a_transmitting) const noexcept
	{
		return !a_transmitting && !Failed(a_nowSeconds);
	}

	void PttState::Poll(const PttInput& a_input, double a_nowSeconds) noexcept
	{
		const bool held = a_input.keyDown && a_input.inputAllowed;
		m_tier = TierFromKeys(a_input);
		if (!a_input.inputAllowed) {
			m_tailActive = false;
		} else if (!held && m_held) {
			m_tailActive = true;
			m_releasedAtSeconds = a_nowSeconds;
		}
		m_held = held;
	}

	PttTick PttState::Tick(double a_nowSeconds) noexcept
	{
		if (m_tailActive && a_nowSeconds - m_releasedAtSeconds >= PttTailSeconds - TimeTolerance(a_nowSeconds)) {
			m_tailActive = false;
		}
		const bool transmit = m_held || m_tailActive;
		const PttTick tick{ m_pendingTransmit, m_pendingTier, m_pendingBurstStart, m_pendingTransmit && !transmit };
		m_pendingBurstStart = transmit && !m_pendingTransmit;
		m_pendingTransmit = transmit;
		m_pendingTier = m_tier;
		return tick;
	}
}
