#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace F4T::Replication
{
	class SenderClock
	{
	public:
		static constexpr double kOffsetRelaxPerSecond = 0.002;
		static constexpr double kResetSeconds = 1.0;
		static constexpr double kMaxSenderStepSeconds = 5.0;

		enum class Result : std::uint8_t
		{
			Unavailable,
			Stale,
			Reset,
			Mapped
		};

		struct Mapping
		{
			Result result{ Result::Unavailable };
			double mappedSeconds{ 0.0 };
			double lateSeconds{ 0.0 };
			double senderIntervalSeconds{ 0.0 };
		};

		void Reset() noexcept { *this = SenderClock{}; }

		[[nodiscard]] bool Established() const noexcept { return m_established; }

		[[nodiscard]] Mapping Observe(double a_senderSeconds, double a_arrivalSeconds) noexcept
		{
			if (!std::isfinite(a_senderSeconds) || !(a_senderSeconds > 0.0) || !std::isfinite(a_arrivalSeconds)) {
				return Mapping{};
			}
			if (!m_established) {
				return Establish(a_senderSeconds, a_arrivalSeconds);
			}
			if (a_senderSeconds <= m_lastSender) {
				if (m_lastSender - a_senderSeconds > kResetSeconds) {
					return Establish(a_senderSeconds, a_arrivalSeconds);
				}
				return Mapping{ Result::Stale, 0.0, 0.0, 0.0 };
			}

			const double raw = a_arrivalSeconds - a_senderSeconds;
			const double relaxed = m_offset + kOffsetRelaxPerSecond * (a_arrivalSeconds - m_lastArrival);
			if (a_senderSeconds - m_lastSender > kMaxSenderStepSeconds || std::fabs(raw - relaxed) > kResetSeconds) {
				return Establish(a_senderSeconds, a_arrivalSeconds);
			}

			m_offset = std::min(relaxed, raw);
			const double interval = a_senderSeconds - m_lastSender;
			m_lastSender = a_senderSeconds;
			m_lastArrival = a_arrivalSeconds;
			const double mapped = a_senderSeconds + m_offset;
			return Mapping{ Result::Mapped, mapped, std::max(0.0, a_arrivalSeconds - mapped), interval };
		}

	private:
		Mapping Establish(double a_senderSeconds, double a_arrivalSeconds) noexcept
		{
			m_established = true;
			m_offset = a_arrivalSeconds - a_senderSeconds;
			m_lastSender = a_senderSeconds;
			m_lastArrival = a_arrivalSeconds;
			return Mapping{ Result::Reset, a_arrivalSeconds, 0.0, 0.0 };
		}

		double m_offset{ 0.0 };
		double m_lastSender{ 0.0 };
		double m_lastArrival{ 0.0 };
		bool m_established{ false };
	};
}
