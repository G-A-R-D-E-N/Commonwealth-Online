#include "F4TProxySaveSafety.h"

#include <array>
#include <cassert>
#include <string_view>
#include <vector>

namespace
{
	using F4T::ProxySaveSafety::CanRecordStandIn;
	using F4T::ProxySaveSafety::DecideProxyRetire;
	using F4T::ProxySaveSafety::DecideSavedProxySweep;
	using F4T::ProxySaveSafety::DecideSaveTeardownRoute;
	using F4T::ProxySaveSafety::DecideSlotSaveCleanup;
	using F4T::ProxySaveSafety::EvaluateTickGate;
	using F4T::ProxySaveSafety::GetTickGateLabel;
	using F4T::ProxySaveSafety::IsCreatedFallbackBaseProxy;
	using F4T::ProxySaveSafety::IsPlacedFallbackProxy;
	using F4T::ProxySaveSafety::IsProtectedPlacedCodsworth;
	using F4T::ProxySaveSafety::IsSaveStartFallback;
	using F4T::ProxySaveSafety::IsSaveTeardownOnGameThread;
	using F4T::ProxySaveSafety::IsSuccessfulLoadMessageData;
	using F4T::ProxySaveSafety::IsUniqueStandInCandidate;
	using F4T::ProxySaveSafety::IsValidStandInRecordLength;
	using F4T::ProxySaveSafety::kMaximumStandInFormIds;
	using F4T::ProxySaveSafety::kSweepSourcePriority;
	using F4T::ProxySaveSafety::ProxyRetireAction;
	using F4T::ProxySaveSafety::SaveTeardownRoute;
	using F4T::ProxySaveSafety::SavedProxySweep;
	using F4T::ProxySaveSafety::ShouldBumpSessionGeneration;
	using F4T::ProxySaveSafety::ShouldKeepStandIn;
	using F4T::ProxySaveSafety::ShouldLogPeriodic;
	using F4T::ProxySaveSafety::ShouldRunAnotherSweepPass;
	using F4T::ProxySaveSafety::SlotSaveCleanup;
	using F4T::ProxySaveSafety::SweepSource;
	using F4T::ProxySaveSafety::TickGate;
	using F4T::ProxySaveSafety::TickGateInputs;
	using F4T::ProxySaveSafety::VisitSweepSourcesInPriorityOrder;

	void TestPlacedCodsworthBaseIsProtectedFromDeletion()
	{
		assert(IsProtectedPlacedCodsworth(false, true));
		assert(!IsProtectedPlacedCodsworth(true, true));
		assert(!IsProtectedPlacedCodsworth(false, false));
		assert(!IsProtectedPlacedCodsworth(true, false));
	}

	void TestTickGateRunsOnlyInGameplay()
	{
		assert(EvaluateTickGate(TickGateInputs{}) == TickGate::kRun);

		assert(EvaluateTickGate(TickGateInputs{ .loadingMenuOpen = true }) == TickGate::kLoadingMenu);
		assert(EvaluateTickGate(TickGateInputs{ .mainMenuOpen = true }) == TickGate::kMainMenu);
		assert(EvaluateTickGate(TickGateInputs{ .saveLoadBusy = true }) == TickGate::kSaveLoadBusy);

		assert(EvaluateTickGate(TickGateInputs{ .loadingMenuOpen = true, .mainMenuOpen = true }) == TickGate::kLoadingMenu);
		assert(EvaluateTickGate(TickGateInputs{ .mainMenuOpen = true, .saveLoadBusy = true }) == TickGate::kMainMenu);
		assert(
			EvaluateTickGate(TickGateInputs{ .loadingMenuOpen = true, .mainMenuOpen = true, .saveLoadBusy = true }) ==
			TickGate::kLoadingMenu);

		assert(std::string_view(GetTickGateLabel(TickGate::kRun)) != std::string_view(GetTickGateLabel(TickGate::kLoadingMenu)));
		assert(std::string_view(GetTickGateLabel(TickGate::kRun)) != std::string_view(GetTickGateLabel(TickGate::kMainMenu)));
		assert(std::string_view(GetTickGateLabel(TickGate::kRun)) != std::string_view(GetTickGateLabel(TickGate::kSaveLoadBusy)));
	}

	void TestSlotSaveCleanupPrefersTheLiveProxy()
	{
		assert(DecideSlotSaveCleanup(false, false) == SlotSaveCleanup::kNone);
		assert(DecideSlotSaveCleanup(false, true) == SlotSaveCleanup::kDropPendingSpawn);
		assert(DecideSlotSaveCleanup(true, false) == SlotSaveCleanup::kDropLiveProxy);
		assert(DecideSlotSaveCleanup(true, true) == SlotSaveCleanup::kDropLiveProxy);
	}

	void TestSavedProxySweepKeepsLiveAndForeignActors()
	{
		assert(DecideSavedProxySweep(true, false, false, false, false) == SavedProxySweep::kDelete);

		assert(DecideSavedProxySweep(false, false, false, false, false) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(true, true, false, false, false) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(true, false, true, false, false) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(false, true, true, false, false) == SavedProxySweep::kKeep);
	}

	void TestSavedProxySweepDeletesCreatedFallbackBaseActors()
	{
		assert(DecideSavedProxySweep(false, false, false, false, true) == SavedProxySweep::kDelete);

		assert(DecideSavedProxySweep(false, true, false, false, true) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(false, false, true, false, true) == SavedProxySweep::kKeep);
	}

	void TestSavedProxySweepKeepsThePlacedFallbackReference()
	{
		assert(DecideSavedProxySweep(true, false, false, true, false) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(true, true, false, true, false) == SavedProxySweep::kKeep);
		assert(DecideSavedProxySweep(true, false, true, true, false) == SavedProxySweep::kKeep);
	}

	void TestCreatedProxyWithTheFallbackLocalIdIsNotAPlacedFallback()
	{
		assert(IsPlacedFallbackProxy(true, false));

		assert(!IsPlacedFallbackProxy(true, true));
		assert(!IsPlacedFallbackProxy(false, true));
		assert(!IsPlacedFallbackProxy(false, false));
	}

	void TestCreatedFallbackBaseProxyNeedsCreatedNonPuppetBase()
	{
		assert(IsCreatedFallbackBaseProxy(true, true, false, true));

		assert(!IsCreatedFallbackBaseProxy(false, true, false, true));
		assert(!IsCreatedFallbackBaseProxy(true, true, true, true));
		assert(!IsCreatedFallbackBaseProxy(true, false, false, true));
		assert(!IsCreatedFallbackBaseProxy(true, true, false, false));
	}

	void TestSaveStartFallbackIgnoresLoads()
	{
		assert(IsSaveStartFallback(true, false, false));

		assert(!IsSaveStartFallback(true, false, true));
		assert(!IsSaveStartFallback(true, true, false));
		assert(!IsSaveStartFallback(true, true, true));
		assert(!IsSaveStartFallback(false, false, false));
		assert(!IsSaveStartFallback(false, false, true));
	}

	void TestProxyRetireOnlyTouchesCustomProxyActors()
	{
		assert(DecideProxyRetire(false, false, false) == ProxyRetireAction::kSkip);
		assert(DecideProxyRetire(false, true, false) == ProxyRetireAction::kDelete);
		assert(DecideProxyRetire(false, false, true) == ProxyRetireAction::kDelete);
		assert(DecideProxyRetire(false, true, true) == ProxyRetireAction::kDelete);

		assert(DecideProxyRetire(true, false, false) == ProxyRetireAction::kHoldPlacedFallback);
		assert(DecideProxyRetire(true, true, true) == ProxyRetireAction::kHoldPlacedFallback);
	}

	void TestSweepSourcePrioritySweepsThePlayerCellFirst()
	{
		std::vector<SweepSource> visited;
		VisitSweepSourcesInPriorityOrder([&visited](SweepSource a_source) {
			visited.push_back(a_source);
		});

		const std::array<SweepSource, 2> expected{ SweepSource::kPlayerCell, SweepSource::kProcessLists };
		assert(visited.size() == kSweepSourcePriority.size());
		assert(visited == std::vector<SweepSource>(expected.begin(), expected.end()));
	}

	void TestSweepPassesContinueWhileTheCapHidesCandidates()
	{
		assert(ShouldRunAnotherSweepPass(512, true));

		assert(!ShouldRunAnotherSweepPass(7, false));
		assert(!ShouldRunAnotherSweepPass(0, true));
		assert(!ShouldRunAnotherSweepPass(512, false));
	}

	void TestSaveTeardownRunsOnlyOnTheGameThread()
	{
		assert(IsSaveTeardownOnGameThread(4242, 4242));
		assert(!IsSaveTeardownOnGameThread(4243, 4242));

		assert(!IsSaveTeardownOnGameThread(4242, 0));
		assert(!IsSaveTeardownOnGameThread(0, 4242));
	}

	void TestSaveTeardownQueuesWhenTheGameThreadIsUnknown()
	{
		assert(DecideSaveTeardownRoute(4242, 4242) == SaveTeardownRoute::kRunInline);
		assert(DecideSaveTeardownRoute(4243, 4242) == SaveTeardownRoute::kQueueGameThreadTask);
		assert(DecideSaveTeardownRoute(4242, 0) == SaveTeardownRoute::kQueueGameThreadTask);
		assert(DecideSaveTeardownRoute(0, 4242) == SaveTeardownRoute::kQueueGameThreadTask);
	}

	void TestStandInRecordLengthMustBeWholeIdsWithinTheBound()
	{
		constexpr auto kMaximumBytes = static_cast<std::uint32_t>(kMaximumStandInFormIds * sizeof(std::uint32_t));
		assert(IsValidStandInRecordLength(0));
		assert(IsValidStandInRecordLength(4));
		assert(IsValidStandInRecordLength(kMaximumBytes));

		assert(!IsValidStandInRecordLength(1));
		assert(!IsValidStandInRecordLength(5));
		assert(!IsValidStandInRecordLength(kMaximumBytes + 4));
		assert(!IsValidStandInRecordLength(0xFFFFFFFF));
	}

	void TestSuccessfulLoadMessageCarriesTheBoolPayload()
	{
		assert(IsSuccessfulLoadMessageData(1));
		assert(!IsSuccessfulLoadMessageData(0));
	}

	void TestStandInIsDroppedOnlyWhenItResolvesToADeadOrUncreatedRef()
	{
		assert(ShouldKeepStandIn(false, false, false));
		assert(ShouldKeepStandIn(false, false, true));
		assert(ShouldKeepStandIn(true, true, false));

		assert(!ShouldKeepStandIn(true, true, true));
		assert(!ShouldKeepStandIn(true, false, false));
		assert(!ShouldKeepStandIn(true, false, true));
	}

	void TestStandInRecordingStopsAtTheBound()
	{
		assert(CanRecordStandIn(0));
		assert(CanRecordStandIn(kMaximumStandInFormIds - 1));

		assert(!CanRecordStandIn(kMaximumStandInFormIds));
	}

	void TestOnlyAUniqueNewCandidateIsRecordedAsAStandIn()
	{
		assert(!IsUniqueStandInCandidate(0));
		assert(IsUniqueStandInCandidate(1));
		assert(!IsUniqueStandInCandidate(2));
	}

	void TestPeriodicLogScheduleLogsTheFirstFiveThenEvery300th()
	{
		assert(ShouldLogPeriodic(1));
		assert(ShouldLogPeriodic(5));
		assert(!ShouldLogPeriodic(6));
		assert(!ShouldLogPeriodic(299));
		assert(ShouldLogPeriodic(300));
		assert(!ShouldLogPeriodic(301));
		assert(ShouldLogPeriodic(600));
	}

	void TestSessionGenerationBumpHappensOnceAndOnlyWhenConnected()
	{
		assert(ShouldBumpSessionGeneration(true, false));
		assert(!ShouldBumpSessionGeneration(true, true));
		assert(!ShouldBumpSessionGeneration(false, false));
		assert(!ShouldBumpSessionGeneration(false, true));
	}
}

int main()
{
	TestTickGateRunsOnlyInGameplay();
	TestSlotSaveCleanupPrefersTheLiveProxy();
	TestSavedProxySweepKeepsLiveAndForeignActors();
	TestSavedProxySweepDeletesCreatedFallbackBaseActors();
	TestSavedProxySweepKeepsThePlacedFallbackReference();
	TestCreatedProxyWithTheFallbackLocalIdIsNotAPlacedFallback();
	TestCreatedFallbackBaseProxyNeedsCreatedNonPuppetBase();
	TestSaveStartFallbackIgnoresLoads();
	TestProxyRetireOnlyTouchesCustomProxyActors();
	TestSweepSourcePrioritySweepsThePlayerCellFirst();
	TestSweepPassesContinueWhileTheCapHidesCandidates();
	TestSaveTeardownRunsOnlyOnTheGameThread();
	TestSaveTeardownQueuesWhenTheGameThreadIsUnknown();
	TestSessionGenerationBumpHappensOnceAndOnlyWhenConnected();
	TestStandInRecordLengthMustBeWholeIdsWithinTheBound();
	TestSuccessfulLoadMessageCarriesTheBoolPayload();
	TestStandInIsDroppedOnlyWhenItResolvesToADeadOrUncreatedRef();
	TestStandInRecordingStopsAtTheBound();
	TestOnlyAUniqueNewCandidateIsRecordedAsAStandIn();
	TestPeriodicLogScheduleLogsTheFirstFiveThenEvery300th();
	TestPlacedCodsworthBaseIsProtectedFromDeletion();

	return 0;
}
