#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace F4T::Replication
{
	struct Vec3
	{
		float x{};
		float y{};
		float z{};

		[[nodiscard]] bool operator==(const Vec3&) const = default;
	};

	[[nodiscard]] inline Vec3 operator+(const Vec3& a_lhs, const Vec3& a_rhs) noexcept
	{
		return Vec3{ a_lhs.x + a_rhs.x, a_lhs.y + a_rhs.y, a_lhs.z + a_rhs.z };
	}

	[[nodiscard]] inline Vec3 operator-(const Vec3& a_lhs, const Vec3& a_rhs) noexcept
	{
		return Vec3{ a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y, a_lhs.z - a_rhs.z };
	}

	[[nodiscard]] inline Vec3 operator*(const Vec3& a_value, float a_scale) noexcept
	{
		return Vec3{ a_value.x * a_scale, a_value.y * a_scale, a_value.z * a_scale };
	}

	[[nodiscard]] inline float Length(const Vec3& a_value) noexcept
	{
		return std::sqrt((a_value.x * a_value.x) + (a_value.y * a_value.y) + (a_value.z * a_value.z));
	}

	[[nodiscard]] inline float Distance(const Vec3& a_lhs, const Vec3& a_rhs) noexcept
	{
		return Length(a_lhs - a_rhs);
	}

	[[nodiscard]] inline bool IsFinite(const Vec3& a_value) noexcept
	{
		return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
	}

	[[nodiscard]] inline float LerpScalar(float a_from, float a_to, float a_alpha) noexcept
	{
		a_alpha = std::clamp(a_alpha, 0.0F, 1.0F);
		return a_from + (a_to - a_from) * a_alpha;
	}

	[[nodiscard]] inline Vec3 LerpVec3(const Vec3& a_from, const Vec3& a_to, float a_alpha) noexcept
	{
		return Vec3{
			LerpScalar(a_from.x, a_to.x, a_alpha),
			LerpScalar(a_from.y, a_to.y, a_alpha),
			LerpScalar(a_from.z, a_to.z, a_alpha)
		};
	}

	[[nodiscard]] inline float LerpAngle(float a_from, float a_to, float a_alpha) noexcept
	{
		constexpr float pi = 3.14159265F;
		constexpr float twoPi = 2.0F * pi;
		const auto normalize = [](float a_angle) {
			a_angle = std::fmod(a_angle, twoPi);
			if (a_angle < 0.0F) {
				a_angle += twoPi;
			}
			return a_angle;
		};
		if (!std::isfinite(a_from) || !std::isfinite(a_to)) {
			if (std::isfinite(a_from)) {
				return normalize(a_from);
			}
			if (std::isfinite(a_to)) {
				return normalize(a_to);
			}
			return 0.0F;
		}
		a_alpha = std::clamp(a_alpha, 0.0F, 1.0F);
		const float from = normalize(a_from);
		const float to = normalize(a_to);
		float delta = to - from;
		if (delta > pi) {
			delta -= twoPi;
		} else if (delta < -pi) {
			delta += twoPi;
		}
		return normalize(from + delta * a_alpha);
	}

	inline constexpr float kSnapshotSnapDistance = 1500.0F;
	inline constexpr double kMaxSnapshotExtrapolationSeconds = 0.150;
	inline constexpr double kMinExtrapolationSpanSeconds = 0.020;
	inline constexpr double kExtrapolationEaseSeconds = 0.250;

	[[nodiscard]] inline bool ExceedsSnapshotSnapDistance(
		const Vec3& a_previous,
		const Vec3& a_next,
		float a_snapDistance = kSnapshotSnapDistance) noexcept
	{
		if (!IsFinite(a_previous) || !IsFinite(a_next)) {
			return false;
		}
		return Distance(a_previous, a_next) > a_snapDistance;
	}

	struct MovementSnapshot
	{
		Vec3 position{};
		float heading{};
		bool moving{ false };

		[[nodiscard]] bool operator==(const MovementSnapshot&) const = default;
	};

	[[nodiscard]] inline MovementSnapshot LerpMovementSnapshot(
		const MovementSnapshot& a_from,
		const MovementSnapshot& a_to,
		float a_alpha) noexcept
	{
		MovementSnapshot out;
		out.position = LerpVec3(a_from.position, a_to.position, a_alpha);
		out.heading = LerpAngle(a_from.heading, a_to.heading, a_alpha);
		out.moving = a_to.moving;
		return out;
	}

	[[nodiscard]] inline MovementSnapshot ExtrapolateMovementSnapshot(
		const MovementSnapshot& a_before,
		const MovementSnapshot& a_after,
		double a_spanSeconds,
		double a_aheadSeconds) noexcept
	{
		if (!a_after.moving || !(a_spanSeconds > 0.0) || !(a_aheadSeconds > 0.0)) {
			return a_after;
		}

		const Vec3 travel = a_after.position - a_before.position;
		const double t = std::min(a_aheadSeconds, kExtrapolationEaseSeconds);
		const float scale = static_cast<float>((t - (t * t) / (2.0 * kExtrapolationEaseSeconds)) / std::max(a_spanSeconds, kMinExtrapolationSpanSeconds));
		Vec3 offset{ travel.x * scale, travel.y * scale, 0.0F };
		const float offsetLength = Length(offset);
		if (!std::isfinite(offsetLength)) {
			return a_after;
		}
		if (offsetLength > kSnapshotSnapDistance) {
			offset = offset * (kSnapshotSnapDistance / offsetLength);
		}

		MovementSnapshot out = a_after;
		out.position = out.position + offset;
		return out;
	}

	[[nodiscard]] inline Vec3 ExtrapolationVelocity(
		const MovementSnapshot& a_before,
		const MovementSnapshot& a_after,
		double a_spanSeconds,
		double a_aheadSeconds) noexcept
	{
		if (!a_after.moving || !(a_spanSeconds > 0.0) || !(a_aheadSeconds > 0.0)) {
			return Vec3{};
		}

		const double t = std::min(a_aheadSeconds, kExtrapolationEaseSeconds);
		const Vec3 travel = a_after.position - a_before.position;
		const float scale = static_cast<float>((1.0 - t / kExtrapolationEaseSeconds) / std::max(a_spanSeconds, kMinExtrapolationSpanSeconds));
		const Vec3 velocity{ travel.x * scale, travel.y * scale, 0.0F };
		return std::isfinite(Length(velocity)) ? velocity : Vec3{};
	}

	template <class T>
	class SnapshotInterpolationBuffer
	{
	public:
		enum class Status : std::uint8_t
		{
			Empty,
			HoldingStart,
			Interpolating,
			HoldingEnd,
			Extrapolating
		};

		struct Sample
		{
			T value{};
			Status status{ Status::Empty };
		};

		explicit SnapshotInterpolationBuffer(double a_renderDelaySeconds, std::size_t a_maxSamples = 32)
		{
			SetRenderDelay(a_renderDelaySeconds);
			m_maxSamples = a_maxSamples < 2 ? std::size_t{ 2 } : a_maxSamples;
		}

		void SetRenderDelay(double a_renderDelaySeconds) noexcept
		{
			m_renderDelay = (std::isfinite(a_renderDelaySeconds) && a_renderDelaySeconds > 0.0) ? a_renderDelaySeconds : 0.0;
		}

		void Clear() noexcept
		{
			m_samples.clear();
			m_watermark = 0.0;
			m_hasWatermark = false;
		}

		void Insert(std::uint32_t a_sequence, double a_timestamp, const T& a_value)
		{
			if (!std::isfinite(a_timestamp)) {
				return;
			}
			if (m_hasWatermark && a_timestamp <= m_watermark) {
				return;
			}
			for (const auto& entry : m_samples) {
				if (entry.sequence == a_sequence || entry.timestamp == a_timestamp) {
					return;
				}
			}
			const auto pos = std::lower_bound(m_samples.begin(), m_samples.end(), a_timestamp,
				[](const Entry& a_entry, double a_key) { return a_entry.timestamp < a_key; });
			m_samples.insert(pos, Entry{ a_sequence, a_timestamp, a_value });
			if (m_samples.size() > m_maxSamples) {
				m_samples.erase(m_samples.begin());
			}
		}

		template <class LerpFn>
		[[nodiscard]] Sample At(double a_nowTime, LerpFn a_lerp) const
		{
			Sample out;
			if (m_samples.empty()) {
				out.status = Status::Empty;
				return out;
			}
			if (!std::isfinite(a_nowTime)) {
				out.value = m_samples.back().value;
				out.status = Status::HoldingEnd;
				return out;
			}
			const double renderTime = a_nowTime - m_renderDelay;
			if (renderTime <= m_samples.front().timestamp) {
				out.value = m_samples.front().value;
				out.status = Status::HoldingStart;
				return out;
			}
			if (renderTime >= m_samples.back().timestamp) {
				out.value = m_samples.back().value;
				out.status = Status::HoldingEnd;
				return out;
			}
			const auto after = std::upper_bound(m_samples.begin(), m_samples.end(), renderTime,
				[](double a_key, const Entry& a_entry) { return a_key < a_entry.timestamp; });
			const auto before = std::prev(after);
			const double span = after->timestamp - before->timestamp;   // > 0: Insert rejects equal timestamps
			const float alpha = std::clamp(static_cast<float>((renderTime - before->timestamp) / span), 0.0F, 1.0F);
			out.value = a_lerp(before->value, after->value, alpha);
			out.status = Status::Interpolating;
			return out;
		}

		template <class LerpFn, class ExtrapolateFn>
		[[nodiscard]] Sample AtExtrapolated(
			double a_nowTime,
			LerpFn a_lerp,
			ExtrapolateFn a_extrapolate,
			double a_maxExtrapolation) const
		{
			if (m_samples.size() >= 2 && a_maxExtrapolation > 0.0 && std::isfinite(a_nowTime)) {
				const auto& after = m_samples.back();
				const double ahead = std::min((a_nowTime - m_renderDelay) - after.timestamp, a_maxExtrapolation);
				if (ahead > 0.0) {
					const auto& before = *std::prev(m_samples.end(), 2);
					Sample out;
					out.value = a_extrapolate(before.value, after.value, after.timestamp - before.timestamp, ahead);
					out.status = Status::Extrapolating;
					return out;
				}
			}
			return At(a_nowTime, a_lerp);
		}

		[[nodiscard]] bool LastPair(T& a_before, T& a_after, double& a_spanSeconds, double& a_newestTime) const
		{
			if (m_samples.size() < 2) {
				return false;
			}
			const auto& after = m_samples.back();
			const auto& before = *std::prev(m_samples.end(), 2);
			a_before = before.value;
			a_after = after.value;
			a_spanSeconds = after.timestamp - before.timestamp;
			a_newestTime = after.timestamp;
			return true;
		}

		void Prune(double a_nowTime)
		{
			if (m_samples.empty() || !std::isfinite(a_nowTime)) {
				return;
			}
			const double renderTime = a_nowTime - m_renderDelay;
			const auto after = std::upper_bound(m_samples.begin(), m_samples.end(), renderTime,
				[](double a_key, const Entry& a_entry) { return a_key < a_entry.timestamp; });
			if (after == m_samples.begin()) {
				return;
			}
			const auto leftBracket = std::prev(after);
			const double newWatermark = leftBracket->timestamp;
			if (!m_hasWatermark || newWatermark > m_watermark) {
				m_watermark = newWatermark;
				m_hasWatermark = true;
			}
			m_samples.erase(m_samples.begin(), leftBracket);
		}

		[[nodiscard]] std::size_t Size() const noexcept { return m_samples.size(); }
		[[nodiscard]] bool Empty() const noexcept { return m_samples.empty(); }
		[[nodiscard]] double RenderDelay() const noexcept { return m_renderDelay; }
		[[nodiscard]] std::size_t MaxSamples() const noexcept { return m_maxSamples; }

	private:
		struct Entry
		{
			std::uint32_t sequence{};
			double timestamp{};
			T value{};
		};

		std::vector<Entry> m_samples;
		double m_renderDelay{ 0.0 };
		std::size_t m_maxSamples{ 2 };
		double m_watermark{ 0.0 };
		bool m_hasWatermark{ false };
	};

	class AdaptiveRenderDelay
	{
	public:
		static constexpr double kMinDelaySeconds = 0.060;
		static constexpr double kMaxDelaySeconds = 0.250;
		static constexpr double kDefaultDelaySeconds = 0.100;
		static constexpr double kIntervalScale = 1.5;
		static constexpr double kMaxChangePerSecond = 0.100;
		static constexpr std::size_t kIntervalWindow = 16;
		static constexpr std::size_t kMinIntervalsForAdaptation = 2;
		static constexpr double kMaxIntervalSeconds = 0.500;
		static constexpr double kTimedMarginSeconds = 0.010;

		void Reset() noexcept
		{
			m_intervals = {};
			m_intervalCount = 0;
			m_nextInterval = 0;
			m_lastArrival = 0.0;
			m_hasLastArrival = false;
			m_timedIntervals = {};
			m_timedLates = {};
			m_timedCount = 0;
			m_nextTimed = 0;
			m_currentDelay = kDefaultDelaySeconds;
		}

		void NoteArrival(double a_nowTime) noexcept
		{
			if (!std::isfinite(a_nowTime)) {
				return;
			}
			if (!m_hasLastArrival) {
				m_hasLastArrival = true;
				m_lastArrival = a_nowTime;
				return;
			}

			const double interval = a_nowTime - m_lastArrival;
			m_lastArrival = a_nowTime;
			if (!(interval > 0.0) || interval > kMaxIntervalSeconds) {
				return;
			}

			m_intervals[m_nextInterval] = interval;
			m_nextInterval = (m_nextInterval + 1) % kIntervalWindow;
			if (m_intervalCount < kIntervalWindow) {
				++m_intervalCount;
			}

			const double target = TargetDelay();
			const double maxStep = kMaxChangePerSecond * interval;
			if (target > m_currentDelay) {
				m_currentDelay = std::min(target, m_currentDelay + maxStep);
			} else {
				m_currentDelay = std::max(target, m_currentDelay - maxStep);
			}
		}

		[[nodiscard]] double TargetDelay() const noexcept
		{
			if (m_intervalCount < kMinIntervalsForAdaptation) {
				return kDefaultDelaySeconds;
			}

			double sum = 0.0;
			for (std::size_t i = 0; i < m_intervalCount; ++i) {
				sum += m_intervals[i];
			}
			const double mean = sum / static_cast<double>(m_intervalCount);

			double deviation = 0.0;
			for (std::size_t i = 0; i < m_intervalCount; ++i) {
				deviation += std::fabs(m_intervals[i] - mean);
			}
			deviation /= static_cast<double>(m_intervalCount);

			return std::clamp(
				std::max(kIntervalScale * mean, mean + deviation),
				kMinDelaySeconds,
				kMaxDelaySeconds);
		}

		void NoteTimedSample(double a_senderIntervalSeconds, double a_lateSeconds) noexcept
		{
			if (!std::isfinite(a_senderIntervalSeconds) || !(a_senderIntervalSeconds > 0.0) ||
				a_senderIntervalSeconds > kMaxIntervalSeconds || !std::isfinite(a_lateSeconds) || a_lateSeconds < 0.0) {
				return;
			}

			m_timedIntervals[m_nextTimed] = a_senderIntervalSeconds;
			m_timedLates[m_nextTimed] = a_lateSeconds;
			m_nextTimed = (m_nextTimed + 1) % kIntervalWindow;
			if (m_timedCount < kIntervalWindow) {
				++m_timedCount;
			}

			const double target = TimedTargetDelay();
			const double maxStep = kMaxChangePerSecond * a_senderIntervalSeconds;
			if (target > m_currentDelay) {
				m_currentDelay = std::min(target, m_currentDelay + maxStep);
			} else {
				m_currentDelay = std::max(target, m_currentDelay - maxStep);
			}
		}

		[[nodiscard]] double TimedTargetDelay() const noexcept
		{
			if (m_timedCount < kMinIntervalsForAdaptation) {
				return kDefaultDelaySeconds;
			}

			double sum = 0.0;
			double maxLate = 0.0;
			for (std::size_t i = 0; i < m_timedCount; ++i) {
				sum += m_timedIntervals[i];
				maxLate = std::max(maxLate, m_timedLates[i]);
			}
			const double mean = sum / static_cast<double>(m_timedCount);
			return std::clamp(mean + maxLate + kTimedMarginSeconds, kMinDelaySeconds, kMaxDelaySeconds);
		}

		[[nodiscard]] double Delay() const noexcept { return m_currentDelay; }
		[[nodiscard]] std::size_t IntervalCount() const noexcept { return m_intervalCount; }

	private:
		std::array<double, kIntervalWindow> m_intervals{};
		std::size_t m_intervalCount{ 0 };
		std::size_t m_nextInterval{ 0 };
		double m_lastArrival{ 0.0 };
		bool m_hasLastArrival{ false };
		double m_currentDelay{ kDefaultDelaySeconds };
		std::array<double, kIntervalWindow> m_timedIntervals{};
		std::array<double, kIntervalWindow> m_timedLates{};
		std::size_t m_timedCount{ 0 };
		std::size_t m_nextTimed{ 0 };
	};
}
