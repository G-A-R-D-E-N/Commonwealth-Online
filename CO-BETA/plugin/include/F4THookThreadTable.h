#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#if !defined(_WINBASE_)
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
#endif
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace F4T::HookThread
{
	enum class Id : std::uint16_t
	{
		ExecuteCommand = 1,
		CompileAndRun = 2,
		LaunchDispatch = 3,
		SaveGameImpl = 4,
		IsSavingAllowed = 5,
		DropObject = 6,
		PickUpObject = 7,
		DoItemTransfer = 8,
		HandleHealthDamage = 9,
		Kill = 10,
		ProxyUpdate = 11,
		HitFrame = 12,
		WeaponFire = 13,
		Place = 14,
		UpdateActiveEdit = 15,
		ScrapAccept = 16,
		ConfirmCall = 17,
		ProxyActivate = 18,
		ProxyGetActivateText = 19,
		LiveFireGiveBack = 20,
		PlayerAnimEventCapture = 21,
		AimRedirect = 22,
	};

	constexpr std::size_t kCapacity = 64;
	constexpr std::uint64_t kReportRepeatIntervalMs = 0;

	struct Row
	{
		std::uint16_t hookId{};
		std::uint32_t threadId{};
		std::uint32_t count{};
	};

	struct Slot
	{
		std::atomic<std::uint64_t> pair{ 0 };
		std::atomic<std::uint32_t> count{ 0 };
		std::atomic<std::uint32_t> announced{ 0 };
	};

	inline Slot g_slots[kCapacity]{};
	inline std::atomic<std::uint64_t> g_lastReportMs{ 0 };

	inline std::uint32_t CurrentThreadId() noexcept
	{
#if defined(_WIN32)
		return static_cast<std::uint32_t>(::GetCurrentThreadId());
#else
		return static_cast<std::uint32_t>(::syscall(SYS_gettid));
#endif
	}

	inline void Reset() noexcept
	{
		for (auto& slot : g_slots) {
			slot.pair.store(0, std::memory_order_relaxed);
			slot.count.store(0, std::memory_order_relaxed);
			slot.announced.store(0, std::memory_order_relaxed);
		}
		g_lastReportMs.store(0, std::memory_order_relaxed);
	}

	inline void NoteOnThread(Id a_id, std::uint32_t a_threadId) noexcept
	{
		const auto hookId = static_cast<std::uint32_t>(a_id);
		if (hookId == 0 || a_threadId == 0) {
			return;
		}
		const auto key = (static_cast<std::uint64_t>(a_threadId) << 32) | hookId;
		for (auto& slot : g_slots) {
			auto current = slot.pair.load(std::memory_order_acquire);
			if (current == key) {
				slot.count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (current != 0) {
				continue;
			}
			if (slot.pair.compare_exchange_strong(current, key, std::memory_order_acq_rel, std::memory_order_acquire)) {
				slot.count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (current == key) {
				slot.count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
	}

	inline void Note(Id a_id) noexcept
	{
		NoteOnThread(a_id, CurrentThreadId());
	}

	inline std::size_t Snapshot(Row* a_out, std::size_t a_capacity) noexcept
	{
		if (a_out == nullptr || a_capacity == 0) {
			return 0;
		}
		std::size_t written = 0;
		for (const auto& slot : g_slots) {
			const auto key = slot.pair.load(std::memory_order_acquire);
			if (key == 0 || written >= a_capacity) {
				continue;
			}
			a_out[written].hookId = static_cast<std::uint16_t>(key & 0xffffffffu);
			a_out[written].threadId = static_cast<std::uint32_t>(key >> 32);
			a_out[written].count = slot.count.load(std::memory_order_relaxed);
			++written;
		}
		return written;
	}

	inline bool AppendText(char* a_out, std::size_t a_capacity, std::size_t& a_used, const char* a_text) noexcept
	{
		const auto length = std::strlen(a_text);
		if (a_used + length + 1 > a_capacity) {
			return false;
		}
		std::memcpy(a_out + a_used, a_text, length);
		a_used += length;
		a_out[a_used] = '\0';
		return true;
	}

	inline bool TakeReport(std::uint64_t a_nowMs, std::uint64_t a_intervalMs, char* a_out, std::size_t a_capacity) noexcept
	{
		if (a_out == nullptr || a_capacity == 0) {
			return false;
		}
		a_out[0] = '\0';
		const auto last = g_lastReportMs.load(std::memory_order_acquire);
		const bool due = a_intervalMs > 0 && last != 0 && a_nowMs >= last && (a_nowMs - last) >= a_intervalMs;
		bool anyNew = false;
		for (const auto& slot : g_slots) {
			if (slot.pair.load(std::memory_order_acquire) != 0 && slot.announced.load(std::memory_order_acquire) == 0 &&
				slot.count.load(std::memory_order_acquire) > 0) {
				anyNew = true;
				break;
			}
		}
		if (!anyNew && !due) {
			return false;
		}

		std::size_t used = 0;
		bool wrote = false;
		for (auto& slot : g_slots) {
			const auto key = slot.pair.load(std::memory_order_acquire);
			const auto seen = slot.announced.load(std::memory_order_acquire);
			const auto count = slot.count.load(std::memory_order_acquire);
			if (key == 0 || count == 0 || (seen != 0 && !due)) {
				continue;
			}
			char chunk[80]{};
			std::snprintf(
				chunk,
				sizeof(chunk),
				"hook %u thread %u count %u\n",
				static_cast<unsigned>(key & 0xffffffffu),
				static_cast<unsigned>(key >> 32),
				static_cast<unsigned>(count));
			if (!AppendText(a_out, a_capacity, used, chunk)) {
				break;
			}
			slot.announced.store(count, std::memory_order_release);
			wrote = true;
		}
		if (!wrote) {
			return false;
		}
		g_lastReportMs.store(a_nowMs, std::memory_order_release);
		return true;
	}
}
