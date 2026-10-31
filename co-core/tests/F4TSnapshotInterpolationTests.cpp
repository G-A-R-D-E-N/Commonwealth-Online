#include "F4TSnapshotInterpolation.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
	using namespace F4T::Replication;
	using VBuf = SnapshotInterpolationBuffer<Vec3>;
	using FBuf = SnapshotInterpolationBuffer<float>;
	using MBuf = SnapshotInterpolationBuffer<MovementSnapshot>;

	bool Near(float a_lhs, float a_rhs, float a_eps = 1.0e-3F)
	{
		return std::fabs(a_lhs - a_rhs) <= a_eps;
	}

	bool NearVec3(const Vec3& a_lhs, const Vec3& a_rhs, float a_eps = 1.0e-3F)
	{
		return Near(a_lhs.x, a_rhs.x, a_eps) && Near(a_lhs.y, a_rhs.y, a_eps) && Near(a_lhs.z, a_rhs.z, a_eps);
	}

	Vec3 TrueTrajectory(double a_time)
	{
		return Vec3{ static_cast<float>(a_time), static_cast<float>(2.0 * a_time), static_cast<float>(-a_time) };
	}
}

int main()
{
	constexpr float pi = 3.14159265F;
	constexpr float twoPi = 2.0F * pi;

	// Value primitives (clamped).
	assert(Near(LerpScalar(0.0F, 10.0F, 0.5F), 5.0F));
	assert(Near(LerpScalar(0.0F, 10.0F, -1.0F), 0.0F));
	assert(Near(LerpScalar(0.0F, 10.0F, 2.0F), 10.0F));
	assert(NearVec3(LerpVec3(Vec3{ 0, 0, 0 }, Vec3{ 2, 4, 6 }, 0.5F), Vec3{ 1, 2, 3 }));

	// LerpAngle: shortest arc, normalized to [0, 2pi).
	assert(Near(LerpAngle(0.0F, pi / 2.0F, 0.5F), pi / 4.0F));   // no wrap
	{
		const float mid = LerpAngle(0.1F, 6.2F, 0.5F);   // 5.7deg -> 355deg: shortest arc goes backward across 0
		assert(Near(mid, 0.0F, 2.0e-2F) || Near(mid, twoPi, 2.0e-2F));
	}

	{
		const float inf = std::numeric_limits<float>::infinity();
		const float nan = std::nanf("");
		assert(std::isfinite(LerpAngle(nan, 1.0F, 0.5F)));
		assert(std::isfinite(LerpAngle(1.0F, inf, 0.5F)));
		assert(std::isfinite(LerpAngle(-inf, nan, 0.5F)));
		assert(LerpAngle(nan, nan, 0.5F) == 0.0F);
		assert(Near(LerpAngle(nan, pi / 2.0F, 0.5F), pi / 2.0F));
		for (const float huge : { 1.0e6F, -1.0e6F, 1000.0F, -1000.0F }) {
			const float normalized = LerpAngle(huge, huge, 0.0F);
			assert(std::isfinite(normalized) && normalized >= 0.0F && normalized < twoPi + 1.0e-3F);
		}
	}

	// Constructor invariants clamp to a valid state.
	{
		FBuf clampDelay(-5.0, 0);
		assert(clampDelay.RenderDelay() == 0.0);
		assert(clampDelay.MaxSamples() >= 2);
		FBuf clampNaN(std::nan(""), 100);
		assert(clampNaN.RenderDelay() == 0.0);
		assert(clampNaN.MaxSamples() == 100);
	}

	// Empty buffer.
	{
		VBuf buffer(0.25);
		assert(buffer.At(1.0, LerpVec3).status == VBuf::Status::Empty);
	}

	// Bracket / alpha exactness (linear trajectory interpolates exactly).
	{
		VBuf buffer(0.0);
		buffer.Insert(1, 0.0, Vec3{ 0, 0, 0 });
		buffer.Insert(2, 1.0, Vec3{ 10, 20, -10 });
		const auto sampled = buffer.At(0.5, LerpVec3);
		assert(sampled.status == VBuf::Status::Interpolating);
		assert(NearVec3(sampled.value, Vec3{ 5, 10, -5 }));
	}

	// Out-of-order arrival + duplicate sequence + duplicate timestamp.
	{
		FBuf buffer(0.0);
		buffer.Insert(2, 1.0, 100.0F);
		buffer.Insert(1, 0.0, 0.0F);     // earlier timestamp, lower sequence -> accepted, sorted
		buffer.Insert(2, 1.0, 999.0F);   // duplicate sequence -> ignored
		buffer.Insert(3, 1.0, 999.0F);   // duplicate timestamp -> ignored
		assert(buffer.Size() == 2);
		assert(Near(buffer.At(0.5, LerpScalar).value, 50.0F));
	}

	// Starvation is explicit and holds, never a jump; non-finite now holds last.
	{
		FBuf buffer(0.0);
		buffer.Insert(1, 1.0, 10.0F);
		buffer.Insert(2, 2.0, 20.0F);
		const auto early = buffer.At(0.5, LerpScalar);
		assert(early.status == FBuf::Status::HoldingStart && Near(early.value, 10.0F));
		const auto late = buffer.At(3.0, LerpScalar);
		assert(late.status == FBuf::Status::HoldingEnd && Near(late.value, 20.0F));
		const auto nonFinite = buffer.At(std::nan(""), LerpScalar);
		assert(nonFinite.status == FBuf::Status::HoldingEnd && Near(nonFinite.value, 20.0F));
	}

	// Bounded memory.
	{
		FBuf buffer(0.0, 4);
		for (std::uint32_t i = 0; i < 10; ++i) {
			buffer.Insert(i, static_cast<double>(i), static_cast<float>(i));
		}
		assert(buffer.Size() <= 4);
	}

	// Prune sets a monotonic play-past watermark; played-past inserts rejected; left bracket preserved.
	{
		FBuf buffer(0.0, 32);
		for (std::uint32_t i = 0; i <= 5; ++i) {
			buffer.Insert(i, static_cast<double>(i), static_cast<float>(i));   // t = 0..5
		}
		buffer.Prune(3.0);   // renderTime 3.0 -> left bracket t=3; drop t<3; watermark=3
		const auto sizeAfterPrune = buffer.Size();
		buffer.Insert(100, 2.0, 42.0F);   // <= watermark -> rejected
		assert(buffer.Size() == sizeAfterPrune);
		buffer.Insert(101, 3.0, 42.0F);   // == watermark -> rejected
		assert(buffer.Size() == sizeAfterPrune);
		buffer.Insert(102, 6.0, 60.0F);   // future -> accepted
		assert(buffer.Size() == sizeAfterPrune + 1);
		const auto sampled = buffer.At(3.5, LerpScalar);   // bracket 3..4 preserved
		assert(sampled.status == FBuf::Status::Interpolating && Near(sampled.value, 3.5F));
	}

	{
		const double dt = 0.1;
		const double delay = 0.3;
		constexpr int sampleCount = 13;
		VBuf buffer(delay, 64);
		bool arrived[sampleCount] = {};
		const auto arrivalJitter = [](int a_i) { return (a_i % 2 == 0) ? 0.22 : 0.03; };

		float lastX = -1.0e9F;
		bool primed = false;
		int interpolatedFrames = 0;
		for (int frame = 0; frame <= 40; ++frame) {
			const double now = 0.05 * frame;
			for (int i = 0; i < sampleCount; ++i) {
				if (!arrived[i] && (dt * i + arrivalJitter(i)) <= now + 1.0e-9) {
					buffer.Insert(static_cast<std::uint32_t>(i), dt * i, TrueTrajectory(dt * i));
					arrived[i] = true;
				}
			}
			const auto sampled = buffer.At(now, LerpVec3);
			const double renderTime = now - delay;
			if (sampled.status == VBuf::Status::Interpolating) {
				primed = true;
				assert(NearVec3(sampled.value, TrueTrajectory(renderTime), 1.0e-3F));
				assert(sampled.value.x >= lastX - 1.0e-4F);
				lastX = sampled.value.x;
				++interpolatedFrames;
			} else if (primed && renderTime > 0.0 && renderTime < dt * (sampleCount - 2)) {
				assert(false);
			}
		}
		assert(interpolatedFrames >= 10);
	}

	{
		assert(NearVec3(Vec3{ 1, 2, 3 } + Vec3{ 4, 5, 6 }, Vec3{ 5, 7, 9 }));
		assert(NearVec3(Vec3{ 4, 5, 6 } - Vec3{ 1, 2, 3 }, Vec3{ 3, 3, 3 }));
		assert(NearVec3(Vec3{ 1, -2, 3 } * 2.0F, Vec3{ 2, -4, 6 }));
		assert(Near(Distance(Vec3{ 0, 0, 0 }, Vec3{ 3, 4, 0 }), 5.0F));
		assert(IsFinite(Vec3{ 1, 2, 3 }));
		assert(!IsFinite(Vec3{ std::nanf(""), 2, 3 }));

		assert(!ExceedsSnapshotSnapDistance(Vec3{ 0, 0, 0 }, Vec3{ 1499.0F, 0, 0 }));
		assert(ExceedsSnapshotSnapDistance(Vec3{ 0, 0, 0 }, Vec3{ 1501.0F, 0, 0 }));
		assert(ExceedsSnapshotSnapDistance(Vec3{ 0, 0, 0 }, Vec3{ 200.0F, 0, 0 }, 100.0F));
		assert(!ExceedsSnapshotSnapDistance(Vec3{ std::nanf(""), 0, 0 }, Vec3{ 9000.0F, 0, 0 }));
	}

	{
		const MovementSnapshot a{ Vec3{ 0, 0, 10 }, 3.0F, true };
		const MovementSnapshot b{ Vec3{ 10, 20, 12 }, -3.0F, true };

		const auto midpoint = LerpMovementSnapshot(a, b, 0.5F);
		assert(NearVec3(midpoint.position, Vec3{ 5, 10, 11 }));
		assert(Near(midpoint.heading, pi, 1.0e-3F));
		assert(midpoint.moving);

		const MovementSnapshot wrapA{ Vec3{ 0, 0, 0 }, 0.05F, false };
		const MovementSnapshot wrapB{ Vec3{ 0, 0, 0 }, -0.05F, false };
		const auto wrapped = LerpMovementSnapshot(wrapA, wrapB, 0.5F);
		assert(Near(wrapped.heading, 0.0F, 1.0e-3F) || Near(wrapped.heading, twoPi, 1.0e-3F));
		assert(!wrapped.moving);
	}

	{
		const MovementSnapshot a{ Vec3{ 0, 0, 5 }, 1.0F, true };
		const MovementSnapshot b{ Vec3{ 10, 0, 9 }, 1.0F, true };

		const auto ahead = ExtrapolateMovementSnapshot(a, b, 0.1, 0.05);
		assert(NearVec3(ahead.position, Vec3{ 14.5F, 0, 9 }));
		assert(Near(ahead.heading, 1.0F));
		assert(ahead.moving);

		const MovementSnapshot stopped{ Vec3{ 10, 0, 9 }, 1.0F, false };
		assert(NearVec3(ExtrapolateMovementSnapshot(a, stopped, 0.1, 0.05).position, stopped.position));
		assert(NearVec3(ExtrapolateMovementSnapshot(a, b, 0.1, 0.0).position, b.position));
		assert(NearVec3(ExtrapolateMovementSnapshot(a, b, 0.0, 0.05).position, b.position));
		assert(NearVec3(ExtrapolateMovementSnapshot(a, b, -0.1, 0.05).position, b.position));
		assert(NearVec3(ExtrapolateMovementSnapshot(a, b, 0.1, std::nan("")).position, b.position));

		const MovementSnapshot far{ Vec3{ 1400, 0, 0 }, 0.0F, true };
		const MovementSnapshot farther{ Vec3{ 2800, 0, 0 }, 0.0F, true };
		const auto clamped = ExtrapolateMovementSnapshot(far, farther, 0.1, 0.25);
		assert(Near(Distance(farther.position, clamped.position), kSnapshotSnapDistance, 1.0e-2F));
	}

	{
		const auto at = [](float a_x) { return MovementSnapshot{ Vec3{ a_x, 0, 0 }, 0.0F, true }; };
		MBuf buffer(0.1, 32);
		buffer.Insert(1, 1.0, at(0.0F));
		buffer.Insert(2, 1.1, at(100.0F));

		const auto inside = buffer.AtExtrapolated(1.15, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(inside.status == MBuf::Status::Interpolating);
		assert(Near(inside.value.position.x, 50.0F));

		MBuf exact(0.0, 32);
		exact.Insert(1, 1.0, at(0.0F));
		exact.Insert(2, 1.1, at(100.0F));
		const auto atNewestExact = exact.AtExtrapolated(1.1, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(atNewestExact.status == MBuf::Status::HoldingEnd && Near(atNewestExact.value.position.x, 100.0F));

		const auto ahead = buffer.AtExtrapolated(1.25, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(ahead.status == MBuf::Status::Extrapolating);
		assert(Near(ahead.value.position.x, 145.0F));

		const auto atCap = buffer.AtExtrapolated(1.35, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(atCap.status == MBuf::Status::Extrapolating);
		assert(Near(atCap.value.position.x, 205.0F));

		const auto pastCap = buffer.AtExtrapolated(1.36, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(pastCap.status == MBuf::Status::Extrapolating && Near(pastCap.value.position.x, 205.0F));

		const auto disabled = buffer.AtExtrapolated(1.25, LerpMovementSnapshot, ExtrapolateMovementSnapshot, 0.0);
		assert(disabled.status == MBuf::Status::HoldingEnd && Near(disabled.value.position.x, 100.0F));

		const auto nonFinite = buffer.AtExtrapolated(std::nan(""), LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(nonFinite.status == MBuf::Status::HoldingEnd && Near(nonFinite.value.position.x, 100.0F));

		MBuf single(0.1, 32);
		single.Insert(1, 1.0, at(7.0F));
		const auto onlyOne = single.AtExtrapolated(9.0, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds);
		assert(onlyOne.status == MBuf::Status::HoldingEnd && Near(onlyOne.value.position.x, 7.0F));

		MBuf empty(0.1, 32);
		assert(empty.AtExtrapolated(1.0, LerpMovementSnapshot, ExtrapolateMovementSnapshot, kMaxSnapshotExtrapolationSeconds).status == MBuf::Status::Empty);
	}

	{
		FBuf buffer(0.0, 8);
		buffer.Insert(1, 0.0, 0.0F);
		buffer.Insert(2, 1.0, 10.0F);
		buffer.SetRenderDelay(0.5);
		assert(Near(static_cast<float>(buffer.RenderDelay()), 0.5F));
		assert(Near(buffer.At(1.25, LerpScalar).value, 7.5F));
		buffer.SetRenderDelay(std::nan(""));
		assert(buffer.RenderDelay() == 0.0);
		buffer.SetRenderDelay(-1.0);
		assert(buffer.RenderDelay() == 0.0);

		FBuf cleared(0.0, 8);
		cleared.Insert(1, 1.0, 10.0F);
		cleared.Prune(1.0);
		cleared.Insert(2, 0.5, 5.0F);
		assert(cleared.Size() == 1);
		cleared.Clear();
		assert(cleared.Empty());
		cleared.Insert(3, 0.5, 5.0F);
		assert(cleared.Size() == 1);
		assert(Near(cleared.At(0.5, LerpScalar).value, 5.0F));
	}

	{
		AdaptiveRenderDelay delay;
		assert(Near(static_cast<float>(delay.Delay()), 0.100F));
		assert(delay.IntervalCount() == 0);
		delay.NoteArrival(0.0);
		delay.NoteArrival(0.1);
		assert(delay.IntervalCount() == 1);
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.100F));
		assert(Near(static_cast<float>(delay.Delay()), 0.100F));

		for (int i = 2; i <= 20; ++i) {
			delay.NoteArrival(0.1 * i);
		}
		assert(delay.IntervalCount() == 16);
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.150F));
		assert(Near(static_cast<float>(delay.Delay()), 0.150F));
	}

	{
		AdaptiveRenderDelay delay;
		for (int i = 0; i <= 20; ++i) {
			delay.NoteArrival(i / 30.0);
		}
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.060F));
		assert(delay.Delay() < 0.100 && delay.Delay() >= 0.060);
	}

	{
		AdaptiveRenderDelay delay;
		for (int i = 0; i <= 3; ++i) {
			delay.NoteArrival(i * 0.4);
		}
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.250F));
	}

	{
		AdaptiveRenderDelay delay;
		delay.NoteArrival(0.0);
		delay.NoteArrival(0.05);
		delay.NoteArrival(0.10);
		delay.NoteArrival(0.15);
		delay.NoteArrival(0.40);
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.175F));
	}

	{
		AdaptiveRenderDelay delay;
		delay.NoteArrival(0.0);
		delay.NoteArrival(0.1);
		delay.NoteArrival(0.2);
		delay.NoteArrival(0.3);
		assert(delay.IntervalCount() == 3);
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.150F));
		assert(delay.Delay() > 0.100 && delay.Delay() < 0.150);
	}

	{
		AdaptiveRenderDelay delay;
		delay.NoteArrival(1.0);
		delay.NoteArrival(1.0);
		delay.NoteArrival(0.5);
		delay.NoteArrival(std::nan(""));
		assert(delay.IntervalCount() == 0);
		assert(Near(static_cast<float>(delay.Delay()), 0.100F));

		for (int i = 0; i <= 200; ++i) {
			delay.NoteArrival(2.0 + (i * 0.1));
		}
		assert(Near(static_cast<float>(delay.Delay()), 0.150F));
		delay.Reset();
		assert(delay.IntervalCount() == 0);
		assert(Near(static_cast<float>(delay.Delay()), 0.100F));
	}

	{
		const MovementSnapshot a{ Vec3{ 0, 0, 0 }, 0.0F, true };
		const MovementSnapshot b{ Vec3{ 10, 0, 0 }, 0.0F, true };
		assert(NearVec3(ExtrapolateMovementSnapshot(a, b, 0.001, 0.15).position, Vec3{ 62.5F, 0, 0 }));
	}

	{
		AdaptiveRenderDelay delay;
		delay.NoteArrival(0.0);
		delay.NoteArrival(0.1);
		delay.NoteArrival(0.2);
		const double steady = delay.Delay();
		delay.NoteArrival(30.0);
		assert(delay.IntervalCount() == 2);
		assert(delay.Delay() == steady);
		delay.NoteArrival(30.1);
		assert(delay.IntervalCount() == 3);
		assert(Near(static_cast<float>(delay.TargetDelay()), 0.150F));
	}

	return 0;
}
