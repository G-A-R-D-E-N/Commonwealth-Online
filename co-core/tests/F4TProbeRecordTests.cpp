#include "F4TProbeRecord.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace
{
	using F4T::ProbeRecord::Kind;
	using F4T::ProbeRecord::MaxArgs;
	using F4T::ProbeRecord::Record;
	using F4T::ProbeRecord::WireSize;
	using F4T::ProbeRing::Capacity;
	using F4T::ProbeRing::Ring;

	Record MakeRecord(std::uint64_t a_tick, Kind a_kind = Kind::Return)
	{
		Record record;
		record.probeId = static_cast<std::uint16_t>(a_tick & 0xFFFFU);
		record.kind = a_kind;
		record.threadId = static_cast<std::uint32_t>(a_tick * 3U);
		record.tickMicros = a_tick;
		record.callerRva = 0x1234U + a_tick;
		for (std::size_t index = 0; index < MaxArgs; ++index) {
			record.args[index] = a_tick * 100U + index;
		}
		record.retval = a_tick ^ 0xDEADBEEFU;
		record.argCount = static_cast<std::uint8_t>(MaxArgs - (a_tick % 2U));
		return record;
	}

	void AssertSame(const Record& a_lhs, const Record& a_rhs)
	{
		assert(a_lhs.probeId == a_rhs.probeId);
		assert(a_lhs.kind == a_rhs.kind);
		assert(a_lhs.threadId == a_rhs.threadId);
		assert(a_lhs.tickMicros == a_rhs.tickMicros);
		assert(a_lhs.callerRva == a_rhs.callerRva);
		for (std::size_t index = 0; index < MaxArgs; ++index) {
			assert(a_lhs.args[index] == a_rhs.args[index]);
		}
		assert(a_lhs.retval == a_rhs.retval);
		assert(a_lhs.argCount == a_rhs.argCount);
	}

	void TestCodec()
	{
		using namespace F4T::ProbeRecord;

		const auto record = MakeRecord(0x1234);
		std::array<std::byte, WireSize> bytes{};
		assert(Encode(record, bytes) == WireSize);

		assert(bytes[0] == std::byte{ Version });
		assert(bytes[1] == std::byte{ 0x34 } && bytes[2] == std::byte{ 0x12 });
		assert(bytes[3] == std::byte{ static_cast<unsigned char>(Kind::Return) });
		assert(bytes[4] == std::byte{ 0x9C } && bytes[7] == std::byte{ 0x00 });
		assert(bytes[8] == std::byte{ 0x34 } && bytes[15] == std::byte{ 0x00 });
		assert(bytes[80] == std::byte{ 6 });
		assert(bytes[81] == std::byte{ 0 });

		Record decoded;
		assert(Decode(bytes, decoded));
		AssertSame(record, decoded);

		for (const auto kind : { Kind::Enter, Kind::Return, Kind::Snapshot }) {
			auto varied = MakeRecord(7, kind);
			std::array<std::byte, WireSize> encoded{};
			assert(Encode(varied, encoded) == WireSize);
			Record roundTrip;
			assert(Decode(encoded, roundTrip));
			AssertSame(varied, roundTrip);
		}

		std::array<std::byte, WireSize - 1> small{};
		assert(Encode(record, small) == 0);
		assert(!Decode(small, decoded));
		std::array<std::byte, WireSize + 1> large{};
		assert(!Decode(large, decoded));

		auto mutated = bytes;
		mutated[0] = std::byte{ 2 };
		assert(!Decode(mutated, decoded));
		mutated = bytes;
		mutated[3] = std::byte{ 3 };
		assert(!Decode(mutated, decoded));
		mutated = bytes;
		mutated[80] = std::byte{ 7 };
		assert(!Decode(mutated, decoded));

		auto badArgCount = record;
		badArgCount.argCount = static_cast<std::uint8_t>(MaxArgs + 1);
		std::array<std::byte, WireSize> rejected{};
		assert(Encode(badArgCount, rejected) == 0);

		auto badKind = record;
		badKind.kind = static_cast<Kind>(3);
		assert(Encode(badKind, rejected) == 0);
	}

	void TestWrapAround()
	{
		Ring ring;
		assert(ring.Overwritten() == 0);

		constexpr std::uint64_t kOverflow = 7;
		for (std::uint64_t tick = 1; tick <= Capacity + kOverflow; ++tick) {
			ring.Push(MakeRecord(tick));
		}
		assert(ring.Overwritten() == kOverflow);

		std::vector<Record> out(Capacity);
		const auto count = ring.Drain(out);
		assert(count == Capacity);
		for (std::size_t index = 0; index < count; ++index) {
			assert(out[index].tickMicros == kOverflow + 1 + index);
			AssertSame(out[index], MakeRecord(kOverflow + 1 + index));
		}

		assert(ring.Drain(std::span<Record>(out).first(Capacity - 1)) == 0);
	}

	void TestConcurrentNoOverwrite()
	{
		constexpr std::uint32_t kThreads = 4;
		constexpr std::uint64_t kPerThread = 500;
		constexpr std::uint64_t kTotal = kThreads * kPerThread;

		Ring ring;
		std::vector<std::thread> threads;
		threads.reserve(kThreads);
		for (std::uint32_t thread = 0; thread < kThreads; ++thread) {
			threads.emplace_back([&ring, thread]() {
				for (std::uint64_t index = 0; index < kPerThread; ++index) {
					ring.Push(MakeRecord(thread * kPerThread + index));
				}
			});
		}
		for (auto& thread : threads) {
			thread.join();
		}

		assert(ring.Overwritten() == 0);
		std::vector<Record> out(Capacity);
		const auto count = ring.Drain(out);
		assert(count == kTotal);

		std::vector<std::uint64_t> ticks;
		ticks.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			AssertSame(out[index], MakeRecord(out[index].tickMicros));
			ticks.push_back(out[index].tickMicros);
		}
		std::sort(ticks.begin(), ticks.end());
		for (std::uint64_t tick = 0; tick < kTotal; ++tick) {
			assert(ticks[tick] == tick);
		}
	}

	void TestConcurrentOverwrite()
	{
		constexpr std::uint32_t kThreads = 4;
		constexpr std::uint64_t kPerThread = 1500;
		constexpr std::uint64_t kTotal = kThreads * kPerThread;
		static_assert(kTotal > Capacity);

		Ring ring;
		std::vector<std::thread> threads;
		threads.reserve(kThreads);
		for (std::uint32_t thread = 0; thread < kThreads; ++thread) {
			threads.emplace_back([&ring, thread]() {
				for (std::uint64_t index = 0; index < kPerThread; ++index) {
					ring.Push(MakeRecord(thread * kPerThread + index));
				}
			});
		}
		for (auto& thread : threads) {
			thread.join();
		}

		assert(ring.Overwritten() == kTotal - Capacity);
		std::vector<Record> out(Capacity);
		assert(ring.Drain(out) == Capacity);
	}
}

int main()
{
	TestCodec();
	TestWrapAround();
	TestConcurrentNoOverwrite();
	TestConcurrentOverwrite();
	return 0;
}
