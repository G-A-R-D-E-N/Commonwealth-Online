#include "F4TVoiceReceiveRig.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReceiveRig
{
	namespace
	{
		void TestFourSpeakersClipAtFullScale()
		{
			Bench bench;
			std::array<Talker, 4> talkers{};
			float highest = 0.0F;
			float lowest = 0.0F;
			for (std::uint32_t tick = 0; tick < 40; ++tick) {
				for (std::uint16_t id = 1; id <= 4; ++id) {
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud, tick * kTickFrames)), tick == 0);
				}
				for (const float sample : Pull(bench)) {
					highest = (std::max)(highest, sample);
					lowest = (std::min)(lowest, sample);
				}
				Require(bench.receiver.Speakers().size() <= 4U);
			}
			Require(highest == 1.0F);
			Require(lowest == -1.0F);
		}

		void TestSixSpeakersNeverExceedFourLive()
		{
			Bench bench;
			std::array<Talker, 6> talkers{};
			for (std::uint32_t tick = 0; tick < 40; ++tick) {
				for (std::uint16_t id = 1; id <= 6; ++id) {
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud, tick * kTickFrames)), tick == 0);
				}
				Require(MaxAbs(Pull(bench)) <= 1.0F);
				Require(bench.receiver.Speakers().size() <= 4U);
			}
			float drained = 0.0F;
			for (std::uint32_t tick = 40; tick < 60; ++tick) {
				for (std::uint16_t id = 1; id <= 4; ++id) {
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud, tick * kTickFrames)));
				}
				drained = MaxAbs(Pull(bench));
				Require(bench.receiver.Speakers().size() <= 4U);
			}
			const auto settled = bench.receiver.Speakers();
			Require(drained > 0.0F);
			Require(settled.size() == 4U);
			for (std::uint16_t id = 1; id <= 4; ++id) {
				Require(Contains(settled, id));
			}
		}

		void TestFifthSpeakerReplacesOldestSlot()
		{
			Bench bench;
			std::array<Talker, 5> talkers{};
			for (std::uint32_t tick = 0; tick < 40; ++tick) {
				for (std::uint16_t id = 1; id <= 5; ++id) {
					const bool silentFirst = id == 1 && tick >= 9;
					const bool notYet = id == 5 && tick < 10;
					if (silentFirst || notYet) {
						continue;
					}
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud / 2, tick * kTickFrames)), tick == 0 || id == 5);
				}
				Pull(bench);
			}
			const auto speakers = bench.receiver.Speakers();
			Require(speakers.size() == 4U);
			Require(Contains(speakers, 5));
			Require(!Contains(speakers, 1));
		}

		void TestReplacedSpeakerIgnoresDelayedTrafficAndRejoins()
		{
			Bench bench;
			std::array<Talker, 5> talkers{};
			auto talk = [&](std::uint16_t a_id, std::uint32_t a_tick, bool a_burstStart) {
				Send(bench, a_id, a_tick + 1U, Encode(talkers[a_id - 1U].encoder, Tone(kLoud / 2, a_tick * kTickFrames)), a_burstStart);
			};
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				for (std::uint16_t id = 1; id <= 4; ++id) {
					talk(id, tick, tick == 0);
				}
				Pull(bench);
			}
			for (std::uint32_t tick = 10; tick < 20; ++tick) {
				for (std::uint16_t id = 2; id <= 5; ++id) {
					talk(id, tick, id == 5 && tick == 10);
				}
				if (tick == 11) {
					Send(bench, 1, 3U, Encode(talkers[0].encoder, Tone(kLoud / 2, 2 * kTickFrames)));
				}
				Pull(bench);
			}
			const auto settled = bench.receiver.Speakers();
			Require(settled.size() == 4U);
			Require(Contains(settled, 5));
			Require(!Contains(settled, 1));
			for (std::uint32_t tick = 20; tick < 40; ++tick) {
				for (const std::uint16_t id : std::array<std::uint16_t, 4>{ 1, 3, 4, 5 }) {
					talk(id, tick, id == 1 && tick == 20);
				}
				Pull(bench);
			}
			const auto rejoined = bench.receiver.Speakers();
			Require(rejoined.size() == 4U);
			Require(Contains(rejoined, 1));
			Require(!Contains(rejoined, 2));
		}

		void TestFifthSpeakerStartFlagLostPlaysInTime(bool a_dropFirst)
		{
			Bench bench;
			std::array<Talker, 5> talkers{};
			for (std::uint16_t id = 1; id <= 4; ++id) {
				bench.receiver.SetSpeakerVolume(id, 0);
			}
			std::uint32_t audibleTick = 100U;
			const std::uint32_t firstAccepted = a_dropFirst ? 11U : 10U;
			for (std::uint32_t tick = 0; tick < 30; ++tick) {
				for (std::uint16_t id = 1; id <= 5; ++id) {
					const bool replaced = id == 1 && tick >= 10U;
					const bool notYet = id == 5 && tick < 10U;
					const bool dropped = id == 5 && a_dropFirst && tick == 10U;
					if (replaced || notYet || dropped) {
						continue;
					}
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud / 2, tick * kTickFrames)), tick == 0 && id <= 4);
				}
				const auto out = Pull(bench);
				if (audibleTick == 100U && tick >= 10U && MaxAbs(out) > 0.0F) {
					audibleTick = tick;
				}
			}
			Require(audibleTick <= firstAccepted + F4T::Voice::JitterInitialDepthFrames + 1U);
		}

		float PeakAt(double a_x, double a_y, double a_z)
		{
			Bench bench;
			bench.receiver.SetPositions(ListenerPose{ 0.0, 0.0, 0.0, 0.0 }, std::array<SpeakerPosition, 1>{ SpeakerPosition{ 1, a_x, a_y, a_z } });
			Talker talker;
			std::vector<float> out;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				out = Pull(bench);
			}
			return MaxAbs(out);
		}

		std::vector<float> LastOutput(bool a_positioned)
		{
			Bench bench;
			if (a_positioned) {
				bench.receiver.SetPositions(ListenerPose{ 0.0, 0.0, 0.0, 0.0 }, std::array<SpeakerPosition, 1>{ SpeakerPosition{ 1, 0.0, 0.0, 0.0 } });
			}
			Talker talker;
			std::vector<float> out;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				out = Pull(bench);
			}
			return out;
		}

		void TestSpeakerWithoutPositionPlaysCentred()
		{
			const auto unplaced = LastOutput(false);
			Require(MaxAbs(unplaced) > 0.0F);
			Require(ChannelPeak(unplaced, 0) == ChannelPeak(unplaced, 1));
			Require(unplaced == LastOutput(true));
		}

		void TestCoalescedTakeoverHasNoStep()
		{
			Bench bench;
			std::array<Talker, 5> talkers{};
			for (std::uint16_t id = 2; id <= 4; ++id) {
				bench.receiver.SetSpeakerVolume(id, 0);
			}
			std::vector<float> before;
			for (std::uint32_t tick = 0; tick < 9; ++tick) {
				for (std::uint16_t id = 1; id <= 4; ++id) {
					Send(bench, id, tick + 1U, Encode(talkers[id - 1U].encoder, Tone(kLoud, tick * kTickFrames)), tick == 0);
				}
				before = Pull(bench);
			}
			for (std::uint32_t packet = 0; packet < 3U; ++packet) {
				Send(bench, 5, packet + 1U, Encode(talkers[4].encoder, Tone(kLoud / 2, (9U + packet) * kTickFrames)), packet == 0U);
			}
			const auto takeover = Pull(bench);
			Require(Contains(bench.receiver.Speakers(), 5));
			Require(!Contains(bench.receiver.Speakers(), 1));
			Require(BoundaryStep(before, takeover) <= 0.01F);
		}

		void TestRangeIsThreeDimensional()
		{
			Require(PeakAt(0.0, 0.0, 2000.0) == 0.0F);
			Require(PeakAt(800.0, 0.0, 500.0) < PeakAt(800.0, 0.0, 0.0));
		}

		void TestSpeakerPanFollowsListenerRight()
		{
			Bench bench;
			bench.receiver.SetPositions(ListenerPose{ 0.0, 0.0, 0.0, 0.0 }, std::array<SpeakerPosition, 1>{ SpeakerPosition{ 1, 100.0, 0.0, 0.0 } });
			Talker talker;
			std::vector<float> out;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				out = Pull(bench);
			}
			Require(ChannelPeak(out, 1) > 0.1F);
			Require(ChannelPeak(out, 0) < 1e-4F);
		}

		void TestMuteSilencesSpeakerWithinOneTick()
		{
			Bench bench;
			Talker talker;
			std::vector<float> last;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				last = Pull(bench);
			}
			Require(MaxAbs(last) > 0.0F);
			const auto concealedBeforeMute = bench.receiver.Stats().concealed;
			bench.receiver.SetSpeakerMuted(1, true);
			Require(!Send(bench, 1, 11U, Encode(talker.encoder, Tone(kLoud, 10 * kTickFrames))));
			const auto muted = Pull(bench);
			Require(MaxAbs(muted, kRampEnd, muted.size()) == 0.0F);
			Require(!Contains(bench.receiver.Speakers(), 1));
			for (std::uint32_t tick = 11; tick < 17; ++tick) {
				Pull(bench);
			}
			Require(bench.receiver.Stats().concealed == concealedBeforeMute);
			bench.receiver.SetSpeakerMuted(1, false);
			Require(MaxAbs(Pull(bench)) == 0.0F);
			Require(!Contains(bench.receiver.Speakers(), 1));
			float rejoined = 0.0F;
			for (std::uint32_t tick = 17; tick < 22; ++tick) {
				Require(Send(bench, 1, tick - 5U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames))));
				rejoined = (std::max)(rejoined, MaxAbs(Pull(bench)));
			}
			Require(Contains(bench.receiver.Speakers(), 1));
			Require(rejoined > 0.0F);
		}

		void TestMuteFadesWithoutStep()
		{
			Bench bench;
			Talker talker;
			std::vector<float> last;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				last = Pull(bench);
			}
			const float step = LargestStep(last);
			Require(step > 0.0F);
			bench.receiver.SetSpeakerMuted(1, true);
			const auto muted = Pull(bench);
			Require(std::fabs(muted[0] - last[last.size() - 2U]) <= 2.0F * step);
		}

		void TestDeafenSilencesEverything()
		{
			Bench bench;
			Talker talker;
			std::vector<float> last;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				last = Pull(bench);
			}
			Require(MaxAbs(last) > 0.0F);
			bench.receiver.SetDeafened(true);
			Require(!Send(bench, 1, 11U, Encode(talker.encoder, Tone(kLoud, 10 * kTickFrames))));
			Require(MaxAbs(Pull(bench)) == 0.0F);
			Require(bench.receiver.Speakers().empty());
			bench.receiver.SetDeafened(false);
		}

		void TestVolumeAndMasterZeroSilence()
		{
			Bench bench;
			Talker talker;
			std::uint32_t sequence = 1U;
			auto speak = [&](std::uint32_t a_tick) {
				Require(Send(bench, 1, sequence, Encode(talker.encoder, Tone(kLoud, a_tick * kTickFrames)), sequence == 1U));
				sequence += 1U;
				return Pull(bench);
			};
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				speak(tick);
			}
			bench.receiver.SetSpeakerVolume(1, 0);
			Require(MaxAbs(speak(10), kRampEnd, 2U * kTickFrames) == 0.0F);
			bench.receiver.SetSpeakerVolume(1, 100);
			Require(MaxAbs(speak(11)) > 0.0F);
			bench.receiver.SetMasterVolume(0);
			Require(MaxAbs(speak(12), kRampEnd, 2U * kTickFrames) == 0.0F);
			bench.receiver.SetMasterVolume(100);
			Require(MaxAbs(speak(13)) > 0.0F);
		}

		void TestRemoveSpeakerDropsSlotAtNextTick()
		{
			Bench bench;
			Talker talker;
			std::vector<float> last;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				last = Pull(bench);
			}
			Require(MaxAbs(last) > 0.0F);
			bench.receiver.RemoveSpeaker(1);
			const auto removed = Pull(bench);
			Require(MaxAbs(removed, kRampEnd, removed.size()) == 0.0F);
			Require(bench.receiver.Speakers().empty());
		}

		void TestRemoveSpeakerNotDroppedWhenManyQueued()
		{
			Bench bench;
			Talker talker;
			for (std::uint32_t tick = 0; tick < 10; ++tick) {
				Require(Send(bench, 1, tick + 1U, Encode(talker.encoder, Tone(kLoud, tick * kTickFrames)), tick == 0));
				Pull(bench);
			}
			for (std::uint16_t id = 100; id < 140; ++id) {
				bench.receiver.RemoveSpeaker(id);
			}
			bench.receiver.RemoveSpeaker(1);
			const auto removed = Pull(bench);
			Require(MaxAbs(removed, kRampEnd, removed.size()) == 0.0F);
			Require(bench.receiver.Speakers().empty());
		}

		void TestStopWithoutStartIsSafe()
		{
			Bench bench;
			bench.receiver.Stop();
			bench.receiver.Stop();
			Require(!bench.receiver.OutputFailed());
		}
	}

	void RunSpeakerTests()
	{
		TestFourSpeakersClipAtFullScale();
		TestSixSpeakersNeverExceedFourLive();
		TestFifthSpeakerReplacesOldestSlot();
		TestReplacedSpeakerIgnoresDelayedTrafficAndRejoins();
		TestFifthSpeakerStartFlagLostPlaysInTime(false);
		TestFifthSpeakerStartFlagLostPlaysInTime(true);
		TestRangeIsThreeDimensional();
		TestSpeakerPanFollowsListenerRight();
		TestSpeakerWithoutPositionPlaysCentred();
		TestCoalescedTakeoverHasNoStep();
		TestMuteSilencesSpeakerWithinOneTick();
		TestMuteFadesWithoutStep();
		TestDeafenSilencesEverything();
		TestVolumeAndMasterZeroSilence();
		TestRemoveSpeakerDropsSlotAtNextTick();
		TestRemoveSpeakerNotDroppedWhenManyQueued();
		TestStopWithoutStartIsSafe();
	}
}
