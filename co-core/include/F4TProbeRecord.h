#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace F4T::ProbeRecord
{
	inline constexpr std::uint8_t Version = 1;
	inline constexpr std::size_t MaxArgs = 6;
	inline constexpr std::size_t WireSize = 82;

	enum class Kind : std::uint8_t
	{
		Enter = 0,
		Return = 1,
		Snapshot = 2
	};

	struct Record
	{
		std::uint16_t probeId{ 0 };
		Kind kind{ Kind::Enter };
		std::uint32_t threadId{ 0 };
		std::uint64_t tickMicros{ 0 };
		std::uint64_t callerRva{ 0 };
		std::array<std::uint64_t, MaxArgs> args{};
		std::uint64_t retval{ 0 };
		std::uint8_t argCount{ 0 };
	};

	std::size_t Encode(const Record& a_record, std::span<std::byte> a_out);
	bool Decode(std::span<const std::byte> a_bytes, Record& a_out);
}

namespace F4T::ProbeRing
{
	inline constexpr std::size_t Capacity = 4096;
	static_assert((Capacity & (Capacity - 1)) == 0, "ProbeRing capacity must be a power of two");

	class Ring
	{
	public:
		void Push(const ProbeRecord::Record& a_record);
		std::size_t Drain(std::span<ProbeRecord::Record> a_out) const;
		std::uint64_t Overwritten() const;

	private:
		struct Slot
		{
			std::atomic<std::uint64_t> seq{ 0 };
			std::atomic<std::uint16_t> probeId{ 0 };
			std::atomic<std::uint8_t> kind{ 0 };
			std::atomic<std::uint32_t> threadId{ 0 };
			std::atomic<std::uint64_t> tickMicros{ 0 };
			std::atomic<std::uint64_t> callerRva{ 0 };
			std::array<std::atomic<std::uint64_t>, ProbeRecord::MaxArgs> args{};
			std::atomic<std::uint64_t> retval{ 0 };
			std::atomic<std::uint8_t> argCount{ 0 };
		};

		std::array<Slot, Capacity> m_slots{};
		std::atomic<std::uint64_t> m_nextSeq{ 0 };
	};
}
