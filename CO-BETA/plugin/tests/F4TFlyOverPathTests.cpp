#include "F4TFlyOverPath.h"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace
{
	using F4T::FlyOver::CameraPoint;
	using F4T::FlyOver::CameraSample;
	using F4T::FlyOver::Path;

	constexpr float kTolerance = 1.0e-4F;
	constexpr float kSegmentSeconds = 2.0F;

	constexpr CameraPoint kLine[] = {
		{ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F },
		{ 10.0F, 0.0F, 0.0F, 0.0F, 0.0F },
		{ 20.0F, 0.0F, 0.0F, 0.0F, 0.0F },
		{ 30.0F, 0.0F, 0.0F, 0.0F, 0.0F }
	};

	constexpr std::size_t kLineCount = sizeof(kLine) / sizeof(kLine[0]);
	constexpr CameraPoint kSingle = { 4.0F, 5.0F, 6.0F, 1.25F, -0.5F };

	constexpr CameraPoint kYawWrap[] = {
		{ 0.0F, 0.0F, 0.0F, 6.20F, 0.0F },
		{ 100.0F, 0.0F, 0.0F, 0.10F, 0.0F }
	};

	constexpr CameraPoint kShortArc[] = {
		{ 0.0F, 0.0F, 0.0F, 1.0F, 0.0F },
		{ 10.0F, 0.0F, 0.0F, 2.0F, 1.0F }
	};

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= kTolerance;
	}

	bool SampleNearlyEqual(const CameraSample& a_lhs, const CameraSample& a_rhs)
	{
		return NearlyEqual(a_lhs.x, a_rhs.x) && NearlyEqual(a_lhs.y, a_rhs.y) && NearlyEqual(a_lhs.z, a_rhs.z) &&
		       NearlyEqual(a_lhs.yaw, a_rhs.yaw) && NearlyEqual(a_lhs.pitch, a_rhs.pitch);
	}

	bool SampleExactly(const CameraSample& a_sample, const CameraPoint& a_point)
	{
		return a_sample.x == a_point.x && a_sample.y == a_point.y && a_sample.z == a_point.z && a_sample.yaw == a_point.yaw &&
		       a_sample.pitch == a_point.pitch;
	}

	bool SampleIsZero(const CameraSample& a_sample)
	{
		return a_sample.x == 0.0F && a_sample.y == 0.0F && a_sample.z == 0.0F && a_sample.yaw == 0.0F && a_sample.pitch == 0.0F;
	}

	CameraPoint LinePoint(std::size_t a_index)
	{
		CameraPoint point{};
		point.x = static_cast<float>(a_index) * 10.0F;
		return point;
	}
}

int main()
{
	Path empty;
	assert(empty.Empty());
	assert(empty.PointCount() == 0);
	assert(empty.LoopDuration() == 0.0);
	assert(SampleIsZero(empty.Sample(0.0)));
	assert(SampleIsZero(empty.Sample(12.5)));
	assert(SampleIsZero(empty.Sample(-3.0)));
	assert(!empty.InFinalWindow(0.0, 1.0F));
	assert(!empty.InFinalWindow(3.0, 0.5F));

	Path nullPath(nullptr, 6, kSegmentSeconds);
	assert(nullPath.Empty());
	assert(nullPath.PointCount() == 0);
	assert(SampleIsZero(nullPath.Sample(1.0)));

	Path zeroCount(kLine, 0, kSegmentSeconds);
	assert(zeroCount.Empty());
	assert(zeroCount.PointCount() == 0);
	assert(SampleIsZero(zeroCount.Sample(1.0)));

	Path single(&kSingle, 1, kSegmentSeconds);
	assert(!single.Empty());
	assert(single.PointCount() == 1);
	assert(single.LoopDuration() == 2.0);
	assert(SampleExactly(single.Sample(0.0), kSingle));
	assert(SampleExactly(single.Sample(1.7), kSingle));
	assert(SampleExactly(single.Sample(-9.0), kSingle));
	assert(SampleExactly(single.Sample(single.LoopDuration()), kSingle));

	Path line(kLine, kLineCount, kSegmentSeconds);
	assert(!line.Empty());
	assert(line.PointCount() == kLineCount);
	assert(NearlyEqual(static_cast<float>(line.LoopDuration()), 8.0F));
	for (std::size_t index = 0; index < kLineCount; ++index) {
		const double startSeconds = static_cast<double>(index) * static_cast<double>(kSegmentSeconds);
		const double endSeconds = startSeconds + static_cast<double>(kSegmentSeconds);
		assert(SampleExactly(line.Sample(startSeconds), kLine[index]));
		assert(SampleExactly(line.Sample(endSeconds), kLine[(index + 1) % kLineCount]));
	}
	assert(SampleExactly(line.Sample(line.LoopDuration()), kLine[0]));

	const auto quarter = line.Sample(0.5);
	const auto middle = line.Sample(1.0);
	const auto threeQuarter = line.Sample(1.5);
	assert(NearlyEqual(quarter.x, 1.5625F));
	assert(NearlyEqual(middle.x, 5.0F));
	assert(NearlyEqual(threeQuarter.x, 8.4375F));
	assert(NearlyEqual(middle.x - kLine[0].x, kLine[1].x - middle.x));
	assert(NearlyEqual(quarter.x - kLine[0].x, kLine[1].x - threeQuarter.x));
	assert(NearlyEqual(quarter.pitch, 0.0F));

	const double delta = 1.3;
	assert(SampleNearlyEqual(line.Sample(delta), line.Sample(line.LoopDuration() + delta)));
	assert(SampleNearlyEqual(line.Sample(delta), line.Sample(2.0 * line.LoopDuration() + delta)));
	assert(SampleNearlyEqual(line.Sample(-delta), line.Sample(line.LoopDuration() - delta)));
	assert(!NearlyEqual(line.Sample(delta).x, line.Sample(line.LoopDuration() - delta).x));

	Path yawWrap(kYawWrap, 2, kSegmentSeconds);
	assert(std::fabs(yawWrap.Sample(0.5).yaw - 6.2286F) < 1.0e-3F);
	assert(std::fabs(yawWrap.Sample(1.0).yaw - 0.0084F) < 1.0e-3F);
	assert(std::fabs(yawWrap.Sample(1.5).yaw - 0.0714F) < 1.0e-3F);
	assert(std::fabs(yawWrap.Sample(1.0).yaw - 3.1416F) > 1.0F);
	assert(SampleExactly(yawWrap.Sample(0.0), kYawWrap[0]));
	assert(SampleExactly(yawWrap.Sample(2.0), kYawWrap[1]));

	Path shortArc(kShortArc, 2, kSegmentSeconds);
	assert(NearlyEqual(shortArc.Sample(0.5).yaw, 1.15625F));
	assert(NearlyEqual(shortArc.Sample(1.0).yaw, 1.5F));
	assert(NearlyEqual(shortArc.Sample(0.5).pitch, 0.15625F));
	assert(NearlyEqual(shortArc.Sample(1.0).pitch, 0.5F));

	constexpr std::size_t kManyCount = F4T::FlyOver::kMaxPathPoints + 8;
	CameraPoint           many[kManyCount]{};
	for (std::size_t index = 0; index < kManyCount; ++index) {
		many[index] = LinePoint(index);
	}
	Path clamped(many, kManyCount, kSegmentSeconds);
	assert(clamped.PointCount() == F4T::FlyOver::kMaxPathPoints);
	assert(SampleExactly(clamped.Sample(0.0), LinePoint(0)));
	assert(SampleExactly(clamped.Sample(static_cast<double>(F4T::FlyOver::kMaxPathPoints - 1) * 2.0),
		LinePoint(F4T::FlyOver::kMaxPathPoints - 1)));
	assert(NearlyEqual(static_cast<float>(clamped.LoopDuration()), static_cast<float>(F4T::FlyOver::kMaxPathPoints) * 2.0F));
	assert(SampleExactly(clamped.Sample(clamped.LoopDuration()), LinePoint(0)));

	assert(!line.InFinalWindow(0.0, 1.0F));
	assert(!line.InFinalWindow(5.0, 1.0F));
	assert(!line.InFinalWindow(6.99, 1.0F));
	assert(line.InFinalWindow(7.0, 1.0F));
	assert(line.InFinalWindow(7.5, 1.0F));
	assert(line.InFinalWindow(7.99, 1.0F));
	assert(!line.InFinalWindow(8.0, 1.0F));
	assert(!line.InFinalWindow(9.5, 1.0F));
	assert(line.InFinalWindow(-0.5, 1.0F));
	assert(line.InFinalWindow(15.5, 1.0F));
	assert(!line.InFinalWindow(7.5, 0.0F));
	assert(!line.InFinalWindow(7.5, -1.0F));
	assert(!line.InFinalWindow(7.5, 0.4F));
	assert(line.InFinalWindow(7.7, 0.4F));

	Path floored(kLine, kLineCount, 0.0F);
	assert(NearlyEqual(floored.SecondsPerSegment(), 0.5F));
	assert(NearlyEqual(static_cast<float>(floored.LoopDuration()), 2.0F));

	return 0;
}
