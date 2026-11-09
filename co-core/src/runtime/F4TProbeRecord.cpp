#include "F4TProbeRecord.h"

#include "F4TByteCodec.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
	using F4T::ProbeRecord::Kind;
	using F4T::ProbeRecord::MaxArgs;
	using F4T::ProbeRecord::Record;
	using F4T::ProbeRecord::WireSize;

	bool IsValidKind(std::uint8_t a_kind)
	{
		return a_kind <= static_cast<std::uint8_t>(Kind::Snapshot);
	}
}

namespace F4T::ProbeRecord
{
	std::size_t Encode(const Record& a_record, std::span<std::byte> a_out)
	{
		if (a_out.size() < WireSize || a_record.argCount > MaxArgs || !IsValidKind(static_cast<std::uint8_t>(a_record.kind))) {
			return 0;
		}

		std::size_t offset = 0;
		ByteCodec::PutLittleEndian(a_out, offset, Version, 1);
		ByteCodec::PutLittleEndian(a_out, offset, a_record.probeId, 2);
		ByteCodec::PutLittleEndian(a_out, offset, static_cast<std::uint8_t>(a_record.kind), 1);
		ByteCodec::PutLittleEndian(a_out, offset, a_record.threadId, 4);
		ByteCodec::PutLittleEndian(a_out, offset, a_record.tickMicros, 8);
		ByteCodec::PutLittleEndian(a_out, offset, a_record.callerRva, 8);
		for (const auto arg : a_record.args) {
			ByteCodec::PutLittleEndian(a_out, offset, arg, 8);
		}
		ByteCodec::PutLittleEndian(a_out, offset, a_record.retval, 8);
		ByteCodec::PutLittleEndian(a_out, offset, a_record.argCount, 1);
		ByteCodec::PutLittleEndian(a_out, offset, 0, 1);
		return offset;
	}

	bool Decode(std::span<const std::byte> a_bytes, Record& a_out)
	{
		if (a_bytes.size() != WireSize) {
			return false;
		}

		std::size_t offset = 0;
		if (ByteCodec::GetLittleEndian(a_bytes, offset, 1) != Version) {
			return false;
		}

		a_out.probeId = static_cast<std::uint16_t>(ByteCodec::GetLittleEndian(a_bytes, offset, 2));
		const auto kind = static_cast<std::uint8_t>(ByteCodec::GetLittleEndian(a_bytes, offset, 1));
		if (!IsValidKind(kind)) {
			return false;
		}
		a_out.kind = static_cast<Kind>(kind);
		a_out.threadId = static_cast<std::uint32_t>(ByteCodec::GetLittleEndian(a_bytes, offset, 4));
		a_out.tickMicros = ByteCodec::GetLittleEndian(a_bytes, offset, 8);
		a_out.callerRva = ByteCodec::GetLittleEndian(a_bytes, offset, 8);
		for (auto& arg : a_out.args) {
			arg = ByteCodec::GetLittleEndian(a_bytes, offset, 8);
		}
		a_out.retval = ByteCodec::GetLittleEndian(a_bytes, offset, 8);
		a_out.argCount = static_cast<std::uint8_t>(ByteCodec::GetLittleEndian(a_bytes, offset, 1));
		ByteCodec::GetLittleEndian(a_bytes, offset, 1);
		return a_out.argCount <= MaxArgs;
	}
}

namespace F4T::ProbeRing
{
	void Ring::Push(const ProbeRecord::Record& a_record)
	{
		const auto seq = m_nextSeq.fetch_add(1, std::memory_order_relaxed) + 1;
		auto& slot = m_slots[(seq - 1) & (Capacity - 1)];

		slot.seq.store(0, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		slot.probeId.store(a_record.probeId, std::memory_order_relaxed);
		slot.kind.store(static_cast<std::uint8_t>(a_record.kind), std::memory_order_relaxed);
		slot.threadId.store(a_record.threadId, std::memory_order_relaxed);
		slot.tickMicros.store(a_record.tickMicros, std::memory_order_relaxed);
		slot.callerRva.store(a_record.callerRva, std::memory_order_relaxed);
		for (std::size_t index = 0; index < ProbeRecord::MaxArgs; ++index) {
			slot.args[index].store(a_record.args[index], std::memory_order_relaxed);
		}
		slot.retval.store(a_record.retval, std::memory_order_relaxed);
		slot.argCount.store(a_record.argCount, std::memory_order_relaxed);
		slot.seq.store(seq, std::memory_order_release);
	}

	std::size_t Ring::Drain(std::span<ProbeRecord::Record> a_out) const
	{
		if (a_out.size() < Capacity) {
			return 0;
		}

		std::vector<std::pair<std::uint64_t, ProbeRecord::Record>> entries;
		entries.reserve(Capacity);
		for (const auto& slot : m_slots) {
			const auto seq = slot.seq.load(std::memory_order_acquire);
			if (seq == 0) {
				continue;
			}

			ProbeRecord::Record record;
			record.probeId = slot.probeId.load(std::memory_order_relaxed);
			record.kind = static_cast<ProbeRecord::Kind>(slot.kind.load(std::memory_order_relaxed));
			record.threadId = slot.threadId.load(std::memory_order_relaxed);
			record.tickMicros = slot.tickMicros.load(std::memory_order_relaxed);
			record.callerRva = slot.callerRva.load(std::memory_order_relaxed);
			for (std::size_t index = 0; index < ProbeRecord::MaxArgs; ++index) {
				record.args[index] = slot.args[index].load(std::memory_order_relaxed);
			}
			record.retval = slot.retval.load(std::memory_order_relaxed);
			record.argCount = slot.argCount.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (slot.seq.load(std::memory_order_acquire) != seq) {
				continue;
			}
			entries.emplace_back(seq, record);
		}

		std::sort(entries.begin(), entries.end(), [](const auto& a_lhs, const auto& a_rhs) {
			return a_lhs.first < a_rhs.first;
		});
		for (std::size_t index = 0; index < entries.size(); ++index) {
			a_out[index] = entries[index].second;
		}
		return entries.size();
	}

	std::uint64_t Ring::Overwritten() const
	{
		const auto seq = m_nextSeq.load(std::memory_order_relaxed);
		return seq > Capacity ? seq - Capacity : 0;
	}
}
