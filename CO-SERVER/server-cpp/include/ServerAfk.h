#pragma once

#include "F4TConnectionPolicy.h"

#include <cstdint>
#include <optional>

namespace CommonwealthOnline::Server::Afk
{
	struct Exemption
	{
		bool reported{};
		double reportedAtUnixSeconds{};
		double streakStartUnixSeconds{};
	};

	inline constexpr double kMaximumExemptionSeconds = 600.0;

	// constraint: an exemption only lasts while the client keeps reporting it.
	[[nodiscard]] inline bool ExemptionLive(const Exemption& a_exemption, double a_nowUnixSeconds) noexcept
	{
		return a_exemption.reported &&
		       a_nowUnixSeconds >= a_exemption.reportedAtUnixSeconds &&
		       a_nowUnixSeconds - a_exemption.reportedAtUnixSeconds <=
			       F4T::ConnectionPolicy::DefaultAfkExemptionFreshnessSeconds &&
		       a_nowUnixSeconds - a_exemption.streakStartUnixSeconds <= kMaximumExemptionSeconds;
	}

	[[nodiscard]] inline Exemption NextExemption(const Exemption& a_previous, bool a_exempt, double a_nowUnixSeconds) noexcept
	{
		if (!a_exempt) {
			return Exemption{};
		}
		const bool continuing = a_previous.reported &&
		                        a_previous.streakStartUnixSeconds != 0.0 &&
		                        a_nowUnixSeconds - a_previous.reportedAtUnixSeconds <=
			                        F4T::ConnectionPolicy::DefaultAfkExemptionFreshnessSeconds;
		return Exemption{ true, a_nowUnixSeconds, continuing ? a_previous.streakStartUnixSeconds : a_nowUnixSeconds };
	}

	enum class Verdict : std::uint8_t
	{
		none,
		exempt,
		kick
	};

	[[nodiscard]] inline Verdict Evaluate(
		std::optional<double>& a_lastActivityAtUnixSeconds,
		const Exemption& a_exemption,
		double a_nowUnixSeconds,
		bool a_gameplayActive,
		bool a_steamHost) noexcept
	{
		if (!a_gameplayActive || a_steamHost) {
			return Verdict::none;
		}
		if (ExemptionLive(a_exemption, a_nowUnixSeconds)) {
			a_lastActivityAtUnixSeconds = a_nowUnixSeconds;
			return Verdict::exempt;
		}
		if (F4T::ConnectionPolicy::AfkExpired(
				a_lastActivityAtUnixSeconds,
				a_nowUnixSeconds,
				a_gameplayActive)) {
			return Verdict::kick;
		}
		return Verdict::none;
	}

	// constraint: the notice must reach the transport before the close.
	template <typename Notify, typename Disconnect>
	inline void EndSessionAfterNotifying(std::uint32_t a_connectionId, Notify&& a_notify, Disconnect&& a_disconnect)
	{
		a_notify(a_connectionId);
		a_disconnect(a_connectionId);
	}
}
