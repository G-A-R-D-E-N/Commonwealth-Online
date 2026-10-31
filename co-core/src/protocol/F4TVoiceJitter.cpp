#include "F4TVoiceJitter.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace F4T::Voice
{
	namespace
	{
		std::int64_t ElapsedUs(double a_nowSeconds, double a_sinceSeconds) noexcept
		{
			return std::llround((a_nowSeconds - a_sinceSeconds) * 1000000.0);
		}

		std::uint32_t Periods(std::uint32_t a_from, std::uint32_t a_to) noexcept
		{
			const auto distance = a_to - a_from;
			return a_to < a_from ? distance - 1U : distance;
		}

		std::uint32_t Previous(std::uint32_t a_sequence) noexcept
		{
			return a_sequence == 1U ? 0xFFFFFFFFU : a_sequence - 1U;
		}

		std::span<const std::byte> Staged(const JitterBuffer& a_buffer) noexcept
		{
			return { a_buffer.playout.data(), a_buffer.playoutSize };
		}

		void StagePlayout(JitterBuffer& a_buffer, const JitterFrame& a_frame) noexcept
		{
			a_buffer.playoutSize = a_frame.size;
			std::copy_n(a_frame.data.begin(), a_frame.size, a_buffer.playout.begin());
		}

		void RemoveFront(JitterBuffer& a_buffer) noexcept
		{
			std::copy(a_buffer.frames.begin() + 1, a_buffer.frames.begin() + a_buffer.count, a_buffer.frames.begin());
			a_buffer.count -= 1U;
		}

		void RecordUnderrun(JitterBuffer& a_buffer, double a_nowSeconds) noexcept
		{
			a_buffer.underrunEvents += 1U;
			if (a_buffer.hasUnderrun && ElapsedUs(a_nowSeconds, a_buffer.lastUnderrunSeconds) <= JitterUnderrunWindowMs * 1000) {
				a_buffer.hasUnderrun = false;
				a_buffer.depthFrames = std::min(a_buffer.depthFrames + 1U, JitterMaxDepthFrames);
				return;
			}
			a_buffer.hasUnderrun = true;
			a_buffer.lastUnderrunSeconds = a_nowSeconds;
		}

		void AdaptDepth(JitterBuffer& a_buffer, double a_nowSeconds) noexcept
		{
			if (a_buffer.playing && a_buffer.depthFrames > JitterMinDepthFrames && ElapsedUs(a_nowSeconds, a_buffer.quietSinceSeconds) >= JitterQuietMs * 1000) {
				a_buffer.depthFrames -= 1U;
				a_buffer.quietSinceSeconds = a_nowSeconds;
			}
		}

		Decision Concealed(JitterBuffer& a_buffer, std::uint32_t a_expected) noexcept
		{
			if (a_buffer.concealStreak < JitterMaxConcealedFrames) {
				a_buffer.concealStreak += 1U;
				return Decision{ DecisionKind::Conceal, a_expected, {}, false };
			}
			return Decision{ DecisionKind::Silence, a_expected, {}, false };
		}

		Decision PlayFront(JitterBuffer& a_buffer, std::uint32_t a_expected) noexcept
		{
			const JitterFrame front = a_buffer.frames[0];
			StagePlayout(a_buffer, front);
			RemoveFront(a_buffer);
			a_buffer.cursor = a_expected;
			a_buffer.concealStreak = 0;
			if (front.burstEnd) {
				a_buffer.playing = false;
			}
			return Decision{ DecisionKind::Play, a_expected, Staged(a_buffer), front.burstStart };
		}

		void Reanchor(JitterBuffer& a_buffer) noexcept
		{
			a_buffer.cursor = Previous(a_buffer.frames[0].sequence);
			a_buffer.playing = false;
			a_buffer.resetPending = true;
		}

		Decision Advance(JitterBuffer& a_buffer, double a_nowSeconds) noexcept
		{
			if (a_buffer.cursor != 0 && a_buffer.count > 0 && Periods(a_buffer.cursor, a_buffer.frames[0].sequence) > JitterResetJumpTicks) {
				Reanchor(a_buffer);
			}
			if (a_buffer.inDryRun && a_buffer.count > 0) {
				a_buffer.playing = false;
			}
			if (!a_buffer.playing) {
				if (a_buffer.count == 0) {
					a_buffer.waitTicks = 0;
					return {};
				}
				const bool covered = Periods(a_buffer.frames[0].sequence, a_buffer.frames[a_buffer.count - 1].sequence) + 1U >= a_buffer.depthFrames;
				if (!covered && a_buffer.waitTicks < a_buffer.depthFrames) {
					a_buffer.waitTicks += 1U;
					return {};
				}
				if (!a_buffer.inDryRun) {
					a_buffer.cursor = Previous(a_buffer.frames[0].sequence);
				}
				a_buffer.inDryRun = false;
				a_buffer.waitTicks = 0;
				a_buffer.quietSinceSeconds = a_nowSeconds;
				a_buffer.playing = true;
			}
			const auto expected = GnsSnapshotEnvelope::NextSequence(a_buffer.cursor);
			if (a_buffer.count == 0) {
				a_buffer.quietSinceSeconds = a_nowSeconds;
				if (!a_buffer.inDryRun) {
					RecordUnderrun(a_buffer, a_nowSeconds);
				}
				a_buffer.inDryRun = true;
				return Concealed(a_buffer, expected);
			}
			a_buffer.inDryRun = false;
			if (a_buffer.frames[0].sequence == expected) {
				return PlayFront(a_buffer, expected);
			}
			a_buffer.cursor = expected;
			if (a_buffer.frames[0].sequence == GnsSnapshotEnvelope::NextSequence(expected)) {
				a_buffer.concealStreak = 0;
				StagePlayout(a_buffer, a_buffer.frames[0]);
				return Decision{ DecisionKind::Fec, expected, Staged(a_buffer), false };
			}
			return Concealed(a_buffer, expected);
		}

		void DrainStandingLag(JitterBuffer& a_buffer) noexcept
		{
			if (!a_buffer.playing || a_buffer.inDryRun) {
				a_buffer.drainWindowTicks = 0;
				a_buffer.drainWindowMinLag = 0xFFFFFFFFU;
				return;
			}
			const std::uint32_t lag = a_buffer.count == 0 ? 0U : Periods(a_buffer.cursor, a_buffer.frames[a_buffer.count - 1U].sequence);
			a_buffer.drainWindowMinLag = std::min(a_buffer.drainWindowMinLag, lag);
			a_buffer.drainWindowTicks += 1U;
			if (a_buffer.drainWindowTicks < JitterDrainWindowTicks) {
				return;
			}
			if (a_buffer.drainWindowMinLag > a_buffer.depthFrames + 1U) {
				const std::uint32_t skip = a_buffer.drainWindowMinLag - a_buffer.depthFrames;
				if (skip > JitterResetJumpTicks) {
					Reanchor(a_buffer);
				} else {
					for (std::uint32_t step = 0; step < skip; ++step) {
						a_buffer.cursor = GnsSnapshotEnvelope::NextSequence(a_buffer.cursor);
					}
					while (a_buffer.count > 0 && !GnsSnapshotEnvelope::IsNewer(a_buffer.frames[0].sequence, a_buffer.cursor)) {
						RemoveFront(a_buffer);
					}
				}
				a_buffer.drainEvents += 1U;
			}
			a_buffer.drainWindowTicks = 0;
			a_buffer.drainWindowMinLag = 0xFFFFFFFFU;
		}

		SavedSpeaker* FindSaved(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept
		{
			for (auto& entry : a_slots.saved) {
				if (entry.used && entry.speakerId == a_speakerId) {
					return &entry;
				}
			}
			return nullptr;
		}

		std::uint32_t DeadlineFloor(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept
		{
			for (auto& slot : a_slots.slots) {
				if (slot.used && slot.speakerId == a_speakerId) {
					return slot.jitter.cursor;
				}
			}
			const SavedSpeaker* saved = FindSaved(a_slots, a_speakerId);
			return saved != nullptr ? saved->lastPlayedSequence : 0U;
		}

		void SaveSpeaker(VoiceSlots& a_slots, const VoiceSlot& a_slot) noexcept
		{
			if (!a_slot.used || a_slot.jitter.cursor == 0) {
				return;
			}
			SavedSpeaker* target = FindSaved(a_slots, a_slot.speakerId);
			if (target == nullptr) {
				target = &*std::min_element(a_slots.saved.begin(), a_slots.saved.end(), [](const SavedSpeaker& a, const SavedSpeaker& b) {
					return a.used != b.used ? !a.used : a.savedSeconds < b.savedSeconds;
				});
			}
			*target = SavedSpeaker{ a_slot.speakerId, a_slot.jitter.cursor, a_slot.lastArrivalSeconds, true };
		}

		void EvictSlot(VoiceSlots& a_slots, VoiceSlot& a_slot) noexcept
		{
			SaveSpeaker(a_slots, a_slot);
			a_slot = VoiceSlot{};
		}
	}

	bool PacketValid(std::uint32_t a_sequence, std::span<const std::byte> a_payload) noexcept
	{
		return a_sequence != 0U && !a_payload.empty() && a_payload.size() <= JitterFrameBytes;
	}

	bool Insert(
		JitterBuffer& a_buffer,
		std::uint32_t a_sequence,
		std::span<const std::byte> a_payload,
		bool a_burstStart,
		bool a_burstEnd) noexcept
	{
		if (!PacketValid(a_sequence, a_payload) || !GnsSnapshotEnvelope::IsNewer(a_sequence, a_buffer.cursor)) {
			return false;
		}
		std::size_t position = 0;
		while (position < a_buffer.count && GnsSnapshotEnvelope::IsNewer(a_sequence, a_buffer.frames[position].sequence)) {
			position += 1U;
		}
		if (position < a_buffer.count && a_buffer.frames[position].sequence == a_sequence) {
			return false;
		}
		if (a_buffer.count == JitterMaxFrames) {
			if (position == 0) {
				return false;
			}
			RemoveFront(a_buffer);
			position -= 1U;
		}
		std::copy_backward(a_buffer.frames.begin() + position, a_buffer.frames.begin() + a_buffer.count, a_buffer.frames.begin() + a_buffer.count + 1);
		auto& frame = a_buffer.frames[position];
		frame.sequence = a_sequence;
		frame.burstStart = a_burstStart;
		frame.burstEnd = a_burstEnd;
		frame.size = a_payload.size();
		std::copy(a_payload.begin(), a_payload.end(), frame.data.begin());
		a_buffer.count += 1U;
		return true;
	}

	Decision Next(JitterBuffer& a_buffer, double a_nowSeconds) noexcept
	{
		AdaptDepth(a_buffer, a_nowSeconds);
		auto decision = Advance(a_buffer, a_nowSeconds);
		if (a_buffer.resetPending && (decision.kind == DecisionKind::Play || decision.kind == DecisionKind::Fec || decision.kind == DecisionKind::Conceal)) {
			decision.resetDecoder = true;
			a_buffer.resetPending = false;
		}
		DrainStandingLag(a_buffer);
		return decision;
	}

	void Reset(JitterBuffer& a_buffer) noexcept
	{
		a_buffer.cursor = 0;
		a_buffer.count = 0;
		a_buffer.playing = false;
		a_buffer.inDryRun = false;
		a_buffer.waitTicks = 0;
		a_buffer.drainWindowTicks = 0;
		a_buffer.drainWindowMinLag = 0xFFFFFFFFU;
		a_buffer.resetPending = true;
	}

	VoiceSlot& Admit(VoiceSlots& a_slots, std::uint16_t a_speakerId, double a_nowSeconds) noexcept
	{
		for (auto& slot : a_slots.slots) {
			if (slot.used && slot.speakerId == a_speakerId) {
				return slot;
			}
		}
		const std::uint32_t floor = DeadlineFloor(a_slots, a_speakerId);
		VoiceSlot* target = &*std::min_element(a_slots.slots.begin(), a_slots.slots.end(), [](const VoiceSlot& a, const VoiceSlot& b) {
			return a.used != b.used ? !a.used : a.lastArrivalSeconds != b.lastArrivalSeconds ? a.lastArrivalSeconds < b.lastArrivalSeconds : a.speakerId < b.speakerId;
		});
		EvictSlot(a_slots, *target);
		target->used = true;
		target->speakerId = a_speakerId;
		target->admittedSeconds = a_nowSeconds;
		target->jitter.cursor = floor;
		target->jitter.resetPending = true;
		return *target;
	}

	bool Accept(
		VoiceSlots& a_slots,
		std::uint16_t a_speakerId,
		std::uint32_t a_sequence,
		std::span<const std::byte> a_payload,
		bool a_burstStart,
		bool a_burstEnd,
		double a_nowSeconds) noexcept
	{
		if (!PacketValid(a_sequence, a_payload) || !GnsSnapshotEnvelope::IsNewer(a_sequence, DeadlineFloor(a_slots, a_speakerId))) {
			return false;
		}
		VoiceSlot& slot = Admit(a_slots, a_speakerId, a_nowSeconds);
		if (!Insert(slot.jitter, a_sequence, a_payload, a_burstStart, a_burstEnd)) {
			return false;
		}
		slot.lastArrivalSeconds = a_nowSeconds;
		return true;
	}

	void Expire(VoiceSlots& a_slots, double a_nowSeconds) noexcept
	{
		for (auto& slot : a_slots.slots) {
			const double lastActivity = std::max(slot.lastArrivalSeconds, slot.admittedSeconds);
			if (slot.used && ElapsedUs(a_nowSeconds, lastActivity) >= VoiceSlotExpiryMs * 1000) {
				EvictSlot(a_slots, slot);
			}
		}
	}

	void Remove(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept
	{
		for (auto& slot : a_slots.slots) {
			if (slot.used && slot.speakerId == a_speakerId) {
				EvictSlot(a_slots, slot);
			}
		}
	}

	void Forget(VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept
	{
		for (auto& slot : a_slots.slots) {
			if (slot.used && slot.speakerId == a_speakerId) {
				slot = VoiceSlot{};
			}
		}
		if (SavedSpeaker* saved = FindSaved(a_slots, a_speakerId)) {
			*saved = SavedSpeaker{};
		}
	}

	void ResetSession(VoiceSlots& a_slots) noexcept
	{
		a_slots = VoiceSlots{};
	}
}
