#include "F4TVoiceReceiveRig.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace ReceiveRig;

namespace
{
	void TestRingDropsNewestWhenFull()
	{
		ReceiveRing ring;
		ReceiveEntry entry{};
		entry.size = 1U;
		for (std::uint32_t sequence = 1; sequence <= ReceiveRingSlots; ++sequence) {
			entry.sequence = sequence;
			Require(ring.Push(entry));
		}
		entry.sequence = 9999U;
		for (int i = 0; i < 3; ++i) {
			Require(!ring.Push(entry));
		}
		Require(ring.Dropped() == 3U);
		ReceiveEntry out{};
		for (std::uint32_t sequence = 1; sequence <= ReceiveRingSlots; ++sequence) {
			Require(ring.Pop(out));
			Require(out.sequence == sequence);
		}
		Require(!ring.Pop(out));
	}

	ReceiveStats RunLoss(std::uint32_t a_dropEvery, std::uint32_t a_ticks)
	{
		Bench bench;
		Talker talker;
		for (std::uint32_t tick = 0; tick < a_ticks; ++tick) {
			const std::uint32_t sequence = tick + 1U;
			const auto payload = Encode(talker.encoder, Tone(kLoud / 2, tick * kTickFrames));
			if (sequence % a_dropEvery != 0U) {
				Require(Send(bench, 1, sequence, payload, tick == 0));
			}
			Pull(bench);
		}
		return bench.receiver.Stats();
	}

	void TestLossConcealmentAndFecRecovery()
	{
		constexpr std::uint32_t kTicks = 400;
		const ReceiveStats light = RunLoss(20U, kTicks);
		Require(light.recovered > 0U);
		Require(light.concealed * 100U <= 6U * kTicks);
		const ReceiveStats heavy = RunLoss(7U, kTicks);
		Require((heavy.concealed + heavy.recovered) * 100U <= 16U * kTicks);
	}

	void TestSingleLossRecoveredByFecNotConcealed()
	{
		Bench bench;
		Talker talker;
		std::vector<float> lost;
		for (std::uint32_t tick = 0; tick < 20; ++tick) {
			const auto payload = Encode(talker.encoder, Tone(kLoud / 2, tick * kTickFrames));
			if (tick != 9) {
				Require(Send(bench, 1, tick + 1U, payload, tick == 0));
			}
			const auto out = Pull(bench);
			if (tick == 9) {
				lost = out;
			}
		}
		Require(bench.receiver.Stats().recovered == 1U);
		Require(bench.receiver.Stats().concealed == 0U);
		Require(MaxAbs(lost) > 0.0F);
	}

	void TestThreeConcealedThenSilence()
	{
		Bench bench;
		Talker talker;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			Pull(bench);
		}
		std::vector<float> tail;
		for (std::uint32_t tick = 10; tick < 20; ++tick) {
			tail = Pull(bench);
		}
		Require(bench.receiver.Stats().concealed == 3U);
		Require(MaxAbs(tail) == 0.0F);
	}

	void TestConcealmentFadeLastsTwentyMs()
	{
		Bench bench;
		Talker talker;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			Pull(bench);
		}
		std::vector<std::vector<float>> out;
		std::vector<std::uint64_t> concealed;
		for (std::uint32_t tick = 10; tick < 30; ++tick) {
			out.push_back(Pull(bench));
			concealed.push_back(bench.receiver.Stats().concealed);
		}
		std::size_t third = 0;
		while (third < concealed.size() && concealed[third] < 3U) {
			third += 1U;
		}
		Require(third + 2U < out.size());
		Require(concealed[third] == 3U && concealed[third + 1U] == 3U);
		const auto& fourth = out[third + 1U];
		Require(std::fabs(fourth[0] - out[third][out[third].size() - 2U]) <= 2.0F * LargestStep(out[third]));
		Require(MaxAbs(fourth, 0, kRampEnd) > 0.0F);
		Require(MaxAbs(fourth, kRampEnd, fourth.size()) > 0.0F);
		Require(MaxAbs(out[third + 2U]) == 0.0F);
	}

	void TestLateNextPacketConcealedThenResumes()
	{
		Bench bench;
		Talker talker;
		std::vector<std::vector<std::byte>> payloads;
		for (std::uint32_t tick = 0; tick < 20; ++tick) {
			payloads.push_back(Encode(talker.encoder, Tone(kLoud / 2, tick * kTickFrames)));
		}
		std::vector<std::vector<float>> out;
		for (std::uint32_t tick = 0; tick < 20; ++tick) {
			const std::uint32_t sequence = tick + 1U;
			if (sequence != 10U && sequence != 11U) {
				Require(Send(bench, 1, sequence, payloads[tick], tick == 0));
			}
			if (tick == 12U) {
				Require(Send(bench, 1, 11U, payloads[10]));
			}
			out.push_back(Pull(bench));
		}
		Require(bench.receiver.Stats().concealed == 1U);
		Require(bench.receiver.Stats().recovered == 0U);
		Require(MaxAbs(out[11]) > 0.0F);
		Require(MaxAbs(out[13]) > 0.0F);
	}

	void BurstRestartKeepsLevel(bool a_endFirstBurst)
	{
		Bench bench;
		Talker talker;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0, a_endFirstBurst && tick == 9));
			Pull(bench);
		}
		talker.encoder.Reset();
		float weakest = 1.0F;
		for (std::uint32_t tick = 10; tick < 22; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 10));
			weakest = (std::min)(weakest, MaxAbs(Pull(bench)));
		}
		Require(weakest > 0.6F);
	}

	void TestBurstRestartAfterEndKeepsLevel()
	{
		BurstRestartKeepsLevel(true);
	}

	void TestBurstRestartWithoutEndKeepsLevel()
	{
		BurstRestartKeepsLevel(false);
	}

	void TestBurstRestartWithoutEndHasNoStep()
	{
		Bench bench;
		Talker talker;
		std::vector<float> last;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			last = Pull(bench);
		}
		last = Pull(bench);
		last = Pull(bench);
		talker.encoder.Reset();
		Require(Send(bench, 1, 11U, Encode(talker.encoder, Tone(kLoud, 10 * kTickFrames)), true));
		const auto restart = Pull(bench);
		Require(MaxAbs(restart) > 0.0F);
		Require(BoundaryStep(last, restart) <= 0.01F);
	}

	void TestBurstRestartResumesAudible()
	{
		Bench bench;
		Talker first;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(first.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0, tick == 9));
			Pull(bench);
		}
		for (std::uint32_t tick = 10; tick < 15; ++tick) {
			Pull(bench);
		}
		Talker second;
		std::uint32_t firstAudible = 100U;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, 16U + tick, Encode(second.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			const auto out = Pull(bench);
			if (firstAudible == 100U && MaxAbs(out) > 0.0F) {
				firstAudible = tick;
			}
		}
		Require(firstAudible <= 4U);
		Require(Contains(bench.receiver.Speakers(), 1));
	}

	void TestBurstEndFadesWithoutStep()
	{
		Bench bench;
		Talker talker;
		std::vector<float> lastSpeaking;
		std::vector<float> afterSpeaking;
		std::vector<float> out;
		bool speakingBefore = false;
		for (std::uint32_t tick = 0; tick < 20; ++tick) {
			if (tick < 10U) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0, tick == 9));
			}
			out = Pull(bench);
			const bool speaking = Contains(bench.receiver.Speakers(), 1);
			if (speakingBefore && !speaking) {
				afterSpeaking = out;
			}
			if (speaking) {
				lastSpeaking = out;
			}
			speakingBefore = speaking;
		}
		const float step = LargestStep(lastSpeaking);
		Require(step > 0.0F);
		Require(!afterSpeaking.empty());
		Require(std::fabs(afterSpeaking[0] - lastSpeaking[lastSpeaking.size() - 2U]) <= 2.0F * step);
		Require(MaxAbs(out) == 0.0F);
	}

	void TestReconnectDropsStaleAndResumes()
	{
		Bench bench;
		Talker talker;
		std::vector<float> last;
		for (std::uint32_t tick = 0; tick < 15; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			last = Pull(bench);
		}
		Require(bench.receiver.Speakers().size() == 1U);
		const float step = LargestStep(last);
		Require(step > 0.0F);
		Require(Send(bench, 1, 16U, Encode(talker.encoder, Tone(kLoud, 15 * kTickFrames))));
		bench.receiver.ResetSession();
		const auto faded = Pull(bench);
		Require(std::fabs(faded[0] - last[last.size() - 2U]) <= 2.0F * step);
		Require(MaxAbs(faded, 0, kRampEnd) > 0.0F);
		Require(MaxAbs(faded, kRampEnd, faded.size()) == 0.0F);
		Require(bench.receiver.Speakers().empty());

		Talker reconnected;
		std::uint32_t firstAudible = 100U;
		for (std::uint32_t tick = 0; tick < 10; ++tick) {
			Require(Send(bench, 1, tick + 1U, Encode(reconnected.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
			const auto out = Pull(bench);
			if (firstAudible == 100U && MaxAbs(out) > 0.0F) {
				firstAudible = tick;
			}
		}
		Require(firstAudible <= 4U);
		Require(bench.receiver.Speakers().size() == 1U);
	}

	std::vector<float> Stream(const std::vector<std::vector<std::byte>>& a_payloads, const std::vector<std::uint32_t>& a_chunks)
	{
		Bench bench;
		for (std::size_t i = 0; i < a_payloads.size(); ++i) {
			Require(Send(bench, 1, static_cast<std::uint32_t>(i + 1U), a_payloads[i], i == 0));
		}
		constexpr std::uint64_t kTotal = 12U * MixerFrameSamples;
		std::vector<float> out;
		std::size_t step = 0;
		while (bench.frames < kTotal) {
			const auto wanted = a_chunks[step % a_chunks.size()];
			const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(wanted, kTotal - bench.frames));
			const auto part = Pull(bench, chunk);
			out.insert(out.end(), part.begin(), part.end());
			step += 1U;
		}
		return out;
	}

	void TestFillSizesStayContinuous()
	{
		Talker talker;
		std::vector<std::vector<std::byte>> payloads;
		for (std::size_t tick = 0; tick < 8U; ++tick) {
			payloads.push_back(Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)));
		}
		const auto reference = Stream(payloads, { kTickFrames });
		const auto varied = Stream(payloads, { 441U, 480U, 960U, 1000U });
		Require(MaxAbs(reference) > 0.0F);
		Require(reference == varied);
	}
}

int main()
{
	TestRingDropsNewestWhenFull();
	TestLossConcealmentAndFecRecovery();
	TestSingleLossRecoveredByFecNotConcealed();
	TestThreeConcealedThenSilence();
	TestConcealmentFadeLastsTwentyMs();
	TestLateNextPacketConcealedThenResumes();
	TestBurstRestartAfterEndKeepsLevel();
	TestBurstRestartWithoutEndKeepsLevel();
	TestBurstRestartWithoutEndHasNoStep();
	TestBurstRestartResumesAudible();
	TestBurstEndFadesWithoutStep();
	TestReconnectDropsStaleAndResumes();
	TestFillSizesStayContinuous();
	RunSpeakerTests();
	RunDecoderTests();
	return 0;
}
