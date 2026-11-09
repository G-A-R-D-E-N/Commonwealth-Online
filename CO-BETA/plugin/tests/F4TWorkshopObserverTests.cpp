#include "F4TWorkshopObserver.h"

#include <cassert>
#include <optional>

namespace
{
	using F4T::WorkshopObserver::Angle;
	using F4T::WorkshopObserver::EffectiveAngle;
	using F4T::WorkshopObserver::ReleaseTracker;
	using F4T::WorkshopObserver::ScrapSource;
	using F4T::WorkshopObserver::ScrapTracker;

	constexpr Angle kRaw{ 0.25F, -0.5F, 1.5F };
	constexpr std::uint32_t kHeld = 0x00100001;
	constexpr std::uint32_t kOther = 0x00100002;

	void NonActorUsesAllAxes()
	{
		const auto angle = EffectiveAngle(kRaw, false, std::nullopt);
		assert(angle && angle->x == kRaw.x && angle->y == kRaw.y && angle->z == kRaw.z);
		const auto ignoredFlag = EffectiveAngle(kRaw, false, false);
		assert(ignoredFlag && ignoredFlag->x == kRaw.x && ignoredFlag->y == kRaw.y);
	}

	void ActorUsesXyOnlyWithFlag()
	{
		const auto flagged = EffectiveAngle(kRaw, true, true);
		assert(flagged && flagged->x == kRaw.x && flagged->y == kRaw.y && flagged->z == kRaw.z);
		const auto yawOnly = EffectiveAngle(kRaw, true, false);
		assert(yawOnly && yawOnly->x == 0.0F && yawOnly->y == 0.0F && yawOnly->z == kRaw.z);
		assert(!EffectiveAngle(kRaw, true, std::nullopt));
	}

	void NormalItemReleaseReportsOnce()
	{
		ReleaseTracker tracker;
		assert(tracker.OnReleaseArm(kHeld, false));
		assert(tracker.OnPlaceAttempt(kHeld));
	}

	void WireReleaseReportsOnce()
	{
		ReleaseTracker tracker;
		assert(tracker.OnReleaseArm(kHeld, true));
		assert(!tracker.OnPlaceAttempt(kHeld));
		assert(tracker.OnPlaceAttempt(kHeld));
	}

	void WireSecondHalfOnlyMatchesTheReleasedRef()
	{
		ReleaseTracker tracker;
		assert(tracker.OnReleaseArm(kHeld, true));
		assert(tracker.OnPlaceAttempt(kOther));
		assert(tracker.OnPlaceAttempt(kHeld));
	}

	void NewItemPlacementIsNotSuppressed()
	{
		ReleaseTracker tracker;
		assert(tracker.OnPlaceAttempt(kHeld));
		assert(tracker.OnPlaceAttempt(kHeld));
	}

	void NormalReleaseClearsAWireMarker()
	{
		ReleaseTracker tracker;
		assert(tracker.OnReleaseArm(kHeld, true));
		assert(tracker.OnReleaseArm(kHeld, false));
		assert(tracker.OnPlaceAttempt(kHeld));
	}

	void NullHandleNeverReports()
	{
		ReleaseTracker tracker;
		assert(!tracker.OnReleaseArm(0, false));
		assert(!tracker.OnReleaseArm(0, true));
		assert(tracker.OnPlaceAttempt(0));
	}

	void ButtonAcceptIsNotAClick()
	{
		ScrapTracker tracker;
		assert(tracker.OnAccept() == ScrapSource::kButton);
	}

	void ClickAcceptIsConsumedOnce()
	{
		ScrapTracker tracker;
		tracker.OnConfirmCall(0);
		assert(tracker.OnAccept() == ScrapSource::kClick);
		assert(tracker.OnAccept() == ScrapSource::kButton);
	}

	void CancelClickNeverMarksAnAccept()
	{
		ScrapTracker tracker;
		tracker.OnConfirmCall(1);
		assert(tracker.OnAccept() == ScrapSource::kButton);
		tracker.OnConfirmCall(0);
		tracker.OnConfirmCall(1);
		assert(tracker.OnAccept() == ScrapSource::kButton);
	}

	void AnyNonCancelIdIsAnAccept()
	{
		ScrapTracker tracker;
		tracker.OnConfirmCall(2);
		assert(tracker.OnAccept() == ScrapSource::kClick);
		tracker.OnConfirmCall(-1);
		assert(tracker.OnAccept() == ScrapSource::kClick);
	}
}

int main()
{
	NonActorUsesAllAxes();
	ActorUsesXyOnlyWithFlag();
	NormalItemReleaseReportsOnce();
	WireReleaseReportsOnce();
	WireSecondHalfOnlyMatchesTheReleasedRef();
	NewItemPlacementIsNotSuppressed();
	NormalReleaseClearsAWireMarker();
	NullHandleNeverReports();
	ButtonAcceptIsNotAClick();
	ClickAcceptIsConsumedOnce();
	CancelClickNeverMarksAnAccept();
	AnyNonCancelIdIsAnAccept();
	return 0;
}
