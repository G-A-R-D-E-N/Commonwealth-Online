#include "pch.h"

#include "F4TInventoryContainerSync.h"

#include "F4TContainerChangedHub.h"
#include "F4TDropLedger.h"
#include "F4TFormIdentity.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryDropGate.h"
#include "F4TInventoryOperations.h"
#include "F4TInventoryWire.h"
#include "F4TNetworkingInternal.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace F4T::InventoryContainerSync
{
	namespace
	{
		constexpr std::size_t kMaximumQueued = 32;
		constexpr std::uint32_t kMaximumSends = 4;
		constexpr std::uint64_t kMinimumSendIntervalMs = 300;
		constexpr std::uint32_t kMaximumResends = 4;
		constexpr std::uint64_t kGiveUpMs = 60000;
		constexpr std::uint64_t kSlowResendMs = 30000;
		constexpr std::uint64_t kTakeGiveUpMs = 24ull * 60 * 60 * 1000;
		constexpr std::size_t kMaximumCarried = 32;

		struct Queued
		{
			bool take{};
			std::string cellId;
			std::string refId;
			std::string itemKey;
			std::uint32_t count{};
			std::uint32_t sends{};
			std::string operationId;
			nlohmann::json request;
		};

		struct InFlight
		{
			std::string operationId;
			std::uint64_t sentMs{};
			std::uint64_t firstSentMs{};
			Queued move;
			nlohmann::json request;
			std::uint32_t resends{};
			bool resendPending{};
			std::uint64_t notBeforeMs{};
		};

		std::mutex g_mutex;
		bool g_active{};
		std::string g_nonce;
		std::string g_identity;
		std::uint32_t g_counter{};
		std::uint64_t g_nextSendMs{};
		std::deque<Queued> g_queue;
		std::optional<InFlight> g_inFlight;
		std::string g_carryIdentity;
		std::deque<Queued> g_carry;
		std::int64_t g_carrySavedMs{};

		std::int64_t WallMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		std::uint64_t GiveUpAfterMs(const InFlight& a_flight)
		{
			return a_flight.move.take ? kTakeGiveUpMs : kGiveUpMs;
		}

		bool SameIdRetry(const std::string& a_reason)
		{
			return a_reason == "rateLimited" || a_reason == "inventoryPersistenceUnavailable";
		}

		void LogGaveUp(const InFlight& a_flight)
		{
			F4T::Networking::Detail::LogThrottledWarning("inventory_container_gave_up", std::format("A container move got no confirmed answer and was given up. {} of {} from {} in {}. The owned inventory snapshot reconciles it.",
				a_flight.move.take ? "Take" : "Put", a_flight.move.itemKey, a_flight.move.refId, a_flight.move.cellId));
		}

		bool ScheduleResend(std::uint64_t a_nowMs)
		{
			if ((!g_inFlight->move.take && g_inFlight->resends >= kMaximumResends) || a_nowMs - g_inFlight->firstSentMs >= GiveUpAfterMs(*g_inFlight)) {
				LogGaveUp(*g_inFlight);
				g_inFlight.reset();
				return false;
			}
			g_inFlight->resendPending = true;
			g_inFlight->notBeforeMs = g_inFlight->move.take && g_inFlight->resends >= kMaximumResends ? a_nowMs + kSlowResendMs : 0;
			return true;
		}

		void CarryLocked()
		{
			std::deque<Queued> carried;
			std::size_t droppedPuts = 0;
			if (g_inFlight) {
				if (g_inFlight->move.take) {
					auto move = std::move(g_inFlight->move);
					move.operationId = g_inFlight->operationId;
					move.request = g_inFlight->request;
					carried.push_back(std::move(move));
				} else {
					++droppedPuts;
				}
			}
			for (auto& queued : g_queue) {
				if (queued.take) {
					carried.push_back(std::move(queued));
				} else {
					++droppedPuts;
				}
			}
			g_queue.clear();
			g_inFlight.reset();
			if (droppedPuts != 0) {
				F4T::Networking::Detail::LogThrottledWarning("inventory_container_discarded", std::format("The session ended with {} container puts unsent or unanswered. The owned inventory snapshot reconciles them.", droppedPuts));
			}
			if (carried.empty() || g_identity.empty()) {
				if (!carried.empty()) {
					F4T::Networking::Detail::LogThrottledWarning("inventory_container_carry_lost", std::format("{} container takes could not be carried to the next session: the server identity is unknown.", carried.size()));
				}
				return;
			}
			if (g_carryIdentity != g_identity) {
				g_carry.clear();
			}
			g_carryIdentity = g_identity;
			for (auto& move : carried) {
				if (g_carry.size() >= kMaximumCarried) {
					F4T::Networking::Detail::LogThrottledWarning("inventory_container_carry_full", "Too many container takes are waiting to be carried over. A take was dropped. The owned inventory snapshot reconciles it.");
					continue;
				}
				g_carry.push_back(std::move(move));
			}
			g_carrySavedMs = WallMs();
		}

		void OnContainerChange(const F4T::ContainerChangedHub::ContainerChange& a_change)
		{
			if (a_change.barterMenuOpen || a_change.itemCount <= 0) {
				return;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto playerId = player->GetFormID();
			const bool take = a_change.oldContainer != 0 && a_change.newContainer == playerId;
			const bool put = a_change.oldContainer == playerId && a_change.newContainer != 0;
			if (!take && !put) {
				return;
			}
			const auto containerId = take ? a_change.oldContainer : a_change.newContainer;
			auto* container = RE::TESForm::GetFormByID<RE::TESObjectREFR>(containerId);
			if (!container || container->As<RE::Actor>()) {
				return;
			}
			const auto* cell = container->GetParentCell();
			const auto* item = RE::TESForm::GetFormByID(a_change.baseObj);
			if (!cell || !item || F4T::FormIdentity::SplitFormId(containerId).kind == F4T::FormIdentity::Kind::Runtime) {
				return;
			}
			const auto itemKey = F4T::InventoryDropGate::TranslateItemKey(item);
			if (!itemKey) {
				return;
			}
			const std::scoped_lock lock(g_mutex);
			if (!g_active) {
				return;
			}
			Queued move{ take, std::format("{:08X}", cell->GetFormID()), std::format("{:08X}", containerId), *itemKey,
				static_cast<std::uint32_t>(a_change.itemCount) };
			if (move.take) {
				for (auto it = g_queue.rbegin(); it != g_queue.rend(); ++it) {
					if (it->cellId == move.cellId && it->refId == move.refId && it->itemKey == move.itemKey) {
						if (it->take && it->operationId.empty() && static_cast<std::uint64_t>(it->count) + move.count <= F4T::Drops::kMaximumDropCount) {
							it->count += move.count;
							return;
						}
						break;
					}
				}
				g_queue.push_back(std::move(move));
				return;
			}
			if (!g_queue.empty()) {
				auto& tail = g_queue.back();
				if (!tail.take && tail.cellId == move.cellId && tail.refId == move.refId && tail.itemKey == move.itemKey &&
					static_cast<std::uint64_t>(tail.count) + move.count <= F4T::Drops::kMaximumDropCount) {
					tail.count += move.count;
					return;
				}
			}
			const auto queuedPuts = std::count_if(g_queue.begin(), g_queue.end(), [](const Queued& a_queued) { return !a_queued.take; });
			if (static_cast<std::size_t>(queuedPuts) >= kMaximumQueued) {
				F4T::Networking::Detail::LogThrottledWarning("inventory_container_overflow", std::format("Too many container moves are queued. Dropped put of {} from {} in {}. The owned inventory snapshot reconciles the dropped moves.", move.itemKey, move.refId, move.cellId));
				return;
			}
			g_queue.push_back(std::move(move));
		}

		void EnsureSubscribed()
		{
			static const bool subscribed = F4T::ContainerChangedHub::Subscribe(OnContainerChange);
			if (!subscribed) {
				F4T::Networking::Detail::LogThrottledWarning("inventory_container_subscribe", "Container moves are not tracked: the container change hub is full.");
			}
		}
	}

	void Activate(const std::string& a_sessionNonce, const std::string& a_serverKey, const std::string& a_profile)
	{
		const std::scoped_lock lock(g_mutex);
		CarryLocked();
		const auto identity = a_serverKey.empty() ? std::string{} : a_serverKey + "|" + a_profile;
		g_active = true;
		g_nonce = a_sessionNonce;
		g_identity = identity;
		g_counter = 0;
		if (g_carry.empty()) {
			return;
		}
		const bool expired = WallMs() - g_carrySavedMs >= static_cast<std::int64_t>(kTakeGiveUpMs);
		if (expired) {
			F4T::Networking::Detail::LogThrottledWarning("inventory_container_carry_expired", std::format("{} carried container takes were older than 24 hours and were dropped.", g_carry.size()));
		} else if (!identity.empty() && g_carryIdentity == identity) {
			g_queue = std::move(g_carry);
		} else {
			return;
		}
		g_carry.clear();
		g_carryIdentity.clear();
	}

	void Reset()
	{
		const std::scoped_lock lock(g_mutex);
		CarryLocked();
		g_active = false;
		g_identity.clear();
	}

	bool Busy()
	{
		const std::scoped_lock lock(g_mutex);
		return g_inFlight.has_value() || !g_queue.empty();
	}

	std::optional<nlohmann::json> Pump(std::uint64_t a_nowMs, bool a_quiet, std::uint32_t a_revision)
	{
		EnsureSubscribed();
		const std::scoped_lock lock(g_mutex);
		if (g_inFlight && a_nowMs - g_inFlight->firstSentMs >= GiveUpAfterMs(*g_inFlight)) {
			LogGaveUp(*g_inFlight);
			g_inFlight.reset();
		}
		if (g_inFlight && !g_inFlight->resendPending && g_inFlight->sentMs + F4T::InventoryOps::kOperationTimeoutMs <= a_nowMs) {
			F4T::Networking::Detail::LogThrottledWarning("inventory_container_timeout", "A container move got no reply in time and is resent under the same operation id.");
			(void)ScheduleResend(a_nowMs);
		}
		if (!g_active || !a_quiet || a_nowMs < g_nextSendMs) {
			return std::nullopt;
		}
		if (g_inFlight) {
			if (!g_inFlight->resendPending || a_nowMs < g_inFlight->notBeforeMs) {
				return std::nullopt;
			}
			++g_inFlight->resends;
			g_inFlight->resendPending = false;
			g_inFlight->sentMs = a_nowMs;
			g_nextSendMs = a_nowMs + kMinimumSendIntervalMs;
			return g_inFlight->request;
		}
		if (g_queue.empty()) {
			return std::nullopt;
		}
		auto move = std::move(g_queue.front());
		g_queue.pop_front();
		++move.sends;
		std::string operationId;
		std::optional<nlohmann::json> request;
		if (move.operationId.empty()) {
			operationId = std::format("{}-c{}", g_nonce, ++g_counter);
			request = F4T::InventoryContract::BuildContainerRequest(operationId, a_revision, move.take ? "take" : "put", move.cellId, move.refId,
				move.itemKey, move.count);
		} else {
			operationId = move.operationId;
			request = move.request;
		}
		if (!request) {
			F4T::Networking::Detail::LogThrottledWarning("inventory_container_build", "A container move could not be built and was dropped.");
			return std::nullopt;
		}
		g_nextSendMs = a_nowMs + kMinimumSendIntervalMs;
		g_inFlight = InFlight{ operationId, a_nowMs, a_nowMs, std::move(move), *request };
		return request;
	}

	std::optional<Settled> NoteResult(const nlohmann::json& a_packet)
	{
		const std::scoped_lock lock(g_mutex);
		const auto result = F4T::InventoryWire::NormalizeContainerResult(a_packet);
		if (!result || !g_inFlight || result->value("operationId", std::string{}) != g_inFlight->operationId) {
			return std::nullopt;
		}
		const bool committed = result->value("status", std::string{}) == "committed";
		const auto reason = result->value("reason", std::string{});
		const Settled settled{ committed, result->value("revision", 0u), reason };
		if (!committed && SameIdRetry(reason)) {
			(void)ScheduleResend(g_inFlight->sentMs);
			return settled;
		}
		auto move = std::move(g_inFlight->move);
		g_inFlight.reset();
		if (!committed && reason == "staleRevision" && move.sends < kMaximumSends) {
			move.operationId.clear();
			move.request = nlohmann::json();
			g_queue.push_front(std::move(move));
		}
		return settled;
	}

	void Abandon()
	{
		const std::scoped_lock lock(g_mutex);
		if (g_inFlight) {
			(void)ScheduleResend(g_inFlight->sentMs);
		}
	}
}
