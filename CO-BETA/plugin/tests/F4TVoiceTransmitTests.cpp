#include "F4TVoiceTransmitRig.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using namespace TransmitRig;

namespace Testing = F4T::Voice::Testing;

namespace
{
	std::size_t BurstEnds(const Rig& a_rig)
	{
		return static_cast<std::size_t>(std::count_if(a_rig.packets.begin(), a_rig.packets.end(), [](const VoiceHeader& a_header) {
			return a_header.burstEnd;
		}));
	}

	void Suppress(Rig& a_rig, int a_mode, bool a_on)
	{
		if (a_mode == 0) {
			a_rig.config.micMuted = a_on;
			a_rig.tx.Configure(a_rig.config);
		} else if (a_mode == 1) {
			a_rig.tx.SetCanSend(!a_on);
		} else {
			a_rig.config.voiceChat = !a_on;
			a_rig.tx.Configure(a_rig.config);
		}
	}

	std::vector<std::byte> EncodeReference(Encoder& a_encoder, const Frame& a_pcm)
	{
		std::array<std::uint8_t, F4T::Voice::kMaxEncodedBytes> out{};
		const EncodeResult result = a_encoder.Encode(a_pcm.data(), out.data());
		assert(result.sendable);
		const auto bytes = std::as_bytes(std::span<const std::uint8_t>(out.data(), static_cast<std::size_t>(result.bytes)));
		return std::vector<std::byte>(bytes.begin(), bytes.end());
	}

	void TestPttHoldAndTailBurstEnd()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 50, kTone);
		assert(rig.tx.Status().inputs.transmitting);
		rig.keys[kPttKey] = false;
		for (int tick = 0; tick < 9; ++tick) {
			Run(rig, 1, kTone);
			assert(rig.tx.Status().inputs.transmitting);
		}
		Run(rig, 1, kTone);
		assert(!rig.tx.Status().inputs.transmitting);
		Run(rig, 10, kTone);
		assert(rig.packets.size() == 57);
		assert(rig.packets.front().sequence == 3);
		assert(rig.packets.front().burstStart);
		assert(rig.packets.back().sequence == 59);
		assert(rig.packets.back().burstEnd);
		assert(BurstEnds(rig) == 1);
		for (std::size_t i = 1; i < rig.packets.size(); ++i) {
			assert(rig.packets[i].sequence == rig.packets[i - 1].sequence + 1);
			assert(!rig.packets[i].burstStart);
		}
		assert(rig.tx.Status().sent == 57);
		assert(!rig.tx.Status().inputs.transmitting);
	}

	void TestInputBlockCancelsHold()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 10, kTone);
		rig.tx.SetInputAllowed(false);
		Run(rig, 10, kTone);
		assert(rig.packets.size() == 10);
		assert(rig.packets.back().burstEnd);
	}

	void TestInputBlockIgnoresPress()
	{
		Rig rig;
		RunFrame(rig, SineFrame(kSilence));
		rig.tx.SetInputAllowed(false);
		rig.keys[kPttKey] = true;
		Run(rig, 10, kTone);
		assert(rig.packets.empty());
		assert(!rig.tx.Status().inputs.transmitting);
	}

	void TestInputBlockKeepsOpenMicTierNormal()
	{
		for (const std::uint8_t key : { kWhisperKey, kShoutKey }) {
			Rig rig;
			rig.config.pushToTalk = false;
			rig.tx.Configure(rig.config);
			rig.tx.SetInputAllowed(false);
			rig.keys[key] = true;
			Run(rig, 12, kTone);
			assert(rig.packets.size() == 12);
			for (const VoiceHeader& header : rig.packets) {
				assert(header.tier == Tier::Normal);
			}
		}
	}

	void TestTiersFromKeys()
	{
		Rig whisper;
		whisper.keys[kPttKey] = true;
		whisper.keys[kWhisperKey] = true;
		Run(whisper, 3, kTone);
		assert(!whisper.packets.empty() && whisper.packets.back().tier == Tier::Whisper);

		Rig shout;
		shout.keys[kPttKey] = true;
		shout.keys[kShoutKey] = true;
		Run(shout, 3, kTone);
		assert(!shout.packets.empty() && shout.packets.back().tier == Tier::Shout);
	}

	void TestOpenMicGateAndPreRoll()
	{
		Rig rig;
		rig.config.pushToTalk = false;
		rig.tx.Configure(rig.config);
		Run(rig, 5, kSilence);
		const Frame quietA = SineFrame(50);
		const Frame quietB = SineFrame(100);
		const Frame quietC = SineFrame(150);
		const Frame loud = SineFrame(kTone);
		RunFrame(rig, quietA);
		RunFrame(rig, quietB);
		RunFrame(rig, quietC);
		assert(rig.packets.empty());
		RunFrame(rig, loud);
		Run(rig, 12, kSilence);
		assert(rig.packets.size() == 14);
		assert(rig.packets.front().sequence == 6);
		assert(rig.packets.front().burstStart);
		assert(BurstEnds(rig) == 1);
		assert(rig.packets.back().burstEnd);
		assert(rig.packets.back().sequence == 19);

		Encoder reference;
		const bool referenceOpened = reference.Open();
		const bool referenceReset = reference.Reset();
		assert(referenceOpened && referenceReset);
		const std::array<Frame, 4> expected{ quietA, quietB, quietC, loud };
		for (std::size_t i = 0; i < expected.size(); ++i) {
			assert(rig.payloads[i] == EncodeReference(reference, expected[i]));
		}
	}

	void TestSuppressionDoesNotPreRoll()
	{
		for (int mode = 0; mode < 3; ++mode) {
			Rig rig;
			rig.config.pushToTalk = false;
			rig.tx.Configure(rig.config);
			Suppress(rig, mode, true);
			Run(rig, 5, kSilence);
			Suppress(rig, mode, false);
			Run(rig, 1, kTone);
			assert(rig.packets.size() == 1);
			assert(rig.packets.front().sequence == 6);
			assert(rig.packets.front().burstStart);
		}
	}

	void TestSuppressionSilencesOpenMic()
	{
		for (int mode = 0; mode < 3; ++mode) {
			Rig rig;
			rig.config.pushToTalk = false;
			rig.tx.Configure(rig.config);
			Suppress(rig, mode, true);
			Run(rig, 5, kTone);
			assert(rig.packets.empty());
			assert(rig.tx.Status().sent == 0);
			assert(!rig.tx.Status().inputs.transmitting);
		}
	}

	void TestSuppressionAdvancesSequence()
	{
		for (int mode = 0; mode < 3; ++mode) {
			Rig rig;
			rig.keys[kPttKey] = true;
			Suppress(rig, mode, true);
			Run(rig, 3, kTone);
			assert(rig.packets.empty());
			Suppress(rig, mode, false);
			Run(rig, 1, kTone);
			assert(rig.packets.size() == 1);
			assert(rig.packets.front().sequence == 4);
			assert(rig.packets.front().burstStart);
		}
	}

	void TestResetSessionRestartsSequence()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		assert(rig.packets.size() == 3);
		rig.tx.ResetSession();
		Run(rig, 3, kTone);
		assert(rig.packets.size() == 5);
		assert(rig.packets[3].sequence == 2);
		assert(rig.packets[3].burstStart);
		assert(rig.packets[4].sequence == 3);
		assert(!rig.packets[4].burstStart);
	}

	void TestReopenDropsPartialFrame()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 2, kTone);
		const Frame pcm = SineFrame(kTone);
		rig.tx.OnSamples(pcm.data(), 480);
		rig.tx.OnDeviceError();
		rig.tx.OnReopenAttempt();
		Run(rig, 1, kSilence);
		assert(rig.tx.Status().levelDbfs < -80.0);
	}

	void TestStopDropsPartialFrame()
	{
		std::vector<std::int16_t> tone(1440, kTone);
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		rig.tx.OnSamples(tone.data(), 480);
		rig.tx.OnTick();
		rig.tx.Stop();
		const std::size_t before = rig.packets.size();
		rig.tx.OnSamples(tone.data(), 1440);
		rig.tx.OnTick();
		assert(rig.packets.size() - before == 1);
	}

	void TestResetSessionCancelsTail()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		rig.keys[kPttKey] = false;
		Run(rig, 2, kTone);
		const std::size_t tail = rig.packets.size();
		rig.tx.ResetSession();
		Run(rig, 4, kTone);
		assert(rig.packets.size() == tail);
	}

	void TestStopEndsBurstWithoutEnd()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		rig.tx.Stop();
		assert(BurstEnds(rig) == 0);
		const std::size_t before = rig.packets.size();
		Run(rig, 3, kTone);
		assert(rig.packets.size() > before);
		assert(rig.packets[before].burstStart);
	}

	void TestLevelFloorIsFinite()
	{
		Rig rig;
		assert(rig.tx.Status().levelDbfs == TransmitLevelFloorDbfs);
		Run(rig, 1, kSilence);
		assert(rig.tx.Status().levelDbfs == TransmitLevelFloorDbfs);
	}

	void TestDroppedFramesClearTransmitting()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		assert(rig.tx.Status().inputs.transmitting);
		Testing::MutableFaults().encodeReturn = -1;
		Run(rig, 1, kTone);
		assert(!rig.tx.Status().inputs.transmitting);
		assert(rig.tx.Status().dropped == 1);
		Testing::MutableFaults().encodeReturn = std::nullopt;
		rig.sendOk = false;
		Run(rig, 1, kTone);
		assert(!rig.tx.Status().inputs.transmitting);
		assert(rig.tx.Status().dropped == 2);
		rig.sendOk = true;
		Run(rig, 1, kTone);
		assert(rig.tx.Status().inputs.transmitting);
	}

	void TestOpenMicReopenAfterHangoverStartsBurst()
	{
		Rig rig;
		rig.config.pushToTalk = false;
		rig.tx.Configure(rig.config);
		Run(rig, 3, kTone);
		Run(rig, 10, kSilence);
		const std::size_t ended = rig.packets.size();
		assert(ended == 13);
		assert(rig.packets.back().burstEnd);
		Run(rig, 1, kTone);
		assert(rig.packets.size() == ended + 1);
		assert(rig.packets.back().sequence == 14);
		assert(rig.packets.back().burstStart);
	}

	void TestOpenMicOutageRestartsBurst()
	{
		Rig rig;
		rig.config.pushToTalk = false;
		rig.tx.Configure(rig.config);
		Run(rig, 5, kTone);
		const std::size_t before = rig.packets.size();
		assert(before == 5);
		rig.tx.OnDeviceError();
		rig.now += 0.5;
		rig.tx.OnReopenAttempt();
		Run(rig, 5, kTone);
		assert(rig.packets.size() == before + 5);
		assert(rig.packets[before].burstStart);
		assert(!rig.packets[before + 1].burstStart);
	}

	void TestOutageMidPressRestartsBurst()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		const std::size_t before = rig.packets.size();
		rig.tx.OnDeviceError();
		rig.now += 0.5;
		rig.tx.OnReopenAttempt();
		Run(rig, 3, kTone);
		assert(rig.packets.size() == before + 2);
		assert(rig.packets[before].burstStart);
		assert(!rig.packets[before + 1].burstStart);
	}

	void TestOutageReleaseSendsNothingAfterRecovery()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		const std::size_t before = rig.packets.size();
		assert(before > 0);
		rig.tx.OnDeviceError();
		rig.keys[kPttKey] = false;
		rig.now += 0.5;
		rig.tx.OnReopenAttempt();
		Run(rig, 20, kTone);
		assert(rig.packets.size() == before);
	}

	void TestOutageHoldRestartsWithBurstStart()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		const std::size_t before = rig.packets.size();
		const std::uint32_t last = rig.packets.back().sequence;
		rig.tx.OnDeviceError();
		rig.now += 0.5;
		rig.tx.OnReopenAttempt();
		Run(rig, 3, kTone);
		assert(rig.packets.size() > before);
		assert(rig.packets[before].burstStart);
		assert(rig.packets[before].sequence > last);
	}

	void TestLongHoldHasNoCutoff()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 9000, kTone);
		assert(rig.tx.Status().inputs.transmitting);
		assert(rig.packets.size() == 8998);
		assert(rig.packets.back().sequence == 9000);
		assert(BurstEnds(rig) == 0);
		rig.keys[kPttKey] = false;
		Run(rig, 10, kTone);
		assert(BurstEnds(rig) == 1);
		assert(rig.packets.back().burstEnd);
		for (std::size_t i = 1; i < rig.packets.size(); ++i) {
			assert(rig.packets[i].sequence == rig.packets[i - 1].sequence + 1);
		}
	}

	void TestPressTailAfterSilence()
	{
		Rig rig;
		Run(rig, 5, kSilence);
		rig.keys[kPttKey] = true;
		Run(rig, 3, kTone);
		assert(rig.packets.size() == 1);
		assert(rig.packets.front().sequence == 8);
		assert(rig.packets.front().burstStart);
		rig.keys[kPttKey] = false;
		Run(rig, 15, kTone);
		assert(rig.packets.size() == 10);
		assert(rig.packets.back().sequence == 17);
		assert(rig.packets.back().burstEnd);
		assert(BurstEnds(rig) == 1);
	}

	void TestBurstsEncodeFromFreshEncoder()
	{
		Rig rig;
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		rig.keys[kPttKey] = false;
		Run(rig, 12, kTone);
		const std::size_t burst = rig.payloads.size();
		assert(burst > 0);
		rig.keys[kPttKey] = true;
		Run(rig, 5, kTone);
		rig.keys[kPttKey] = false;
		Run(rig, 12, kTone);
		assert(rig.payloads.size() == 2 * burst);
		for (std::size_t i = 0; i < burst; ++i) {
			assert(rig.payloads[i] == rig.payloads[burst + i]);
		}
	}
}

int main()
{
	TestPttHoldAndTailBurstEnd();
	TestInputBlockCancelsHold();
	TestInputBlockIgnoresPress();
	TestInputBlockKeepsOpenMicTierNormal();
	TestTiersFromKeys();
	TestOpenMicGateAndPreRoll();
	TestSuppressionDoesNotPreRoll();
	TestSuppressionSilencesOpenMic();
	TestSuppressionAdvancesSequence();
	TestResetSessionRestartsSequence();
	TestReopenDropsPartialFrame();
	TestStopDropsPartialFrame();
	TestResetSessionCancelsTail();
	TestStopEndsBurstWithoutEnd();
	TestLevelFloorIsFinite();
	TestDroppedFramesClearTransmitting();
	TestOpenMicReopenAfterHangoverStartsBurst();
	TestOpenMicOutageRestartsBurst();
	TestOutageMidPressRestartsBurst();
	TestOutageReleaseSendsNothingAfterRecovery();
	TestOutageHoldRestartsWithBurstStart();
	TestBurstsEncodeFromFreshEncoder();
	TestLongHoldHasNoCutoff();
	TestPressTailAfterSilence();
	RunDeviceTests();
	Testing::MutableFaults() = Testing::Faults{};
	return 0;
}
