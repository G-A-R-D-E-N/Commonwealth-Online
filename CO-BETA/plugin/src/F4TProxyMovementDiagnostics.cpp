#include "F4TProxyMovementDiagnostics.h"

#include <cmath>
#include <format>
#include <numeric>

namespace F4T::ProxyMovementDiagnostics
{
	namespace
	{
		std::uint64_t SnapCount(const Window& a_window, SnapReason a_reason) noexcept
		{
			return a_window.snaps[static_cast<std::size_t>(a_reason)];
		}
	}

	void RunningStat::Add(double a_value) noexcept
	{
		if (count == 0 || a_value > maximum) {
			maximum = a_value;
		}
		++count;
		sum += a_value;
		sumSquares += a_value * a_value;
	}

	double RunningStat::Mean() const noexcept
	{
		return count == 0 ? 0.0 : sum / static_cast<double>(count);
	}

	double RunningStat::StdDev() const noexcept
	{
		if (count == 0) {
			return 0.0;
		}
		const double mean = Mean();
		const double variance = sumSquares / static_cast<double>(count) - mean * mean;
		return variance > 0.0 ? std::sqrt(variance) : 0.0;
	}

	void NotePacket(Window& a_window, Clock::time_point a_arrival, double a_receivedSpeed) noexcept
	{
		if (a_arrival == Clock::time_point{}) {
			return;
		}
		if (a_window.hasLastArrival) {
			if (a_arrival == a_window.lastArrival) {
				return;
			}
			a_window.arrivalIntervalSeconds.Add(std::chrono::duration<double>(a_arrival - a_window.lastArrival).count());
		}
		a_window.lastArrival = a_arrival;
		a_window.hasLastArrival = true;
		++a_window.packets;
		a_window.receivedSpeed.Add(a_receivedSpeed);
	}

	void NoteSend(Window& a_window) noexcept
	{
		++a_window.sent;
	}

	void NoteRender(
		Window& a_window,
		RenderStatus a_status,
		double a_renderedSpeed,
		std::size_t a_bufferDepth,
		double a_delaySeconds) noexcept
	{
		++a_window.renders;
		++a_window.renderStatus[static_cast<std::size_t>(a_status)];
		a_window.renderedSpeed.Add(a_renderedSpeed);
		a_window.bufferDepth.Add(static_cast<double>(a_bufferDepth));
		a_window.renderDelaySeconds.Add(a_delaySeconds);
	}

	void NoteSnap(Window& a_window, SnapReason a_reason) noexcept
	{
		++a_window.snaps[static_cast<std::size_t>(a_reason)];
	}

	double Fraction(const Window& a_window, RenderStatus a_status) noexcept
	{
		if (a_window.renders == 0) {
			return 0.0;
		}
		return static_cast<double>(a_window.renderStatus[static_cast<std::size_t>(a_status)]) /
			static_cast<double>(a_window.renders);
	}

	std::optional<Window> TakeIfDue(Window& a_window, Clock::time_point a_now)
	{
		if (!a_window.started) {
			a_window.started = true;
			a_window.windowStart = a_now;
			return std::nullopt;
		}
		const double elapsed = std::chrono::duration<double>(a_now - a_window.windowStart).count();
		if (elapsed < kWindowSeconds) {
			return std::nullopt;
		}

		Window closed = a_window;
		closed.durationSeconds = elapsed;
		const auto lastArrival = a_window.lastArrival;
		const auto hasLastArrival = a_window.hasLastArrival;
		a_window = Window{};
		a_window.started = true;
		a_window.windowStart = a_now;
		a_window.lastArrival = lastArrival;
		a_window.hasLastArrival = hasLastArrival;
		return closed;
	}

	std::string FormatProxyWindow(std::uint32_t a_playerId, const Window& a_window)
	{
		const double seconds = a_window.durationSeconds;
		const auto& intervals = a_window.arrivalIntervalSeconds;
		const auto totalSnaps = std::accumulate(a_window.snaps.begin(), a_window.snaps.end(), std::uint64_t{ 0 });
		return std::format(
			"Proxy movement window: playerId={}, seconds={:.2f}, packetsPerSecond={:.1f}, arrivalIntervalMs(mean={:.1f}, max={:.1f}, stddev={:.1f}), receivedSpeed={:.1f}, renderedSpeed={:.1f}, adaptiveDelayMs={:.1f}, bufferDepth={:.1f}, renderFrames={}, interpolating={:.3f}, extrapolating={:.3f}, holdingEnd={:.3f}, holdingStart={:.3f}, snaps={} (movementType={}, idle={}, firstSample={}, distance={}).",
			a_playerId,
			seconds,
			static_cast<double>(a_window.packets) / seconds,
			intervals.Mean() * 1000.0,
			intervals.maximum * 1000.0,
			intervals.StdDev() * 1000.0,
			a_window.receivedSpeed.Mean(),
			a_window.renderedSpeed.Mean(),
			a_window.renderDelaySeconds.Mean() * 1000.0,
			a_window.bufferDepth.Mean(),
			a_window.renders,
			Fraction(a_window, RenderStatus::Interpolating),
			Fraction(a_window, RenderStatus::Extrapolating),
			Fraction(a_window, RenderStatus::HoldingEnd),
			Fraction(a_window, RenderStatus::HoldingStart),
			totalSnaps,
			SnapCount(a_window, SnapReason::kMovementType),
			SnapCount(a_window, SnapReason::kIdle),
			SnapCount(a_window, SnapReason::kFirstSample),
			SnapCount(a_window, SnapReason::kDistance));
	}

	std::string FormatSendWindow(const Window& a_window)
	{
		const double seconds = a_window.durationSeconds;
		return std::format(
			"Local transform send window: seconds={:.2f}, transformsSentPerSecond={:.1f}.",
			seconds,
			static_cast<double>(a_window.sent) / seconds);
	}
}
