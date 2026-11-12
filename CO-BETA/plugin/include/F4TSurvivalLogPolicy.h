#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace F4T::SurvivalLogPolicy
{
	class Throttle
	{
	public:
		explicit Throttle(double a_intervalSeconds) noexcept : _interval(a_intervalSeconds) {}

		[[nodiscard]] bool Due(double a_nowSeconds) noexcept
		{
			if (_last && a_nowSeconds - *_last < _interval) {
				return false;
			}
			_last = a_nowSeconds;
			return true;
		}

		void Reset() noexcept
		{
			_last.reset();
		}

	private:
		double _interval;
		std::optional<double> _last;
	};

	class Latch
	{
	public:
		// Constraint: a_outcome must outlive the latch (string literals only).
			[[nodiscard]] bool Claim(std::uint64_t a_connection, std::uint64_t a_epoch, std::string_view a_outcome = {}) noexcept
		{
			if (_claimed && _connection == a_connection && _epoch == a_epoch && _outcome == a_outcome) {
				return false;
			}
			_claimed = true;
			_connection = a_connection;
			_epoch = a_epoch;
			_outcome = a_outcome;
			return true;
		}

	private:
		bool _claimed{};
		std::uint64_t _connection{};
		std::uint64_t _epoch{};
		std::string_view _outcome{};
	};

	[[nodiscard]] inline std::string_view FlagText(std::optional<bool> a_value) noexcept
	{
		if (!a_value) {
			return "missing";
		}
		return *a_value ? "true" : "false";
	}
}
