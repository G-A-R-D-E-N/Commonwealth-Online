#pragma once

#include "F4TConnectionPolicy.h"

#include <cstdint>
#include <optional>

namespace CommonwealthOnline::Server::Afk
{
	struct Exemption
	{
		bool reported{};
		double reportedAtMonotonicSeconds{};
		double streakStartMonotonicSeconds{};
	};

	inline constexpr double kMaximumExemptionSeconds = 600.0;

	// constraint: an exemption only lasts while the client keeps reporting it.
	[[nodiscard]] inline bool ExemptionLive(const Exemption& a_exemption, double a_nowSeconds) noexcept
	{
		return a_exemption.reported &&
		       a_nowSeconds >= a_exemption.reportedAtMonotonicSeconds &&
		       a_nowSeconds - a_exemption.reportedAtMonotonicSeconds <=
			       F4T::ConnectionPolicy::DefaultAfkExemptionFreshnessSeconds &&
		       a_nowSeconds - a_exemption.streakStartMonotonicSeconds <= kMaximumExemptionSeconds;
	}

	[[nodiscard]] inline Exemption NextExemption(const Exemption& a_previous, bool a_exempt, double a_nowSeconds) noexcept
	{
		if (!a_exempt) {
			return Exemption{};
		}
		const bool continuing = a_previous.reported &&
		                        a_previous.streakStartMonotonicSeconds != 0.0 &&
		                        a_nowSeconds - a_previous.reportedAtMonotonicSeconds <=
			                        F4T::ConnectionPolicy::DefaultAfkExemptionFreshnessSeconds;
		return Exemption{ true, a_nowSeconds, continuing ? a_previous.streakStartMonotonicSeconds : a_nowSeconds };
	}

	enum class Verdict : std::uint8_t
	{
		none,
		exempt,
		kick
	};

	[[nodiscard]] inline Verdict Evaluate(
		std::optional<double>& a_lastActivityAtSeconds,
		const Exemption& a_exemption,
		double a_nowSeconds,
		bool a_gameplayActive,
		bool a_steamHost) noexcept
	{
		if (!a_gameplayActive || a_steamHost) {
			return Verdict::none;
		}
		if (ExemptionLive(a_exemption, a_nowSeconds)) {
			a_lastActivityAtSeconds = a_nowSeconds;
			return Verdict::exempt;
		}
		if (F4T::ConnectionPolicy::AfkExpired(
				a_lastActivityAtSeconds,
				a_nowSeconds,
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
