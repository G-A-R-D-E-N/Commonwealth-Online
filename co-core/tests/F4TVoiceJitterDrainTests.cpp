#include "F4TVoiceJitter.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
	using namespace F4T::Voice;

	constexpr std::array<std::byte, 4> Payload{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };
	constexpr double Tick = 0.02;
	constexpr double T0 = 1000.0;
	constexpr int Ticks = 300;
	constexpr int WindowTicks = 50;
	constexpr int DrainTick = 99;

	double At(int a_ticks) noexcept
	{
		return T0 + a_ticks * Tick;
	}

	std::uint32_t Lag(const JitterBuffer& a_buffer) noexcept
	{
		return a_buffer.count == 0 ? 0U : a_buffer.frames[a_buffer.count - 1U].sequence - a_buffer.cursor;
	}

	void Deliver(VoiceSlots& a_slots, int a_tick)
	{
		if (a_tick == 0) {
			for (std::uint32_t sequence = 1; sequence <= 3; ++sequence) {
				assert(Accept(a_slots, 7, sequence, Payload, false, false, At(a_tick)));
			}
		} else if (a_tick == 3) {
			for (std::uint32_t sequence = 4; sequence <= 9; ++sequence) {
				assert(Accept(a_slots, 7, sequence, Payload, false, false, At(a_tick)));
			}
		} else if (a_tick >= 4 && a_tick % 5 == 4) {
			const auto first = static_cast<std::uint32_t>(a_tick + 6);
			for (std::uint32_t sequence = first; sequence < first + 5U; ++sequence) {
				assert(Accept(a_slots, 7, sequence, Payload, false, false, At(a_tick)));
			}
		}
	}
}

int main()
{
	VoiceSlots slots{};
	JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
	std::vector<std::uint32_t> lags;
	int drainTick = -1;
	for (int k = 0; k < Ticks; ++k) {
		Deliver(slots, k);
		const std::uint32_t cursorBefore = buffer.cursor;
		const std::uint32_t depthBefore = buffer.depthFrames;
		const std::uint32_t drainsBefore = buffer.drainEvents;
		const Decision decision = Next(buffer, At(k));
		assert(decision.kind == DecisionKind::Play);
		const bool drained = buffer.drainEvents != drainsBefore;
		const std::uint32_t skip = buffer.cursor - cursorBefore - 1U;
		lags.push_back(Lag(buffer) + (drained ? skip : 0U));
		if (drained) {
			const std::uint32_t minLag = *std::min_element(lags.end() - WindowTicks, lags.end());
			assert(lags.back() >= minLag + 3U);
			assert(skip == minLag - depthBefore);
			drainTick = k;
		}
	}
	assert(drainTick == DrainTick);
	assert(buffer.underrunEvents == 0);
	assert(buffer.drainEvents == 1);
	return 0;
}
