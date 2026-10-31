#include "pch.h"

#include "F4TContainerChangedHub.h"
#include "F4TDebugLogging.h"
#include "RE/T/TESContainerChangedEvent.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

namespace F4T::ContainerChangedHub
{
	namespace
	{
		std::array<ContainerChange, kRingCapacity> g_ring{};
		std::size_t g_head{};
		std::size_t g_size{};
		std::mutex g_ringMutex;
		std::atomic<std::uint64_t> g_dropped{};
		std::atomic<std::uint32_t> g_gameThread{};
		bool g_registered{};
		bool g_draining{};
		bool g_diagnosticsEnabled{ true };
		std::vector<Callback> g_subscribers;
		std::uint64_t g_logWindowStart{};
		std::uint32_t g_logLines{};

		std::uint64_t MonotonicMs() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		bool OnGameThread() noexcept
		{
			return g_gameThread.load(std::memory_order_acquire) == REX::W32::GetCurrentThreadId();
		}

		void Enqueue(const ContainerChange& a_change)
		{
			std::unique_lock lock(g_ringMutex, std::try_to_lock);
			if (!lock.owns_lock() || g_size >= kRingCapacity) {
				g_dropped.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			g_ring[(g_head + g_size) % kRingCapacity] = a_change;
			++g_size;
		}

		class ContainerChangedSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESContainerChangedEvent& a_event,
				RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				Enqueue(ContainerChange{
					a_event.oldContainerFormID,
					a_event.newContainerFormID,
					a_event.baseObjectFormID,
					a_event.itemCount,
					a_event.referenceFormID,
					a_event.uniqueID,
					REX::W32::GetCurrentThreadId(),
					MonotonicMs() });
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		ContainerChangedSink g_sink;

		void LogDiagnostic(const ContainerChange& a_change)
		{
			if (!g_diagnosticsEnabled) {
				return;
			}
			const auto now = MonotonicMs();
			if (now - g_logWindowStart >= 1000) {
				g_logWindowStart = now;
				g_logLines = 0;
			}
			if (g_logLines >= kDiagnosticLinesPerSecond) {
				return;
			}
			++g_logLines;
			DebugLogging::Log("ContainerChangedHub",
				"old={:08X} new={:08X} base={:08X} count={} ref={:08X} unique={} thread={} ms={} dropped={}",
				a_change.oldContainer, a_change.newContainer, a_change.baseObj, a_change.itemCount,
				a_change.referenceFormID, a_change.uniqueID, a_change.threadId, a_change.monotonicMs, DroppedEvents());
		}
	}

	void Install()
	{
		std::uint32_t expected = 0;
		g_gameThread.compare_exchange_strong(expected, REX::W32::GetCurrentThreadId(), std::memory_order_acq_rel);
		if (!OnGameThread() || g_registered) {
			return;
		}
		if (auto* source = RE::TESContainerChangedEvent::GetEventSource()) {
			source->RegisterSink(&g_sink);
			g_registered = true;
		}
	}

	void Shutdown()
	{
		if (!g_registered) {
			return;
		}
		if (auto* source = RE::TESContainerChangedEvent::GetEventSource()) {
			source->UnregisterSink(&g_sink);
			g_registered = false;
		}
	}

	bool Subscribe(Callback a_callback)
	{
		if (!OnGameThread() || !a_callback || g_subscribers.size() >= kMaximumSubscribers) {
			return false;
		}
		g_subscribers.push_back(std::move(a_callback));
		return true;
	}

	void Drain(const Callback& a_callback)
	{
		if (!OnGameThread() || g_draining) {
			return;
		}
		g_draining = true;
		struct DrainGuard { ~DrainGuard() { g_draining = false; } } guard;
		std::array<ContainerChange, kRingCapacity> batch;
		std::size_t count{};
		{
			const std::scoped_lock lock(g_ringMutex);
			count = g_size;
			for (std::size_t i = 0; i < count; ++i) {
				batch[i] = g_ring[(g_head + i) % kRingCapacity];
			}
			g_head = (g_head + count) % kRingCapacity;
			g_size = 0;
		}
		if (count == 0) {
			return;
		}
		const auto subscribers = g_subscribers;
		for (std::size_t i = 0; i < count; ++i) {
			LogDiagnostic(batch[i]);
			if (a_callback) {
				a_callback(batch[i]);
			}
			for (const auto& subscriber : subscribers) {
				subscriber(batch[i]);
			}
		}
	}

	void SetDiagnosticsEnabled(bool a_enabled)
	{
		if (OnGameThread()) {
			g_diagnosticsEnabled = a_enabled;
		}
	}

	std::uint64_t DroppedEvents() noexcept
	{
		return g_dropped.load(std::memory_order_relaxed);
	}
}
