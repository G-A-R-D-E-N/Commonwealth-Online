#pragma once

#include "F4TSnapshotInterpolation.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace F4T::ProxyMovementDiagnostics
{
	using RenderStatus = F4T::Replication::SnapshotInterpolationBuffer<F4T::Replication::MovementSnapshot>::Status;
	using Clock = std::chrono::steady_clock;

	inline constexpr double kWindowSeconds = 1.0;
	inline constexpr std::size_t kRenderStatusCount = 5;
	inline constexpr std::size_t kSnapReasonCount = 4;

	enum class SnapReason : std::uint8_t
	{
		kMovementType,
		kIdle,
		kFirstSample,
		kDistance
	};

	struct RunningStat
	{
		std::uint64_t count{};
		double sum{};
		double sumSquares{};
		double maximum{};

		void Add(double a_value) noexcept;
		[[nodiscard]] double Mean() const noexcept;
		[[nodiscard]] double StdDev() const noexcept;
	};

	struct Window
	{
		Clock::time_point windowStart{};
		bool started{ false };
		double durationSeconds{};
		Clock::time_point lastArrival{};
		bool hasLastArrival{ false };
		RunningStat arrivalIntervalSeconds{};
		RunningStat receivedSpeed{};
		RunningStat renderedSpeed{};
		RunningStat bufferDepth{};
		RunningStat renderDelaySeconds{};
		std::uint64_t packets{};
		std::uint64_t sent{};
		std::uint64_t renders{};
		std::array<std::uint64_t, kRenderStatusCount> renderStatus{};
		std::array<std::uint64_t, kSnapReasonCount> snaps{};
	};

	void NotePacket(Window& a_window, Clock::time_point a_arrival, double a_receivedSpeed) noexcept;
	void NoteSend(Window& a_window) noexcept;
	void NoteRender(
		Window& a_window,
		RenderStatus a_status,
		double a_renderedSpeed,
		std::size_t a_bufferDepth,
		double a_delaySeconds) noexcept;
	void NoteSnap(Window& a_window, SnapReason a_reason) noexcept;
	[[nodiscard]] double Fraction(const Window& a_window, RenderStatus a_status) noexcept;
	[[nodiscard]] std::optional<Window> TakeIfDue(Window& a_window, Clock::time_point a_now);
	[[nodiscard]] std::string FormatProxyWindow(std::uint32_t a_playerId, const Window& a_window);
	[[nodiscard]] std::string FormatSendWindow(const Window& a_window);
}
