#include "F4TVoiceReceiveRig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReceiveRig
{
	namespace
	{
		using Outputs = std::vector<std::vector<float>>;

		template <typename Step>
		Outputs Drive(std::uint32_t a_ticks, Step a_step)
		{
			Bench bench;
			Outputs out;
			for (std::uint32_t tick = 0; tick < a_ticks; ++tick) {
				a_step(bench, tick);
				out.push_back(Pull(bench));
			}
			return out;
		}

		std::vector<std::vector<std::byte>> Payloads(Talker& a_talker, std::uint32_t a_ticks, std::int16_t a_amplitude = kLoud)
		{
			std::vector<std::vector<std::byte>> out;
			for (std::uint32_t tick = 0; tick < a_ticks; ++tick) {
				out.push_back(Encode(a_talker.encoder, Tone(a_amplitude, tick * kTickFrames)));
			}
			return out;
		}

		void RequireSameFrom(const Outputs& a_actual, const Outputs& a_expected, std::size_t a_from)
		{
			Require(a_actual.size() == a_expected.size());
			float peak = 0.0F;
			for (std::size_t tick = a_from; tick < a_actual.size(); ++tick) {
				Require(a_actual[tick] == a_expected[tick]);
				peak = (std::max)(peak, MaxAbs(a_actual[tick]));
			}
			Require(peak > 0.0F);
		}

		void TestResetSessionMatchesFreshReceiver()
		{
			constexpr std::uint32_t kJoin = 10U;
			constexpr std::uint32_t kTicks = 22U;
			Talker first;
			Talker second;
			const auto firstPayloads = Payloads(first, kJoin);
			const auto secondPayloads = Payloads(second, kTicks - kJoin);
			const auto reused = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick < kJoin) {
					Require(Send(a_bench, 1, a_tick + 1U, firstPayloads[a_tick], a_tick == 0));
					return;
				}
				if (a_tick == kJoin) {
					a_bench.receiver.ResetSession();
				}
				Require(Send(a_bench, 1, a_tick - kJoin + 1U, secondPayloads[a_tick - kJoin], a_tick == kJoin));
			});
			const auto fresh = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick >= kJoin) {
					Require(Send(a_bench, 1, a_tick - kJoin + 1U, secondPayloads[a_tick - kJoin], a_tick == kJoin));
				}
			});
			RequireSameFrom(reused, fresh, kJoin + 1U);
		}

		void TestNewSpeakerInReusedSlotMatchesFreshReceiver()
		{
			constexpr std::uint32_t kJoin = 10U;
			constexpr std::uint32_t kTicks = 22U;
			Talker first;
			Talker second;
			const auto firstPayloads = Payloads(first, kJoin);
			const auto secondPayloads = Payloads(second, kTicks - kJoin);
			const auto reused = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick < kJoin) {
					Require(Send(a_bench, 1, a_tick + 1U, firstPayloads[a_tick], a_tick == 0));
					return;
				}
				if (a_tick == kJoin) {
					a_bench.receiver.RemoveSpeaker(1);
				}
				Require(Send(a_bench, 2, a_tick - kJoin + 2U, secondPayloads[a_tick - kJoin]));
			});
			const auto fresh = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick >= kJoin) {
					Require(Send(a_bench, 2, a_tick - kJoin + 2U, secondPayloads[a_tick - kJoin]));
				}
			});
			RequireSameFrom(reused, fresh, kJoin + 1U);
		}

		void TestBurstRestartAfterEndMatchesFreshReceiver()
		{
			constexpr std::uint32_t kRestart = 15U;
			constexpr std::uint32_t kTicks = 25U;
			Talker talker;
			const auto firstBurst = Payloads(talker, 10U);
			talker.encoder.Reset();
			const auto secondBurst = Payloads(talker, 10U);
			const auto reused = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick < 10U) {
					Require(Send(a_bench, 1, a_tick + 1U, firstBurst[a_tick], a_tick == 0, a_tick == 9U));
				} else if (a_tick >= kRestart) {
					Require(Send(a_bench, 1, a_tick - kRestart + 11U, secondBurst[a_tick - kRestart], a_tick == kRestart));
				}
			});
			const auto fresh = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				if (a_tick >= kRestart) {
					Require(Send(a_bench, 1, a_tick - kRestart + 11U, secondBurst[a_tick - kRestart], a_tick == kRestart));
				}
			});
			RequireSameFrom(reused, fresh, kRestart + 1U);
		}

		void TestFourTalkersMatchSumOfSolo()
		{
			constexpr std::uint32_t kTicks = 12U;
			std::array<std::vector<std::vector<std::byte>>, 4> payloads{};
			for (std::size_t speaker = 0; speaker < payloads.size(); ++speaker) {
				Talker talker;
				payloads[speaker] = Payloads(talker, kTicks, static_cast<std::int16_t>(3000 + 1000 * speaker));
			}
			std::array<Outputs, 4> solo{};
			for (std::size_t speaker = 0; speaker < solo.size(); ++speaker) {
				solo[speaker] = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
					Require(Send(a_bench, static_cast<std::uint16_t>(speaker + 1U), a_tick + 1U, payloads[speaker][a_tick], a_tick == 0));
				});
				Require(MaxAbs(solo[speaker].back()) > 0.0F);
			}
			const auto mixed = Drive(kTicks, [&](Bench& a_bench, std::uint32_t a_tick) {
				for (std::size_t speaker = 0; speaker < payloads.size(); ++speaker) {
					Require(Send(a_bench, static_cast<std::uint16_t>(speaker + 1U), a_tick + 1U, payloads[speaker][a_tick], a_tick == 0));
				}
			});
			float peak = 0.0F;
			for (std::uint32_t tick = 0; tick < kTicks; ++tick) {
				for (std::size_t sample = 0; sample < mixed[tick].size(); ++sample) {
					float sum = 0.0F;
					for (const auto& out : solo) {
						sum += out[tick][sample];
					}
					Require(std::fabs(mixed[tick][sample] - sum) <= 1e-6F);
					peak = (std::max)(peak, std::fabs(mixed[tick][sample]));
				}
			}
			Require(peak > 0.0F);
		}
	}

	void RunDecoderTests()
	{
		TestResetSessionMatchesFreshReceiver();
		TestNewSpeakerInReusedSlotMatchesFreshReceiver();
		TestBurstRestartAfterEndMatchesFreshReceiver();
		TestFourTalkersMatchSumOfSolo();
	}
}
