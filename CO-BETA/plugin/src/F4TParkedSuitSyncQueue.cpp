#include "pch.h"

#include "F4TParkedSuitSyncInternal.h"

#include "F4TParkedSuitInternal.h"
#include "F4TParkedSuitUi.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace F4T::ParkedSuitSync::detail
{
	namespace
	{
		constexpr auto kAckTimeout = std::chrono::seconds{ 5 };
		constexpr std::size_t kMaximumAttempts = 5;
		constexpr auto kParkBackoffCap = std::chrono::seconds{ 60 };
		constexpr std::size_t kMaximumParkBackoffDoublings = 5;
		std::uint32_t g_nonce{};

		void ForgetAdoption(const PendingTransition& a_transition)
		{
			if (a_transition.op != "adopt") {
				return;
			}
			if (const auto suitId = ParseHexId(a_transition.packet, "suitId")) {
				g_adopting.erase(*suitId);
			}
		}

		void AbandonPark(const PendingTransition& a_transition)
		{
			if (a_transition.consoleSpawned && !a_transition.nacked) {
				g_consoleUnsent.insert(a_transition.localId);
			}
		}

		void RememberDroppedPark(const PendingTransition& a_transition)
		{
			if (a_transition.op == "park" && !a_transition.consoleSpawned && !IsDefinitiveRefusal(a_transition.refusal)) {
				g_droppedParks[a_transition.localId] = a_transition.packet;
			}
		}

		// Constraint: a queued park is superseded by a newer park or remove for the same frame only while it is not in flight.
		void SupersedeQueuedParks(std::uint32_t a_localId)
		{
			std::erase_if(g_pending, [&](const PendingTransition& a_pending) {
				return a_pending.op == "park" && a_pending.localId == a_localId && !a_pending.inFlight;
			});
		}

		// Constraint: only the front transition is in flight; it leaves the queue on an ok ack, is resent after a nack or a timeout, and a remove or adopt is dropped after kMaximumAttempts sends. A park never runs out of attempts: it backs off, yields to later transitions, and leaves the queue only on a definitive refusal.
		void FlushPendingLocked(std::vector<std::uint32_t>& a_failedRemovals)
		{
			const std::scoped_lock lock(g_mutex);
			if (!F4T::Networking::IsConnectedToServer()) {
				RetainParksLocked();
				return;
			}
			std::size_t yields{};
			while (!g_pending.empty()) {
				auto& front = g_pending.front();
				const auto now = std::chrono::steady_clock::now();
				if (front.inFlight) {
					if (now < front.nextSend) {
						return;
					}
					if (front.staleWorld) {
						g_pending.pop_front();
						continue;
					}
					front.inFlight = false;
					front.nextSend = now + NackDelay(front);
				}
				if (front.attempts >= kMaximumAttempts && front.op != "park") {
					F4T::DebugLogging::Log("ParkedSuitSync", "dropped {} for localId {:08X} after {} attempts", front.op, front.localId, front.attempts);
					if (front.op == "remove") a_failedRemovals.push_back(front.localId);
					ForgetAdoption(front);
					AbandonPark(front);
					g_pending.pop_front();
					continue;
				}
				if (now < front.nextSend) {
					if (front.op == "park" && yields < g_pending.size()) {
						g_pending.push_back(std::move(front));
						g_pending.pop_front();
						++yields;
						continue;
					}
					return;
				}
				++front.attempts;
				if (!F4T::Networking::SendParkedSuitPacket(front.packet)) {
					front.nextSend = now + kRetryDelay;
					return;
				}
				front.inFlight = true;
				front.nextSend = now + kAckTimeout;
				return;
			}
		}

	}

	std::unordered_map<std::uint32_t, nlohmann::json> g_droppedParks;

	void RetainParksLocked()
	{
		for (auto transition = g_pending.begin(); transition != g_pending.end();) {
			if (transition->op == "park" && !transition->staleWorld) {
				transition->inFlight = false;
				transition->nextSend = {};
				++transition;
				continue;
			}
			ForgetAdoption(*transition);
			AbandonPark(*transition);
			transition = g_pending.erase(transition);
		}
	}

	void DiscardParksLocked()
	{
		for (auto transition = g_pending.begin(); transition != g_pending.end();) {
			if (transition->op != "park") {
				++transition;
				continue;
			}
			if (transition->inFlight) {
				transition->staleWorld = true;
				++transition;
				continue;
			}
			AbandonPark(*transition);
			transition = g_pending.erase(transition);
		}
	}

	std::chrono::seconds NackDelay(const PendingTransition& a_transition)
	{
		if (a_transition.op != "park") {
			return kRetryDelay;
		}
		const auto doublings = std::min(a_transition.attempts, kMaximumParkBackoffDoublings);
		return std::min(kRetryDelay * (std::int64_t{ 1 } << doublings), kParkBackoffCap);
	}

	void Enqueue(PendingTransition a_transition)
	{
		std::optional<std::uint32_t> droppedRemoval;
		{
			const std::scoped_lock lock(g_mutex);
			g_droppedParks.erase(a_transition.localId);
			if (a_transition.op == "park" || a_transition.op == "remove") {
				SupersedeQueuedParks(a_transition.localId);
			}
			a_transition.packet["nonce"] = ++g_nonce;
			if (g_pending.size() >= kMaximumPendingTransitions) {
				auto victim = std::find_if(g_pending.begin(), g_pending.end(), [&](const PendingTransition& a_pending) {
					return a_pending.op != "remove" && a_pending.op != "adopt" && std::none_of(g_pending.begin(), g_pending.end(), [&](const PendingTransition& a_other) { return a_other.op == "remove" && a_other.localId == a_pending.localId; });
				});
				if (victim == g_pending.end()) {
					if (a_transition.op == "remove") {
						droppedRemoval = a_transition.localId;
					}
					AbandonPark(a_transition);
					RememberDroppedPark(a_transition);
					ForgetAdoption(a_transition);
				} else {
					if (victim->op == "remove") {
						droppedRemoval = victim->localId;
					}
					AbandonPark(*victim);
					RememberDroppedPark(*victim);
					g_pending.erase(victim);
					g_pending.push_back(std::move(a_transition));
				}
			} else {
				g_pending.push_back(std::move(a_transition));
			}
		}
		if (droppedRemoval) {
			F4T::ParkedSuitUi::NotifyRemovalFailed(*droppedRemoval);
		}
	}

	void FlushPending()
	{
		std::vector<std::uint32_t> failedRemovals;
		FlushPendingLocked(failedRemovals);
		for (const auto localId : failedRemovals) {
			F4T::ParkedSuitUi::NotifyRemovalFailed(localId);
		}
	}
}
