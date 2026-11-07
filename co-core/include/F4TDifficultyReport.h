#pragma once

#include "F4TDifficultyEnforcement.h"

#include <atomic>
#include <cstdint>
#include <optional>

namespace F4T::DifficultyReport
{
	inline constexpr double kReportIntervalSeconds = 30.0;

	struct Cadence
	{
		std::optional<std::int32_t> lastSent;
		double lastSentAt{};
	};

	[[nodiscard]] inline bool Due(const Cadence& a_cadence, std::int32_t a_current, double a_nowSeconds) noexcept
	{
		if (!a_cadence.lastSent) {
			return true;
		}
		return *a_cadence.lastSent != a_current || a_nowSeconds - a_cadence.lastSentAt >= kReportIntervalSeconds;
	}

	inline std::atomic<std::int32_t> g_localDifficulty{ -1 };
	inline std::atomic<bool> g_serverAcceptsReports{ false };

	inline void SetLocalDifficulty(std::int32_t a_difficulty) noexcept
	{
		g_localDifficulty.store(a_difficulty, std::memory_order_release);
	}

	inline void ClearLocalDifficulty() noexcept
	{
		g_localDifficulty.store(-1, std::memory_order_release);
	}

	[[nodiscard]] inline std::optional<std::int32_t> LocalDifficulty() noexcept
	{
		const auto value = g_localDifficulty.load(std::memory_order_acquire);
		if (!DifficultyEnforcement::IsValidDifficulty(value)) {
			return std::nullopt;
		}
		return value;
	}

	inline void SetServerAcceptsReports(bool a_accepts) noexcept
	{
		g_serverAcceptsReports.store(a_accepts, std::memory_order_release);
	}

	[[nodiscard]] inline bool ServerAcceptsReports() noexcept
	{
		return g_serverAcceptsReports.load(std::memory_order_acquire);
	}
}
