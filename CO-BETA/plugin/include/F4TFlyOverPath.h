#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace F4T::FlyOver
{
	struct CameraPoint
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
		float yaw{ 0.0F };
		float pitch{ 0.0F };
	};

	struct CameraSample
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
		float yaw{ 0.0F };
		float pitch{ 0.0F };
	};

	constexpr std::size_t kMaxPathPoints = 32;

	class Path
	{
	public:
		Path() = default;
		Path(const CameraPoint* a_points, std::size_t a_count, float a_secondsPerSegment)
		{
			Assign(a_points, a_count, a_secondsPerSegment);
		}

		void Assign(const CameraPoint* a_points, std::size_t a_count, float a_secondsPerSegment)
		{
			count_ = 0;
			secondsPerSegment_ = a_secondsPerSegment > 0.5F ? a_secondsPerSegment : 0.5F;
			if (!a_points) {
				return;
			}
			const auto limit = a_count < kMaxPathPoints ? a_count : kMaxPathPoints;
			for (std::size_t index = 0; index < limit; ++index) {
				points_[index] = a_points[index];
			}
			count_ = limit;
		}

		[[nodiscard]] bool Empty() const { return count_ == 0; }
		[[nodiscard]] std::size_t PointCount() const { return count_; }
		[[nodiscard]] float SecondsPerSegment() const { return secondsPerSegment_; }
		[[nodiscard]] double LoopDuration() const
		{
			return static_cast<double>(count_) * static_cast<double>(secondsPerSegment_);
		}

		[[nodiscard]] CameraSample Sample(double a_elapsedSeconds) const
		{
			CameraSample sample{};
			if (count_ == 0) {
				return sample;
			}
			if (count_ == 1) {
				return ToSample(points_[0]);
			}

			const double cycle = LoopDuration();
			double       local = std::fmod(a_elapsedSeconds, cycle);
			if (local < 0.0) {
				local += cycle;
			}
			const auto   segment = static_cast<std::size_t>(local / static_cast<double>(secondsPerSegment_));
			const auto   first = segment % count_;
			const auto   second = (first + 1) % count_;
			double       fraction = (local - static_cast<double>(segment) * static_cast<double>(secondsPerSegment_)) /
			                        static_cast<double>(secondsPerSegment_);
			if (fraction < 0.0) {
				fraction = 0.0;
			}
			if (fraction > 1.0) {
				fraction = 1.0;
			}
			const auto eased = static_cast<float>(fraction * fraction * (3.0 - 2.0 * fraction));

			const auto& a = points_[first];
			const auto& b = points_[second];
			sample.x = Lerp(a.x, b.x, eased);
			sample.y = Lerp(a.y, b.y, eased);
			sample.z = Lerp(a.z, b.z, eased);
			sample.yaw = LerpAngle(a.yaw, b.yaw, eased);
			sample.pitch = Lerp(a.pitch, b.pitch, eased);
			return sample;
		}

		[[nodiscard]] bool InFinalWindow(double a_elapsedSeconds, float a_windowSeconds) const
		{
			if (count_ == 0 || a_windowSeconds <= 0.0F) {
				return false;
			}
			const double cycle = LoopDuration();
			if (cycle <= 0.0) {
				return false;
			}
			double local = std::fmod(a_elapsedSeconds, cycle);
			if (local < 0.0) {
				local += cycle;
			}
			return local >= cycle - static_cast<double>(a_windowSeconds);
		}

	private:
		static CameraSample ToSample(const CameraPoint& a_point)
		{
			CameraSample sample{};
			sample.x = a_point.x;
			sample.y = a_point.y;
			sample.z = a_point.z;
			sample.yaw = a_point.yaw;
			sample.pitch = a_point.pitch;
			return sample;
		}

		static float Lerp(float a_from, float a_to, float a_t)
		{
			return a_from + (a_to - a_from) * a_t;
		}

		static float LerpAngle(float a_from, float a_to, float a_t)
		{
			constexpr float kTwoPi = 6.28318530718F;
			float           delta = std::fmod(a_to - a_from, kTwoPi);
			if (delta > 3.14159265359F) {
				delta -= kTwoPi;
			} else if (delta < -3.14159265359F) {
				delta += kTwoPi;
			}
			float yaw = a_from + delta * a_t;
			if (yaw > kTwoPi) {
				yaw -= kTwoPi;
			} else if (yaw < 0.0F) {
				yaw += kTwoPi;
			}
			return yaw;
		}

		CameraPoint points_[kMaxPathPoints]{};
		std::size_t count_{ 0 };
		float       secondsPerSegment_{ 4.0F };
	};
}
