#include "F4TVoiceJitter.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

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

	bool Receive(VoiceSlots& a_slots, std::uint16_t a_speaker, std::uint32_t a_sequence, double a_now, bool a_start = false, bool a_end = false, std::span<const std::byte> a_payload = Payload)
	{
		return Accept(a_slots, a_speaker, a_sequence, a_payload, a_start, a_end, a_now);
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

	void Cycle(VoiceSlots& a_slots, JitterBuffer& a_buffer, double& a_now) noexcept
	{
		for (std::uint32_t k = 1; k <= a_buffer.depthFrames; ++k) {
			assert(Receive(a_slots, 7, a_buffer.cursor + k, a_now));
		}
		for (std::uint32_t k = 0; k <= a_buffer.depthFrames; ++k) {
			static_cast<void>(Next(a_buffer, a_now));
			a_now += Tick;
		}
	}
}

int main()
{
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 102, T0));
		assert(Receive(slots, 7, 101, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		const Decision first = Next(buffer, T0);
		assert(first.kind == DecisionKind::NoneYet && !first.resetDecoder);
		assert(Receive(slots, 7, 103, T0));
		const Decision a = Next(buffer, At(T0, 1));
		assert(a.kind == DecisionKind::Play && a.sequence == 101 && a.resetDecoder && a.payload.size() == Payload.size());
		const Decision b = Next(buffer, At(T0, 2));
		assert(b.kind == DecisionKind::Play && b.sequence == 102);
		const Decision c = Next(buffer, At(T0, 3));
		assert(c.kind == DecisionKind::Play && c.sequence == 103);
		assert(!Receive(slots, 7, 101, At(T0, 3)));
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		assert(Receive(slots, 7, 102, T0));
		assert(Next(buffer, T0).kind == DecisionKind::NoneYet);
		assert(Receive(slots, 7, 101, T0));
		assert(Receive(slots, 7, 103, T0));
		assert(Next(buffer, At(T0, 1)).sequence == 101);
		assert(Next(buffer, At(T0, 2)).sequence == 102);
		assert(Next(buffer, At(T0, 3)).sequence == 103);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 102, T0));
		assert(Receive(slots, 7, 103, T0));
		assert(Receive(slots, 7, 105, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 102);
		assert(Receive(slots, 7, 104, At(T0, 1)));
		assert(Next(buffer, At(T0, 1)).sequence == 103);
		assert(Next(buffer, At(T0, 2)).sequence == 104);
		assert(Next(buffer, At(T0, 3)).sequence == 105);
		assert(!Receive(slots, 7, 104, At(T0, 3)));
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 202, T0));
		assert(!Receive(slots, 7, 202, T0));
		assert(!Receive(slots, 7, 202, T0, true, false));
		assert(Find(slots, 7)->jitter.count == 1);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 301, T0));
		assert(Receive(slots, 7, 302, T0));
		assert(Receive(slots, 7, 304, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).kind == DecisionKind::Play);
		assert(Next(buffer, At(T0, 1)).sequence == 302);
		const Decision fec = Next(buffer, At(T0, 2));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 303 && fec.payload.size() == Payload.size());
		const Decision play = Next(buffer, At(T0, 3));
		assert(play.kind == DecisionKind::Play && play.sequence == 304);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 401, T0));
		assert(Receive(slots, 7, 402, T0));
		assert(Receive(slots, 7, 405, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).kind == DecisionKind::Play);
		assert(Next(buffer, At(T0, 1)).sequence == 402);
		const Decision conceal = Next(buffer, At(T0, 2));
		assert(conceal.kind == DecisionKind::Conceal && conceal.sequence == 403 && conceal.payload.empty());
		const Decision fec = Next(buffer, At(T0, 3));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 404);
		const Decision play = Next(buffer, At(T0, 4));
		assert(play.kind == DecisionKind::Play && play.sequence == 405);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 501, T0));
		assert(Receive(slots, 7, 502, T0));
		assert(Receive(slots, 7, 503, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).kind == DecisionKind::Play);
		assert(Next(buffer, At(T0, 1)).kind == DecisionKind::Play);
		assert(Next(buffer, At(T0, 2)).kind == DecisionKind::Play);
		for (int k = 3; k <= 5; ++k) {
			const Decision conceal = Next(buffer, At(T0, k));
			assert(conceal.kind == DecisionKind::Conceal && conceal.sequence == 504);
		}
		const Decision silence = Next(buffer, At(T0, 6));
		assert(silence.kind == DecisionKind::Silence && silence.sequence == 504);
		assert(Next(buffer, At(T0, 7)).kind == DecisionKind::Silence);
		assert(Receive(slots, 7, 509, At(T0, 7)));
		assert(Next(buffer, At(T0, 8)).kind == DecisionKind::NoneYet);
		assert(Receive(slots, 7, 510, At(T0, 8)));
		assert(Receive(slots, 7, 511, At(T0, 8)));
		for (int k = 9; k <= 12; ++k) {
			assert(Next(buffer, At(T0, k)).kind == DecisionKind::Silence);
		}
		const Decision fec = Next(buffer, At(T0, 13));
		assert(fec.kind == DecisionKind::Fec && fec.sequence == 508);
		const Decision resumed = Next(buffer, At(T0, 14));
		assert(resumed.kind == DecisionKind::Play && resumed.sequence == 509 && !resumed.resetDecoder);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		const Decision admission = Next(buffer, T0);
		assert(admission.kind == DecisionKind::NoneYet && !admission.resetDecoder);
		assert(Receive(slots, 7, 601, At(T0, 1), true, false));
		assert(Receive(slots, 7, 602, At(T0, 1)));
		assert(Receive(slots, 7, 603, At(T0, 1)));
		const Decision first = Next(buffer, At(T0, 1));
		assert(first.kind == DecisionKind::Play && first.sequence == 601 && first.resetDecoder);
		const Decision second = Next(buffer, At(T0, 2));
		assert(second.kind == DecisionKind::Play && second.sequence == 602 && !second.resetDecoder);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 701, T0));
		assert(Receive(slots, 7, 702, T0));
		assert(Receive(slots, 7, 703, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 701);
		assert(Next(buffer, At(T0, 1)).sequence == 702);
		assert(!Receive(slots, 7, 702, At(T0, 1), true, false));
		assert(!Receive(slots, 7, 703, At(T0, 1), true, false));
		const Decision third = Next(buffer, At(T0, 2));
		assert(third.kind == DecisionKind::Play && third.sequence == 703 && !third.resetDecoder);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 801, T0, true, false));
		assert(Receive(slots, 7, 802, T0, false, true));
		assert(Receive(slots, 7, 803, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 801);
		assert(Next(buffer, At(T0, 1)).sequence == 802);
		for (int k = 2; k <= 4; ++k) {
			const Decision idle = Next(buffer, At(T0, k));
			assert(idle.kind == DecisionKind::NoneYet && !idle.resetDecoder);
		}
		assert(buffer.depthFrames == JitterInitialDepthFrames);
		assert(Receive(slots, 7, 804, At(T0, 4)));
		assert(Receive(slots, 7, 805, At(T0, 4)));
		const Decision resumed = Next(buffer, At(T0, 5));
		assert(resumed.kind == DecisionKind::Play && resumed.sequence == 803 && !resumed.resetDecoder);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 901, T0));
		assert(Receive(slots, 7, 902, T0));
		assert(Receive(slots, 7, 903, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 901);
		assert(Next(buffer, At(T0, 1)).sequence == 902);
		assert(Next(buffer, At(T0, 2)).sequence == 903);
		assert(Receive(slots, 7, 954, At(T0, 2)));
		assert(Receive(slots, 7, 955, At(T0, 2)));
		assert(Receive(slots, 7, 956, At(T0, 2)));
		const Decision jump = Next(buffer, At(T0, 3));
		assert(jump.kind == DecisionKind::Play && jump.sequence == 954 && jump.resetDecoder);
		const Decision second = Next(buffer, At(T0, 4));
		assert(second.kind == DecisionKind::Play && second.sequence == 955 && !second.resetDecoder);
		assert(Next(buffer, At(T0, 5)).sequence == 956);
		assert(Receive(slots, 7, 1200, At(T0, 5)));
		const Decision waiting = Next(buffer, At(T0, 6));
		assert(waiting.kind == DecisionKind::NoneYet && !waiting.resetDecoder);
		assert(Receive(slots, 7, 1201, At(T0, 6)));
		assert(Receive(slots, 7, 1202, At(T0, 6)));
		const Decision resumed = Next(buffer, At(T0, 7));
		assert(resumed.kind == DecisionKind::Play && resumed.sequence == 1200 && resumed.resetDecoder);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 1101, T0));
		assert(Receive(slots, 7, 1102, T0));
		assert(Receive(slots, 7, 1103, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 1101);
		assert(Next(buffer, At(T0, 1)).sequence == 1102);
		assert(Next(buffer, At(T0, 2)).sequence == 1103);
		assert(Receive(slots, 7, 1153, At(T0, 2)));
		assert(Receive(slots, 7, 1154, At(T0, 2)));
		assert(Receive(slots, 7, 1155, At(T0, 2)));
		const Decision conceal = Next(buffer, At(T0, 3));
		assert(conceal.kind == DecisionKind::Conceal && conceal.sequence == 1104 && !conceal.resetDecoder);
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 3, T0);
		assert(slot.jitter.resetPending);
		const Decision first = Next(slot.jitter, T0);
		assert(first.kind == DecisionKind::NoneYet && !first.resetDecoder);
		assert(!Next(slot.jitter, At(T0, 1)).resetDecoder);
		assert(Receive(slots, 7, 1201, T0));
		assert(Receive(slots, 7, 1202, T0));
		assert(Receive(slots, 7, 1203, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 1201);
		assert(Next(buffer, At(T0, 1)).sequence == 1202);
		Reset(buffer);
		assert(buffer.count == 0 && buffer.depthFrames == JitterInitialDepthFrames);
		const Decision after = Next(buffer, At(T0, 2));
		assert(after.kind == DecisionKind::NoneYet && !after.resetDecoder);
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		std::array<std::byte, 161> oversized{};
		std::array<std::byte, 160> full{};
		assert(!Insert(slot.jitter, 10, std::span<const std::byte>{}, false, false));
		assert(!Insert(slot.jitter, 10, oversized, false, false));
		assert(!Insert(slot.jitter, 0, Payload, false, false));
		assert(Insert(slot.jitter, 10, full, false, false));
		assert(slot.jitter.playoutSize == 0 && slot.jitter.count == 1);
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		for (std::uint32_t sequence = 1; sequence <= 12; ++sequence) {
			assert(Insert(slot.jitter, sequence, Payload, false, false));
		}
		assert(slot.jitter.count == JitterMaxFrames);
		assert(slot.jitter.frames[0].sequence == 3);
		assert(!Insert(slot.jitter, 1, Payload, false, false));
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		assert(Insert(slot.jitter, 0xFFFFFFFEU, Payload, false, false));
		assert(Insert(slot.jitter, 0xFFFFFFFFU, Payload, false, false));
		assert(Insert(slot.jitter, 1U, Payload, false, false));
		assert(Insert(slot.jitter, 2U, Payload, false, false));
		assert(Next(slot.jitter, T0).sequence == 0xFFFFFFFEU);
		assert(Next(slot.jitter, At(T0, 1)).sequence == 0xFFFFFFFFU);
		assert(Next(slot.jitter, At(T0, 2)).sequence == 1U);
		assert(Next(slot.jitter, At(T0, 3)).sequence == 2U);
		assert(!Insert(slot.jitter, 0xFFFFFFFEU, Payload, false, false));
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		slot.jitter.cursor = 0xFFFFFFFEU;
		assert(Insert(slot.jitter, 0xFFFFFFFFU, Payload, false, false));
		assert(Insert(slot.jitter, 1U, Payload, false, false));
		assert(Next(slot.jitter, T0).kind == DecisionKind::NoneYet);
		assert(Insert(slot.jitter, 2U, Payload, false, false));
		const Decision first = Next(slot.jitter, At(T0, 1));
		assert(first.kind == DecisionKind::Play && first.sequence == 0xFFFFFFFFU);
		assert(Next(slot.jitter, At(T0, 2)).sequence == 1U);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 1701, T0));
		assert(Receive(slots, 7, 1702, T0));
		assert(Receive(slots, 7, 1703, T0));
		assert(Receive(slots, 7, 1705, T0));
		assert(Receive(slots, 7, 1707, T0));
		assert(Receive(slots, 7, 1708, T0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 1701);
		assert(Next(buffer, At(T0, 1)).sequence == 1702);
		assert(Next(buffer, At(T0, 2)).sequence == 1703);
		assert(Next(buffer, At(T0, 3)).kind == DecisionKind::Fec);
		assert(Next(buffer, At(T0, 4)).sequence == 1705);
		assert(Next(buffer, At(T0, 5)).kind == DecisionKind::Fec);
		assert(Next(buffer, At(T0, 6)).sequence == 1707);
		assert(Next(buffer, At(T0, 7)).sequence == 1708);
		assert(buffer.underrunEvents == 0);
		assert(buffer.depthFrames == JitterInitialDepthFrames);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		double now = T0;
		Cycle(slots, buffer, now);
		now = T0 + 5.0;
		Cycle(slots, buffer, now);
		assert(buffer.underrunEvents == 2 && buffer.depthFrames == 4);
		const std::uint32_t base = buffer.cursor;
		for (std::uint32_t k = 1; k <= 3; ++k) {
			assert(Receive(slots, 7, base + k, now));
		}
		assert(Next(buffer, now).kind == DecisionKind::NoneYet);
		assert(Receive(slots, 7, base + 4, At(now, 1)));
		assert(Next(buffer, At(now, 1)).sequence == base + 1);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		double now = T0;
		Cycle(slots, buffer, now);
		now = T0 + 5.02;
		Cycle(slots, buffer, now);
		assert(buffer.underrunEvents == 2 && buffer.depthFrames == JitterInitialDepthFrames);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		double now = T0;
		for (int cycle = 0; cycle < 16; ++cycle) {
			Cycle(slots, buffer, now);
		}
		assert(buffer.underrunEvents == 16 && buffer.depthFrames == JitterMaxDepthFrames);
	}
	{
		VoiceSlots slots{};
		JitterBuffer& buffer = Admit(slots, 7, T0).jitter;
		double now = T0;
		Cycle(slots, buffer, now);
		now = T0 + 5.0;
		Cycle(slots, buffer, now);
		for (int k = 0; k < 1600; ++k) {
			static_cast<void>(Next(buffer, now));
			now += Tick;
		}
		assert(buffer.depthFrames == 4 && buffer.underrunEvents == 2);
		const std::uint32_t base = buffer.cursor;
		for (std::uint32_t k = 1; k <= 4; ++k) {
			assert(Receive(slots, 7, base + k, now));
		}
		const double anchor = now;
		assert(Next(buffer, anchor).sequence == base + 1);
		for (int k = 1; k <= 3000; ++k) {
			const double tick = At(anchor, k);
			assert(Receive(slots, 7, base + 4U + static_cast<std::uint32_t>(k), tick));
			assert(Next(buffer, tick).kind == DecisionKind::Play);
			assert(k != 1499 || buffer.depthFrames == 4);
			assert(k != 1500 || buffer.depthFrames == 3);
			assert(k != 2999 || buffer.depthFrames == 3);
			assert(k != 3000 || buffer.depthFrames == 2);
		}
	}
	{
		VoiceSlots slots{};
		VoiceSlot& slot = Admit(slots, 7, T0);
		for (int k = 0; k < 1500; ++k) {
			static_cast<void>(Next(slot.jitter, At(T0, k)));
		}
		assert(Next(slot.jitter, At(T0, 1500)).kind == DecisionKind::NoneYet);
		assert(slot.jitter.depthFrames == JitterInitialDepthFrames);
	}
	{
		VoiceSlots slots{};
		Admit(slots, 7, T0);
		assert(Receive(slots, 7, 2001, T0 + 10.0));
		assert(Receive(slots, 7, 2002, T0 + 10.0));
		assert(Receive(slots, 7, 2003, T0 + 10.0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0 + 10.0).sequence == 2001);
		assert(Next(buffer, T0 + 39.99).kind == DecisionKind::Play);
		assert(buffer.depthFrames == JitterInitialDepthFrames);
		assert(Receive(slots, 7, 2004, T0 + 39.99));
		assert(Next(buffer, T0 + 40.0).kind == DecisionKind::Play);
		assert(buffer.depthFrames == JitterMinDepthFrames);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 2401, T0, true, false));
		assert(Receive(slots, 7, 2402, T0));
		assert(Receive(slots, 7, 2403, T0, false, true));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, T0).sequence == 2401);
		assert(Next(buffer, At(T0, 1)).sequence == 2402);
		assert(Next(buffer, At(T0, 2)).sequence == 2403);
		assert(Receive(slots, 7, 2600, At(T0, 2)));
		assert(Receive(slots, 7, 2601, At(T0, 2)));
		assert(Receive(slots, 7, 2602, At(T0, 2)));
		const Decision restart = Next(buffer, At(T0, 3));
		assert(restart.kind == DecisionKind::Play && restart.sequence == 2600 && restart.resetDecoder);
		assert(!Next(buffer, At(T0, 4)).resetDecoder);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 1, 10, 1.0));
		assert(Receive(slots, 2, 10, 2.0));
		assert(Receive(slots, 3, 10, 3.0));
		assert(Receive(slots, 4, 10, 4.0));
		assert(Receive(slots, 5, 10, 5.0));
		assert(Find(slots, 1) == nullptr);
		assert(Find(slots, 5) != nullptr && Find(slots, 2) != nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 2, 10, 1.0));
		assert(Receive(slots, 1, 10, 1.0));
		assert(Receive(slots, 3, 10, 2.0));
		assert(Receive(slots, 4, 10, 2.0));
		assert(Receive(slots, 5, 10, 3.0));
		assert(Find(slots, 1) == nullptr);
		assert(Find(slots, 2) != nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 1, 10, 1.0));
		assert(Receive(slots, 2, 10, 2.0));
		assert(Receive(slots, 3, 10, 3.0));
		assert(Receive(slots, 4, 10, 4.0));
		assert(!Receive(slots, 5, 10, 5.0, false, false, std::span<const std::byte>{}));
		assert(!Receive(slots, 5, 0, 5.0));
		assert(Find(slots, 5) == nullptr);
		assert(Find(slots, 1) != nullptr && Find(slots, 2) != nullptr && Find(slots, 3) != nullptr && Find(slots, 4) != nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 10, 2.0, true, false));
		assert(Receive(slots, 7, 11, 2.0, true, false));
		assert(Receive(slots, 7, 12, 2.0));
		JitterBuffer& buffer = Find(slots, 7)->jitter;
		assert(Next(buffer, 2.0).sequence == 10);
		const Decision flagged = Next(buffer, 2.02);
		assert(flagged.kind == DecisionKind::Play && flagged.sequence == 11 && flagged.resetDecoder);
		assert(Next(buffer, 2.04).sequence == 12);
		for (int k = 3; k <= 5; ++k) {
			assert(Next(buffer, 2.0 + k * 0.02).kind == DecisionKind::Conceal);
		}
		assert(Next(buffer, 2.12).kind == DecisionKind::Silence);
		Expire(slots, 2.3);
		assert(Find(slots, 7) == nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 10, 1.0));
		assert(Receive(slots, 7, 11, 1.2));
		Expire(slots, 1.4);
		assert(Find(slots, 7) != nullptr);
		Expire(slots, 1.5);
		assert(Find(slots, 7) == nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 10, 9.7));
		assert(!Receive(slots, 7, 10, 9.9));
		assert(!Receive(slots, 7, 0, 10.0));
		Expire(slots, 10.0);
		assert(Find(slots, 7) == nullptr);
	}
	{
		VoiceSlots slots{};
		assert(Receive(slots, 7, 10, 1.0));
		Remove(slots, 7);
		assert(Find(slots, 7) == nullptr);
	}
	return 0;
}
