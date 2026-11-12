#include "F4TVoiceJitter.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace
{
	using namespace F4T::Voice;

	constexpr std::array<std::byte, 4> Payload{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };
	constexpr double Tick = 0.02;
	constexpr double T0 = 1000.0;

	double At(double a_base, int a_ticks) noexcept
	{
		return a_base + a_ticks * Tick;
	}

	bool Receive(VoiceSlots& a_slots, std::uint16_t a_speaker, std::uint32_t a_sequence, double a_now, bool a_start = false, bool a_end = false)
	{
		return Accept(a_slots, a_speaker, a_sequence, Payload, a_start, a_end, a_now);
	}

	VoiceSlot* Find(VoiceSlots& a_slots, std::uint16_t a_speaker) noexcept
	{
		for (auto& slot : a_slots.slots) {
			if (slot.used && slot.speakerId == a_speaker) {
				return &slot;
			}
		}
		return nullptr;
	}

	std::uint32_t Lag(const JitterBuffer& a_buffer) noexcept
	{
		return a_buffer.count == 0 ? 0U : a_buffer.frames[a_buffer.count - 1U].sequence - a_buffer.cursor;
	}

	void PlayThenEvict(VoiceSlots& a_slots)
	{
		assert(Receive(a_slots, 7, 1, T0));
		assert(Receive(a_slots, 7, 2, T0));
		assert(Receive(a_slots, 7, 3, T0));
		JitterBuffer& buffer = Find(a_slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (std::uint16_t speaker = 11; speaker <= 14; ++speaker) {
			assert(Receive(a_slots, speaker, 10, At(T0, 3)));
		}
		assert(Find(a_slots, 7) == nullptr);
	}
}

int main()
{
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 101, T0));
		assert(Receive(slots, 7, 102, T0));
		assert(Next(buffer, T0).kind == DecisionKind::NoneYet);
		assert(Next(buffer, At(T0, 1)).kind == DecisionKind::NoneYet);
		assert(Next(buffer, At(T0, 2)).kind == DecisionKind::NoneYet);
		const Decision first = Next(buffer, At(T0, 3));
		assert(first.kind == DecisionKind::Play && first.sequence == 101);
		const Decision second = Next(buffer, At(T0, 4));
		assert(second.kind == DecisionKind::Play && second.sequence == 102);
		assert(Next(buffer, At(T0, 5)).kind == DecisionKind::Conceal);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		const Decision idle = Next(buffer, T0);
		assert(idle.kind == DecisionKind::NoneYet && !idle.resetDecoder);
		assert(Receive(slots, 7, 1401, T0));
		assert(Receive(slots, 7, 1402, T0));
		assert(Receive(slots, 7, 1403, T0));
		const Decision first = Next(buffer, At(T0, 1));
		assert(first.kind == DecisionKind::Play && first.sequence == 1401 && first.resetDecoder);
		assert(!Next(buffer, At(T0, 2)).resetDecoder);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1601, T0));
		assert(Receive(slots, 7, 1602, T0));
		assert(Receive(slots, 7, 1603, T0));
		assert(Next(buffer, T0).sequence == 1601);
		assert(Next(buffer, At(T0, 1)).sequence == 1602);
		assert(Next(buffer, At(T0, 2)).sequence == 1603);
		const Decision dry = Next(buffer, At(T0, 3));
		assert(dry.kind == DecisionKind::Conceal && dry.sequence == 1604);
		assert(Receive(slots, 7, 1605, At(T0, 3)));
		assert(Receive(slots, 7, 1606, At(T0, 3)));
		assert(Receive(slots, 7, 1607, At(T0, 3)));
		const Decision fec = Next(buffer, At(T0, 4));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 1604 && !fec.resetDecoder);
		assert(Next(buffer, At(T0, 5)).sequence == 1605);
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		slot.jitter.cursor = 0xFFFFFF00U;
		assert(Insert(slot.jitter, 1U, Payload, false, false));
		assert(Insert(slot.jitter, 2U, Payload, false, false));
		assert(Next(slot.jitter, T0).kind == DecisionKind::NoneYet);
		assert(!Insert(slot.jitter, 0xFFFFFFF0U, Payload, false, false));
		assert(Insert(slot.jitter, 3U, Payload, false, false));
		assert(Next(slot.jitter, At(T0, 1)).sequence == 1U);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1901, T0));
		assert(Receive(slots, 7, 1902, T0));
		assert(Receive(slots, 7, 1903, T0));
		assert(Next(buffer, T0).sequence == 1901);
		assert(Next(buffer, At(T0, 1)).sequence == 1902);
		assert(Next(buffer, At(T0, 2)).sequence == 1903);
		for (int k = 3; k <= 2002; ++k) {
			static_cast<void>(Next(buffer, At(T0, k)));
		}
		assert(buffer.depthFrames == JitterInitialDepthFrames && buffer.underrunEvents == 1);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		for (std::uint32_t k = 0; k < 7500; ++k) {
			const double now = At(T0, static_cast<int>(k));
			if (k > 0) {
				assert(Receive(slots, 7, k + 3U, now));
			}
			assert(Next(buffer, now).kind == DecisionKind::Play);
		}
		assert(buffer.depthFrames == JitterMinDepthFrames);
	}
	{
		VoiceSlots slots{};
		Admit(slots, 9, T0);
		Expire(slots, T0 + 0.2996);
		assert(Find(slots, 9) != nullptr);
		Expire(slots, T0 + 0.3);
		assert(Find(slots, 9) == nullptr);
	}
	{
		VoiceSlots slots{};
		PlayThenEvict(slots);
		assert(!Receive(slots, 7, 2, At(T0, 4)));
		assert(Find(slots, 11) != nullptr && Find(slots, 14) != nullptr);
		assert(Receive(slots, 7, 4, At(T0, 4)));
		assert(!Receive(slots, 7, 3, At(T0, 4)));
		JitterBuffer& returned = Find(slots, 7)->jitter;
		for (int k = 4; k <= 6; ++k) {
			assert(Next(returned, At(T0, k)).kind == DecisionKind::NoneYet);
		}
		const Decision four = Next(returned, At(T0, 7));
		assert(four.kind == DecisionKind::Play && four.sequence == 4 && four.resetDecoder);
		Forget(slots, 7);
		assert(Receive(slots, 7, 2, At(T0, 8)));
	}
	{
		VoiceSlots slots{};
		PlayThenEvict(slots);
		ResetSession(slots);
		assert(Find(slots, 11) == nullptr && Find(slots, 14) == nullptr);
		assert(Receive(slots, 7, 2, At(T0, 4)));
	}
	{
		VoiceSlots slots{};
		for (std::uint16_t speaker = 1; speaker <= 17; ++speaker) {
			const double now = 10.0 + speaker;
			assert(Receive(slots, speaker, 5, now));
			JitterBuffer& buffer = Find(slots, speaker)->jitter;
			for (int k = 0; k < 4; ++k) {
				static_cast<void>(Next(buffer, At(now, k)));
			}
			assert(buffer.cursor == 5);
			Remove(slots, speaker);
		}
		assert(!Receive(slots, 17, 5, 30.0));
		assert(!Receive(slots, 2, 5, 30.0));
		assert(Receive(slots, 1, 5, 30.0));
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		Reset(buffer);
		assert(Receive(slots, 7, 10, At(T0, 2)));
		assert(Receive(slots, 7, 11, At(T0, 2)));
		assert(Receive(slots, 7, 12, At(T0, 2)));
		const Decision first = Next(buffer, At(T0, 2));
		assert(first.kind == DecisionKind::Play && first.sequence == 10 && first.resetDecoder);
		const Decision second = Next(buffer, At(T0, 3));
		assert(second.kind == DecisionKind::Play && second.sequence == 11 && !second.resetDecoder);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Next(buffer, T0).kind == DecisionKind::NoneYet);
		assert(Next(buffer, At(T0, 1)).kind == DecisionKind::NoneYet);
		assert(Next(buffer, At(T0, 2)).kind == DecisionKind::NoneYet);
		assert(Receive(slots, 7, 2, At(T0, 3)));
		const Decision one = Next(buffer, At(T0, 3));
		assert(one.kind == DecisionKind::Play && one.sequence == 1 && one.resetDecoder);
		assert(Receive(slots, 7, 3, At(T0, 3), true, false));
		const Decision two = Next(buffer, At(T0, 4));
		assert(two.kind == DecisionKind::Play && two.sequence == 2 && !two.resetDecoder);
		assert(Receive(slots, 7, 4, At(T0, 4), false, true));
		const Decision three = Next(buffer, At(T0, 5));
		assert(three.kind == DecisionKind::Play && three.sequence == 3 && three.resetDecoder);
		const Decision four = Next(buffer, At(T0, 6));
		assert(four.kind == DecisionKind::Play && four.sequence == 4 && !four.resetDecoder);
		assert(Next(buffer, At(T0, 7)).kind == DecisionKind::NoneYet);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Receive(slots, 7, 10, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k <= 5; ++k) {
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Conceal);
		}
		assert(Receive(slots, 7, 7, At(T0, 5)));
		assert(Next(buffer, At(T0, 6)).sequence == 7);
		assert(Next(buffer, At(T0, 7)).kind == DecisionKind::Conceal);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		assert(Receive(slots, 7, 6, At(T0, 3)));
		assert(Receive(slots, 7, 7, At(T0, 3)));
		const Decision conceal = Next(buffer, At(T0, 3));
		assert(conceal.kind == DecisionKind::Conceal && conceal.sequence == 4 && buffer.underrunEvents == 0);
		const Decision fec = Next(buffer, At(T0, 4));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 5);
		assert(Next(buffer, At(T0, 5)).sequence == 6);
		assert(Next(buffer, At(T0, 6)).sequence == 7);
		assert(Receive(slots, 7, 10, At(T0, 7)));
		assert(Receive(slots, 7, 11, At(T0, 7)));
		const Decision gap = Next(buffer, At(T0, 7));
		assert(gap.kind == DecisionKind::Conceal && gap.sequence == 8);
		assert(buffer.underrunEvents == 0 && buffer.depthFrames == JitterInitialDepthFrames);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k <= 5; ++k) {
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Conceal);
		}
		assert(Next(buffer, At(T0, 6)).kind == DecisionKind::Silence);
		assert(Receive(slots, 7, 5, At(T0, 6)));
		for (std::uint32_t k = 7; k <= 15; ++k) {
			assert(Receive(slots, 7, k, At(T0, 7)));
		}
		const Decision fec = Next(buffer, At(T0, 7));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 4);
		assert(Receive(slots, 7, 16, At(T0, 7)));
		const Decision after = Next(buffer, At(T0, 8));
		assert(after.kind == DecisionKind::Conceal && after.sequence == 5 && !after.resetDecoder);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		assert(Next(buffer, At(T0, 3)).kind == DecisionKind::Conceal && buffer.hasUnderrun);
		assert(Next(buffer, At(T0, 4)).kind == DecisionKind::Conceal);
		assert(Receive(slots, 7, 4, At(T0, 5)));
		assert(Receive(slots, 7, 5, At(T0, 5)));
		assert(Receive(slots, 7, 6, At(T0, 5)));
		assert(Next(buffer, At(T0, 5)).sequence == 4);
		assert(Next(buffer, At(T0, 6)).sequence == 5);
		assert(Next(buffer, At(T0, 7)).sequence == 6);
		assert(Next(buffer, At(T0, 8)).kind == DecisionKind::Conceal);
		assert(!buffer.hasUnderrun && buffer.underrunEvents == 2 && buffer.depthFrames == JitterInitialDepthFrames + 1U);
		assert(Next(buffer, At(T0, 9)).kind == DecisionKind::Conceal);
		assert(Receive(slots, 7, 7, At(T0, 10)));
		assert(Receive(slots, 7, 8, At(T0, 10)));
		assert(Receive(slots, 7, 9, At(T0, 10)));
		for (int k = 10; k <= 13; ++k) {
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::NoneYet);
		}
		assert(Next(buffer, At(T0, 14)).sequence == 7);
		assert(Next(buffer, At(T0, 15)).sequence == 8);
		assert(Next(buffer, At(T0, 16)).sequence == 9);
		assert(Next(buffer, At(T0, 17)).kind == DecisionKind::Conceal);
		assert(buffer.hasUnderrun && buffer.underrunEvents == 3 && buffer.depthFrames == JitterInitialDepthFrames + 1U);
	}
	{
		VoiceSlots slots{};
		Admit(slots, 1, T0).jitter.cursor = 5;
		Remove(slots, 1);
		Admit(slots, 2, T0).jitter.cursor = 9;
		Remove(slots, 2);
		assert(!Receive(slots, 1, 5, T0 + 1.0));
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k < 18; ++k) {
			static_cast<void>(Next(buffer, At(T0, k)));
		}
		for (int k = 18; k < 118; ++k) {
			assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			static_cast<void>(Next(buffer, At(T0, k)));
		}
		assert(Lag(buffer) <= buffer.depthFrames + 1U);
		std::uint32_t overflows{};
		for (int k = 118; k < 618; ++k) {
			if (buffer.count == JitterMaxFrames) {
				overflows += 1U;
			}
			assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Play);
		}
		assert(overflows == 0);
	}
	{
		struct Arrival
		{
			double seconds{};
			std::uint32_t sequence{};
		};
		std::mt19937 random{ 20261022U };
		std::uniform_real_distribution<double> delay{ 0.0, 0.1 };
		std::vector<Arrival> arrivals;
		for (std::uint32_t k = 0; k < 3000; ++k) {
			arrivals.push_back(Arrival{ At(T0, static_cast<int>(k)) + delay(random), k + 1U });
		}
		std::stable_sort(arrivals.begin(), arrivals.end(), [](const Arrival& a_left, const Arrival& a_right) {
			return a_left.seconds < a_right.seconds;
		});
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		std::size_t delivered = 0;
		for (int k = 0; k < 3000; ++k) {
			const double now = At(T0, k);
			while (delivered < arrivals.size() && arrivals[delivered].seconds <= now) {
				static_cast<void>(Receive(slots, 7, arrivals[delivered].sequence, arrivals[delivered].seconds));
				delivered += 1U;
			}
			static_cast<void>(Next(buffer, now));
		}
		assert(buffer.drainEvents <= 2);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		for (std::uint32_t k = 1; k <= 10; ++k) {
			assert(Receive(slots, 7, k, T0));
		}
		for (int k = 0; k < 49; ++k) {
			const double now = At(T0, k);
			static_cast<void>(Next(buffer, now));
			assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 11), now));
		}
		Reset(buffer);
		for (std::uint32_t k = 100; k < 110; ++k) {
			assert(Receive(slots, 7, k, At(T0, 49)));
		}
		const Decision first = Next(buffer, At(T0, 49));
		assert(first.kind == DecisionKind::Play && first.sequence == 100 && first.resetDecoder);
		assert(buffer.drainEvents == 0);
		assert(Next(buffer, At(T0, 50)).sequence == 101);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k < 23; ++k) {
			static_cast<void>(Next(buffer, At(T0, k)));
		}
		for (std::uint32_t k = 54; k <= 58; ++k) {
			assert(Receive(slots, 7, k, At(T0, 23)));
		}
		bool unsignalled = false;
		std::uint32_t resets = 0;
		std::uint32_t jumps = 0;
		std::uint32_t before = buffer.cursor;
		for (int k = 23; k < 150; ++k) {
			if (k > 23) {
				assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 35), At(T0, k)));
			}
			const Decision decision = Next(buffer, At(T0, k));
			resets += decision.resetDecoder ? 1U : 0U;
			if (buffer.cursor - before > JitterResetJumpTicks) {
				jumps += 1U;
				unsignalled = !decision.resetDecoder;
			} else if (decision.resetDecoder) {
				unsignalled = false;
			}
			before = buffer.cursor;
		}
		assert(jumps >= 1);
		assert(!unsignalled);
		assert(resets == 1);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Receive(slots, 7, 300, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k < 50; ++k) {
			assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Play);
		}
		assert(buffer.drainEvents == 1);
		bool decided = false;
		Decision first{};
		std::uint32_t resets = 0;
		for (int k = 50; k < 80; ++k) {
			static_cast<void>(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			const Decision decision = Next(buffer, At(T0, k));
			if (!decided && decision.kind != DecisionKind::NoneYet) {
				first = decision;
				decided = true;
			}
			resets += decision.resetDecoder ? 1U : 0U;
		}
		assert(first.kind == DecisionKind::Play && first.sequence == 300 && first.resetDecoder);
		assert(resets == 1);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 1, T0));
		assert(Receive(slots, 7, 2, T0));
		assert(Receive(slots, 7, 3, T0));
		assert(Receive(slots, 7, 100, T0));
		assert(Next(buffer, T0).sequence == 1);
		assert(Next(buffer, At(T0, 1)).sequence == 2);
		assert(Next(buffer, At(T0, 2)).sequence == 3);
		for (int k = 3; k < 50; ++k) {
			assert(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Play);
		}
		assert(buffer.drainEvents == 1);
		std::uint32_t resets = 0;
		for (int k = 50; k < 80; ++k) {
			static_cast<void>(Receive(slots, 7, static_cast<std::uint32_t>(k + 1), At(T0, k)));
			resets += Next(buffer, At(T0, k)).resetDecoder ? 1U : 0U;
		}
		assert(resets == 0);
	}
	return 0;
}
