#pragma once

#include "F4TProxyMovementDiagnostics.h"
#include "F4TSnapshotInterpolation.h"
#include "F4TSenderTimeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace F4T::ProxySnapshotMovement
{
	using F4T::Replication::AdaptiveRenderDelay;
	using F4T::Replication::MovementSnapshot;
	using F4T::Replication::SenderClock;
	using F4T::Replication::SnapshotInterpolationBuffer;
	using F4T::Replication::Vec3;
	using F4T::ProxyMovementDiagnostics::SnapReason;

	inline constexpr std::size_t kSampleCapacity = 32;
	inline constexpr double kBlendSeconds = 0.100;
	inline constexpr double kMaxBlendCoastSpeed = 1000.0;

	struct State
	{
		SnapshotInterpolationBuffer<MovementSnapshot> buffer{
			AdaptiveRenderDelay::kDefaultDelaySeconds,
			kSampleCapacity
		};
		AdaptiveRenderDelay delay{};
		SenderClock senderClock{};
		MovementSnapshot lastSnapshot{};
		MovementSnapshot lastRendered{};
		MovementSnapshot blendFrom{};
		Vec3 lastRenderVelocity{};
		Vec3 blendVelocity{};
		double blendStart{};
		double lastRenderSeconds{};
		std::uint32_t playerId{};
		std::uint32_t nextSequence{ 1 };
		bool hasLastSnapshot{ false };
		bool hasPlayerId{ false };
		bool ranAhead{ false };
		bool blending{ false };
		bool hasRendered{ false };
		bool senderTimeline{ false };
		F4T::ProxyMovementDiagnostics::Window diagnostics{};
	};

	struct RenderResult
	{
		MovementSnapshot snapshot{};
		bool hasValue{ false };
		SnapshotInterpolationBuffer<MovementSnapshot>::Status status{};
	};

	inline Vec3 CapBlendCoastVelocity(Vec3 a_velocity) noexcept
	{
		const double speed = std::hypot(static_cast<double>(a_velocity.x), static_cast<double>(a_velocity.y));
		if (!std::isfinite(speed)) {
			return Vec3{};
		}
		if (speed > kMaxBlendCoastSpeed) {
			const auto scale = static_cast<float>(kMaxBlendCoastSpeed / speed);
			a_velocity.x *= scale;
			a_velocity.y *= scale;
		}
		return a_velocity;
	}

	inline void Reset(State& a_state) noexcept
	{
		a_state.buffer.Clear();
		a_state.delay.Reset();
		a_state.lastSnapshot = {};
		a_state.hasLastSnapshot = false;
		a_state.hasPlayerId = false;
		a_state.ranAhead = false;
		a_state.blending = false;
		a_state.hasRendered = false;
		a_state.lastRenderVelocity = {};
		a_state.nextSequence = 1;
		a_state.senderClock.Reset();
		a_state.senderTimeline = false;
		a_state.diagnostics = F4T::ProxyMovementDiagnostics::Window{};
	}

	inline bool Record(
		State& a_state,
		std::uint32_t a_playerId,
		const MovementSnapshot& a_snapshot,
		bool a_forceSnap,
		double a_nowSeconds,
		double a_senderSeconds) noexcept
	{
		if (!std::isfinite(a_nowSeconds) || !std::isfinite(a_snapshot.heading) ||
			!F4T::Replication::IsFinite(a_snapshot.position)) {
			return false;
		}

		if (a_state.hasPlayerId && a_state.playerId != a_playerId) {
			Reset(a_state);
		}
		a_state.playerId = a_playerId;
		a_state.hasPlayerId = true;

		if (a_state.hasLastSnapshot && a_state.lastSnapshot == a_snapshot) {
			return false;
		}

		const auto mapping = a_state.senderClock.Observe(a_senderSeconds, a_nowSeconds);
		if (mapping.result == SenderClock::Result::Stale) {
			return false;
		}
		const bool usingSender = mapping.result != SenderClock::Result::Unavailable;
		if (usingSender != a_state.senderTimeline && !a_state.buffer.Empty()) {
			a_state.buffer.Clear();
			a_state.delay.Reset();
		}
		a_state.senderTimeline = usingSender;

		double timestamp = a_nowSeconds;
		bool senderReset = false;
		if (usingSender) {
			timestamp = mapping.mappedSeconds;
			if (mapping.result == SenderClock::Result::Reset) {
				a_state.delay.Reset();
				senderReset = true;
			} else {
				a_state.delay.NoteTimedSample(mapping.senderIntervalSeconds, mapping.lateSeconds);
			}
		} else {
			a_state.delay.NoteArrival(a_nowSeconds);
		}

		const bool snap = a_forceSnap || senderReset || !a_state.hasLastSnapshot ||
			F4T::Replication::ExceedsSnapshotSnapDistance(a_state.lastSnapshot.position, a_snapshot.position);
		if (snap) {
			if (!a_forceSnap) {
				F4T::ProxyMovementDiagnostics::NoteSnap(
					a_state.diagnostics,
					a_state.hasLastSnapshot ? SnapReason::kDistance : SnapReason::kFirstSample);
			}
			a_state.buffer.Clear();
			a_state.blending = false;
			a_state.ranAhead = false;
			a_state.hasRendered = false;
			a_state.lastRenderVelocity = {};
		} else if (a_state.ranAhead || a_state.blending) {
			a_state.blendFrom = a_state.lastRendered;
			a_state.blendVelocity = a_state.lastRenderVelocity;
			a_state.blendStart = a_state.lastRenderSeconds;
			a_state.blending = true;
			a_state.ranAhead = false;
		}

		a_state.buffer.Insert(a_state.nextSequence++, timestamp, a_snapshot);
		a_state.lastSnapshot = a_snapshot;
		a_state.hasLastSnapshot = true;
		return true;
	}

	[[nodiscard]] inline RenderResult Render(State& a_state, double a_nowSeconds) noexcept
	{
		RenderResult result{};
		if (a_state.buffer.Empty()) {
			return result;
		}

		a_state.buffer.SetRenderDelay(a_state.delay.Delay());
		const auto sample = a_state.buffer.AtExtrapolated(
			a_nowSeconds,
			F4T::Replication::LerpMovementSnapshot,
			F4T::Replication::ExtrapolateMovementSnapshot,
			F4T::Replication::kExtrapolationEaseSeconds);
		if (sample.status == SnapshotInterpolationBuffer<MovementSnapshot>::Status::Empty) {
			return result;
		}

		using Status = SnapshotInterpolationBuffer<MovementSnapshot>::Status;
		result.status = sample.status;
		result.snapshot = sample.value;
		if (a_state.blending) {
			const double t = (a_nowSeconds - a_state.blendStart) / kBlendSeconds;
			if (t >= 1.0) {
				a_state.blending = false;
			} else {
				const double u = t > 0.0 ? t : 0.0;
				const float s = static_cast<float>(u * u * (3.0 - 2.0 * u));
				const float coast = static_cast<float>(kBlendSeconds * (u - 0.5 * u * u));
				const Vec3 from{
					a_state.blendFrom.position.x + a_state.blendVelocity.x * coast,
					a_state.blendFrom.position.y + a_state.blendVelocity.y * coast,
					a_state.blendFrom.position.z
				};
				result.snapshot.position = F4T::Replication::LerpVec3(from, sample.value.position, s);
				result.snapshot.heading = F4T::Replication::LerpAngle(a_state.blendFrom.heading, sample.value.heading, s);
			}
		}

		const double dt = a_nowSeconds - a_state.lastRenderSeconds;
		if (sample.status == Status::Extrapolating && !a_state.blending) {
			MovementSnapshot before{};
			MovementSnapshot after{};
			double span = 0.0;
			double newest = 0.0;
			if (a_state.buffer.LastPair(before, after, span, newest)) {
				const double ahead = std::min(a_nowSeconds - a_state.buffer.RenderDelay() - newest, F4T::Replication::kExtrapolationEaseSeconds);
				a_state.lastRenderVelocity = F4T::Replication::ExtrapolationVelocity(before, after, span, ahead);
			}
		} else if (a_state.hasRendered && dt > 0.0) {
			a_state.lastRenderVelocity = CapBlendCoastVelocity((result.snapshot.position - a_state.lastRendered.position) * static_cast<float>(1.0 / dt));
		}
		a_state.lastRenderSeconds = a_nowSeconds;
		a_state.ranAhead = sample.status == Status::Extrapolating || sample.status == Status::HoldingEnd;
		a_state.lastRendered = result.snapshot;
		a_state.hasRendered = true;
		result.hasValue = true;
		return result;
	}
}
