#include "F4TProbeCapture.h"
#include "F4TProbeRecord.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
	using F4T::ProbeCapture::Build;
	using F4T::ProbeCapture::CaptureStackChain;
	using F4T::ProbeCapture::Context;
	using F4T::ProbeCapture::CurrentThreadId;
	using F4T::ProbeCapture::CurrentTickMicros;
	using F4T::ProbeCapture::MaxStackChain;
	using F4T::ProbeCapture::PushedCount;
	using F4T::ProbeCapture::Ring;
	using F4T::ProbeCapture::SetModuleBase;

	void TestBuild()
	{
		constexpr std::uintptr_t moduleBase = 10000;
		constexpr std::uintptr_t caller = 10128;
		const Context context{ 7, 999, caller, moduleBase };

		const std::array<std::uint64_t, 8> values{ 1, 2, 3, 4, 5, 6, 7, 8 };
		const auto record = Build(3, F4T::ProbeRecord::Kind::Enter, context, values, 42);
		assert(record.probeId == 3);
		assert(record.kind == F4T::ProbeRecord::Kind::Enter);
		assert(record.threadId == 7);
		assert(record.tickMicros == 999);
		assert(record.callerRva == 128);
		assert(record.retval == 42);
		assert(record.argCount == F4T::ProbeRecord::MaxArgs);
		for (std::size_t index = 0; index < F4T::ProbeRecord::MaxArgs; ++index) {
			assert(record.args[index] == values[index]);
		}

		const std::span<const std::uint64_t> two{ values.data(), 2 };
		const auto shortRecord = Build(4, F4T::ProbeRecord::Kind::Return, context, two, 0);
		assert(shortRecord.argCount == 2);
		assert(shortRecord.args[0] == 1);
		assert(shortRecord.args[1] == 2);
		assert(shortRecord.args[2] == 0);

		const auto emptyRecord = Build(5, F4T::ProbeRecord::Kind::Snapshot, context, {}, 0);
		assert(emptyRecord.argCount == 0);

		const Context belowBase{ 1, 2, moduleBase - 1, moduleBase };
		assert(Build(6, F4T::ProbeRecord::Kind::Enter, belowBase, {}, 0).callerRva == 0);
	}

	void TestCaptureClock()
	{
		assert(CurrentThreadId() != 0);
		const auto first = CurrentTickMicros();
		const auto second = CurrentTickMicros();
		assert(second >= first);
	}

	void TestStackChain()
	{
		std::array<std::uint64_t, MaxStackChain> chain{};
		const auto count = CaptureStackChain(0, chain);
		assert(count >= 1);
		assert(count <= MaxStackChain);
		assert(chain[0] != 0);

		std::array<std::uint64_t, 2> shortChain{};
		assert(CaptureStackChain(0, shortChain) <= shortChain.size());
	}

	void TestPushToRing()
	{
		SetModuleBase(10000);
		assert(F4T::ProbeCapture::ModuleBase() == 10000);

		F4T::ProbeCapture::Push(
			3,
			F4T::ProbeRecord::Kind::Snapshot,
			reinterpret_cast<std::uintptr_t>(&TestPushToRing),
			{},
			0);
		assert(PushedCount() == 1);

		std::vector<F4T::ProbeRecord::Record> records(F4T::ProbeRing::Capacity);
		const auto drained = Ring().Drain(records);
		assert(drained == 1);
		assert(records[0].probeId == 3);
		assert(records[0].kind == F4T::ProbeRecord::Kind::Snapshot);
		assert(records[0].argCount <= F4T::ProbeRecord::MaxArgs);
	}
}

int main()
{
	TestBuild();
	TestCaptureClock();
	TestStackChain();
	TestPushToRing();
	return 0;
}
