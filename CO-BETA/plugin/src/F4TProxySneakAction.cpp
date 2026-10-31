#include "pch.h"

#include "F4TProxySneakAction.h"

#include "F4TDebugLogging.h"
#include "RE/A/Actor.h"
#include "RE/B/BGSAction.h"
#include "RE/B/BGSDefaultObjectManager.h"
#include "RE/D/DEFAULT_OBJECT.h"

namespace F4T::ProxySneakAction
{
	namespace
	{
		constexpr auto kSneakActionRetryInterval = std::chrono::milliseconds(500);

		const char* GetOutcomeLabel(SneakRequestOutcome a_outcome)
		{
			switch (a_outcome) {
			case SneakRequestOutcome::kSettled:
				return "settled";
			case SneakRequestOutcome::kPending:
				return "pending";
			case SneakRequestOutcome::kUseActionFallback:
				return "actionFallback";
			case SneakRequestOutcome::kGiveUp:
				return "giveUp";
			default:
				return "unknown";
			}
		}
	}

	bool RequestProxySneakState(
		SneakRequestState& a_state,
		RE::Actor& a_proxy,
		bool a_requestedSneaking,
		bool a_allowActionFallback)
	{
		const auto actorId = a_proxy.GetFormID();
		const auto now = std::chrono::steady_clock::now();
		const auto requestChanged = a_state.lastRequested != a_requestedSneaking;
		if (requestChanged) {
			a_state.lastRequested = a_requestedSneaking;
			a_state.nextAttemptTime = {};
			a_state.actionAttempts = 0;
			a_state.lastOutcome.reset();
			ArmSneakRetryWindow(now, a_state.nextAttemptTime, kSneakActionRetryInterval);
		}

		const auto traceByCount = ShouldTraceCount(++a_state.traceCount);
		const auto trace = requestChanged || traceByCount;

		const auto stanceBefore = a_proxy.IsSneaking();
		const auto setCalled = stanceBefore != a_requestedSneaking;
		// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/proxy-crouch-co143/README.md
		const auto setAccepted = setCalled && a_proxy.SetSneaking(a_requestedSneaking);
		const auto stanceAfterSet = a_proxy.IsSneaking();
		if (setCalled && trace) {
			F4T::DebugLogging::Log(
				"ProxySneak",
				"[RT-VERIFY 364-B] proxy SetSneaking actor={:08X}: requested={}, IsSneaking before={}, returned={}, IsSneaking after={}.",
				actorId,
				a_requestedSneaking,
				stanceBefore,
				setAccepted,
				stanceAfterSet);
		}

		auto outcome = ResolveSneakRequest(
			stanceAfterSet == a_requestedSneaking,
			now < a_state.nextAttemptTime,
			a_state.actionAttempts);
		if (!a_allowActionFallback && outcome == SneakRequestOutcome::kUseActionFallback) {
			outcome = SneakRequestOutcome::kPending;
		}

		if (outcome == SneakRequestOutcome::kUseActionFallback) {
			ArmSneakRetryWindow(now, a_state.nextAttemptTime, kSneakActionRetryInterval);
			++a_state.actionAttempts;
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/proxy-crouch-co143/README.md
			auto* defaultObjects = RE::BGSDefaultObjectManager::GetSingleton();
			auto* action = defaultObjects ? defaultObjects->GetDefaultObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionSneak) : nullptr;
			if (!action) {
				F4T::DebugLogging::Log(
					"ProxySneak",
					"[RT-VERIFY 364-B] PerformAction(kActionSneak) fallback skipped actor={:08X}: requested={}, default object is missing, IsSneaking={}, attempt {}/{}.",
					actorId,
					a_requestedSneaking,
					stanceAfterSet,
					a_state.actionAttempts,
					kMaxActionAttemptsPerRequest);
			} else {
				const auto accepted = a_proxy.PerformAction(action, nullptr);
				F4T::DebugLogging::Log(
					"ProxySneak",
					"[RT-VERIFY 364-B] PerformAction(kActionSneak) fallback actor={:08X}: requested={}, IsSneaking before={}, accepted={}, IsSneaking after={}, attempt {}/{}.",
					actorId,
					a_requestedSneaking,
					stanceAfterSet,
					accepted,
					a_proxy.IsSneaking(),
					a_state.actionAttempts,
					kMaxActionAttemptsPerRequest);
			}
		}

		const auto outcomeChanged = !a_state.lastOutcome || *a_state.lastOutcome != outcome;
		if (trace || outcomeChanged) {
			F4T::DebugLogging::Log(
				"ProxySneak",
				"[RT-VERIFY 364-B] proxy sneak outcome={} actor={:08X}: requested={}, IsSneaking={}, actionAttempts={}/{}.",
				GetOutcomeLabel(outcome),
				actorId,
				a_requestedSneaking,
				stanceAfterSet,
				a_state.actionAttempts,
				kMaxActionAttemptsPerRequest);
		}
		a_state.lastOutcome = outcome;

		return outcome == SneakRequestOutcome::kSettled;
	}
}
