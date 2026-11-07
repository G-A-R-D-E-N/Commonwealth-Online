#include "F4TMutationRing.h"

#include "F4TByteCodec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace
{
	using F4T::MutationRing::Capacity;
	using F4T::MutationRing::DumpHeaderSize;
	using F4T::MutationRing::DumpVersion;
	using F4T::MutationRing::Entry;
	using F4T::MutationRing::EntryWireSize;
	using TestAfterClaimHook = void (*)();

	constexpr std::array<std::byte, 4> Magic{ std::byte{ 'C' }, std::byte{ 'O' }, std::byte{ 'R' }, std::byte{ 'G' } };

	// source:CO-BETA/plugin/lib/commonlibf4/include/RE/B/BSCoreTypes.h
	// source:co-core/include/F4TMutationRing.h CO-defined ring slot
	struct Slot
	{
		std::atomic_bool busy{ false };
		std::atomic<std::uint64_t> seq{ 0 };
		std::atomic<std::uint64_t> tick{ 0 };
		std::atomic<std::uint16_t> site{ 0 };
		std::atomic<std::uint32_t> formId{ 0 };
		std::atomic<std::uint64_t> address{ 0 };
		std::atomic<std::uint32_t> aux{ 0 };
	};

	std::array<Slot, Capacity> g_slots;
	std::atomic<std::uint64_t> g_nextSeq{ 0 };
	std::atomic_bool g_enabled{ false };
	std::atomic<TestAfterClaimHook> g_afterClaimHook{ nullptr };

	bool ReadSlot(const Slot& a_slot, Entry& a_out)
	{
		const auto seq = a_slot.seq.load(std::memory_order_acquire);
		if (seq == 0) {
			return false;
		}
		a_out.seq = seq;
		a_out.tick = a_slot.tick.load(std::memory_order_relaxed);
		a_out.site = a_slot.site.load(std::memory_order_relaxed);
		a_out.formId = a_slot.formId.load(std::memory_order_relaxed);
		a_out.address = a_slot.address.load(std::memory_order_relaxed);
		a_out.aux = a_slot.aux.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		return a_slot.seq.load(std::memory_order_acquire) == seq;
	}

	bool Lapped(std::uint64_t a_seq)
	{
		return g_nextSeq.load(std::memory_order_relaxed) > a_seq + Capacity - 1;
	}
}

namespace F4T::MutationRing
{
	using F4T::ByteCodec::GetLittleEndian;
	using F4T::ByteCodec::PutLittleEndian;

	void SetEnabled(bool a_enabled)
	{
		g_enabled.store(a_enabled, std::memory_order_relaxed);
	}

	bool IsEnabled()
	{
		return g_enabled.load(std::memory_order_relaxed);
	}

	void SetTestAfterClaimHook(void (*a_hook)())
	{
		g_afterClaimHook.store(a_hook, std::memory_order_relaxed);
	}

	// constraint: source:co-core/include/F4TMutationRing.h CO-defined formId/address fields; busy claim is lock-free
	void Record(MutationSite a_site, std::uint32_t a_formId, std::uint64_t a_address, std::uint32_t a_aux)
	{
		if (!g_enabled.load(std::memory_order_relaxed)) {
			return;
		}

		for (std::size_t attempt = 0; attempt < Capacity; ++attempt) {
			const auto seq = g_nextSeq.fetch_add(1, std::memory_order_relaxed) + 1;
			auto& slot = g_slots[(seq - 1) % Capacity];
			if (slot.busy.exchange(true, std::memory_order_acquire)) {
				continue;
			}

			slot.seq.store(0, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			if (const auto hook = g_afterClaimHook.load(std::memory_order_relaxed)) {
				hook();
			}
			if (Lapped(seq)) {
				slot.busy.store(false, std::memory_order_release);
				continue;
			}

			slot.tick.store(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()), std::memory_order_relaxed);
			slot.site.store(static_cast<std::uint16_t>(a_site), std::memory_order_relaxed);
			slot.formId.store(a_formId, std::memory_order_relaxed);
			slot.address.store(a_address, std::memory_order_relaxed);
			slot.aux.store(a_aux, std::memory_order_relaxed);
			if (Lapped(seq)) {
				slot.busy.store(false, std::memory_order_release);
				continue;
			}
			slot.seq.store(seq, std::memory_order_release);
			slot.busy.store(false, std::memory_order_release);
			return;
		}
	}

	std::size_t Snapshot(std::span<Entry> a_out)
	{
		if (a_out.size() < Capacity) {
			return 0;
		}

		std::size_t count = 0;
		for (const auto& slot : g_slots) {
			if (ReadSlot(slot, a_out[count])) {
				++count;
			}
		}

		const auto ordered = a_out.first(count);
		std::sort(ordered.begin(), ordered.end(), [](const Entry& a_lhs, const Entry& a_rhs) {
			return a_lhs.seq < a_rhs.seq;
		});
		return count;
	}

	std::size_t Encode(std::span<const Entry> a_entries, std::span<std::byte> a_out)
	{
		const auto size = DumpHeaderSize + a_entries.size() * EntryWireSize;
		if (a_out.size() < size) {
			return 0;
		}

		std::size_t offset = 0;
		for (const auto magic : Magic) {
			a_out[offset++] = magic;
		}
		PutLittleEndian(a_out, offset, DumpVersion, 4);
		PutLittleEndian(a_out, offset, a_entries.size(), 4);
		for (const auto& entry : a_entries) {
			PutLittleEndian(a_out, offset, entry.seq, 8);
			PutLittleEndian(a_out, offset, entry.tick, 8);
			PutLittleEndian(a_out, offset, entry.site, 2);
			PutLittleEndian(a_out, offset, entry.formId, 4);
			PutLittleEndian(a_out, offset, entry.address, 8);
			PutLittleEndian(a_out, offset, entry.aux, 4);
		}
		return offset;
	}

	bool Decode(std::span<const std::byte> a_bytes, std::vector<Entry>& a_out)
	{
		a_out.clear();
		if (a_bytes.size() < DumpHeaderSize || !std::equal(Magic.begin(), Magic.end(), a_bytes.begin())) {
			return false;
		}

		std::size_t offset = Magic.size();
		if (GetLittleEndian(a_bytes, offset, 4) != DumpVersion) {
			return false;
		}
		const auto count = GetLittleEndian(a_bytes, offset, 4);
		if (count > Capacity || a_bytes.size() - DumpHeaderSize < count * EntryWireSize) {
			return false;
		}

		a_out.reserve(count);
		for (std::uint64_t index = 0; index < count; ++index) {
			Entry entry;
			entry.seq = GetLittleEndian(a_bytes, offset, 8);
			entry.tick = GetLittleEndian(a_bytes, offset, 8);
			entry.site = static_cast<std::uint16_t>(GetLittleEndian(a_bytes, offset, 2));
			entry.formId = static_cast<std::uint32_t>(GetLittleEndian(a_bytes, offset, 4));
			entry.address = GetLittleEndian(a_bytes, offset, 8);
			entry.aux = static_cast<std::uint32_t>(GetLittleEndian(a_bytes, offset, 4));
			a_out.push_back(entry);
		}
		return true;
	}
}
