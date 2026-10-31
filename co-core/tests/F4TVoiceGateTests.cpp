#include "F4TVoiceGate.h"
#include "F4TVoicePtt.h"
#include "F4TVoiceStatus.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
	using F4T::Voice::FrameLevelDbfs;
	using F4T::Voice::GateHangoverTicks;
	using F4T::Voice::GateTick;
	using F4T::Voice::MicHealth;
	using F4T::Voice::OpenMicGate;
	using F4T::Voice::PttInput;
	using F4T::Voice::PttState;
	using F4T::Voice::PttTick;
	using F4T::Voice::SelectVoiceStatus;
	using F4T::Voice::Tier;
	using F4T::Voice::VoiceFrameSamples;
	using F4T::Voice::VoiceStatus;
	using F4T::Voice::VoiceStatusInputs;
	using Frame = std::array<std::int16_t, VoiceFrameSamples>;

	constexpr double PollSeconds = 0.01;
	constexpr std::size_t PollsPerTick = 2;

	Frame Constant(std::int16_t a_value)
	{
		Frame frame{};
		frame.fill(a_value);
		return frame;
	}

	Frame Marked(std::int16_t a_marker)
	{
		Frame frame{};
		frame[0] = a_marker;
		return frame;
	}

	PttInput Key(bool a_keyDown, bool a_inputAllowed = true)
	{
		PttInput input{};
		input.keyDown = a_keyDown;
		input.inputAllowed = a_inputAllowed;
		return input;
	}

	void TestFrameLevel()
	{
		const auto loud = FrameLevelDbfs(Constant(1000));
		assert(std::abs(loud.rmsDbfs - 20.0 * std::log10(1000.0 / 32768.0)) < 1e-9);
		assert(std::abs(loud.peakDbfs - loud.rmsDbfs) < 1e-9);

		const auto silent = FrameLevelDbfs(Constant(0));
		assert(std::isinf(silent.rmsDbfs) && silent.rmsDbfs < 0.0);
		assert(std::isinf(silent.peakDbfs) && silent.peakDbfs < 0.0);

		Frame clipped{};
		clipped[5] = -32768;
		assert(std::abs(FrameLevelDbfs(clipped).peakDbfs) < 1e-9);
		assert(std::abs(FrameLevelDbfs(clipped).rmsDbfs + 10.0 * std::log10(960.0)) < 1e-9);
	}

	void TestPttTailEndsOnExactTick()
	{
		PttState ptt{};
		for (std::size_t poll = 0; poll <= 200; ++poll) {
			const double now = static_cast<double>(poll) * PollSeconds;
			ptt.Poll(Key(poll < 100), now);
			if (poll % PollsPerTick != 0) {
				continue;
			}
			const PttTick out = ptt.Tick(now);
			if (poll > 0) {
				const std::size_t frame = poll - PollsPerTick;
				assert(out.transmit == (frame <= 114) && out.burstStart == (frame == 0) && out.burstEnd == (frame == 114));
			}
		}
	}

	void TestPttTailUsesCurrentKeys()
	{
		PttState ptt{};
		PttInput whisper = Key(true);
		whisper.whisperDown = true;
		ptt.Poll(whisper, 0.0); assert(!ptt.Tick(0.0).transmit);
		ptt.Poll(Key(false), 0.5);
		const PttTick held = ptt.Tick(0.5);
		assert(held.transmit && held.tier == Tier::Whisper && held.burstStart);
		const PttTick tail = ptt.Tick(0.52);
		assert(tail.transmit && tail.tier == Tier::Normal && !tail.burstEnd);
		assert(ptt.Tick(0.66).burstEnd);
		assert(!ptt.Tick(0.68).transmit);
	}

	void TestPttTailBoundaryOnOddPoll()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); assert(!ptt.Tick(0.0).transmit);
		ptt.Poll(Key(false), 1.01);
		for (std::size_t centis = 102; centis <= 116; centis += 2) {
			const PttTick out = ptt.Tick(static_cast<double>(centis) / 100.0);
			assert(out.transmit && out.burstStart == (centis == 102) && out.burstEnd == (centis == 116));
		}
		assert(!ptt.Tick(1.18).transmit);
	}

	void TestPttTailEndOnTwentyMsGrid()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); ptt.Poll(Key(false), 0.5);
		for (std::size_t tick = 0; tick <= 9; ++tick) {
			const PttTick out = ptt.Tick(0.507 + static_cast<double>(tick) * 0.020);
			assert(out.transmit == (tick >= 1 && tick <= 8) && out.burstStart == (tick == 1) && out.burstEnd == (tick == 8));
		}
		ptt.Poll(Key(true), 0.69);
		assert(!ptt.Tick(0.707).transmit);
		assert(ptt.Tick(0.727).burstStart);
	}

	void TestPttTailStillTransmitsAt149Ms()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); ptt.Poll(Key(false), 0.0);
		assert(!ptt.Tick(0.0).transmit); assert(ptt.Tick(0.149).transmit);
		const PttTick last = ptt.Tick(0.169);
		assert(last.transmit && last.burstEnd);
		assert(!ptt.Tick(0.189).transmit);
	}

	void TestPttTailBoundaryIsToleranceInclusive()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); assert(!ptt.Tick(0.0).transmit);
		ptt.Poll(Key(false), 0.0);
		assert(ptt.Tick(0.0).transmit);
		assert(ptt.Tick(F4T::Voice::PttTailSeconds - 1e-9).burstEnd);

		for (std::int64_t pair = 0; pair < 200; ++pair) {
			const std::int64_t releaseMicros = 8388608000000LL + 10000 + pair * 20000;
			const double release = static_cast<double>(releaseMicros) / 1e6;
			PttState large{};
			large.Poll(Key(true), release - 0.01);
			assert(!large.Tick(release - 0.01).transmit);
			large.Poll(Key(false), release);
			assert(large.Tick(release).transmit);
			assert(large.Tick(static_cast<double>(releaseMicros + 150000) / 1e6).burstEnd);
		}
	}

	void TestPttPressDuringTailContinuesStream()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); assert(!ptt.Tick(0.0).transmit);
		ptt.Poll(Key(false), 0.5);
		assert(ptt.Tick(0.5).burstStart);
		ptt.Poll(Key(true), 0.51);
		const PttTick during = ptt.Tick(0.52);
		assert(during.transmit && !during.burstStart && !during.burstEnd);
		const PttTick held = ptt.Tick(0.54);
		assert(held.transmit && !held.burstStart && !held.burstEnd);
		ptt.Poll(Key(false), 0.6);
		assert(ptt.Tick(0.6).transmit);
		const PttTick before = ptt.Tick(0.74);
		assert(before.transmit && !before.burstEnd);
		const PttTick edge = ptt.Tick(0.75);
		assert(edge.transmit && edge.burstEnd);
		assert(!ptt.Tick(0.76).transmit);
	}

	void TestPttTierKeys()
	{
		PttState ptt{};
		PttInput input = Key(true);
		ptt.Poll(input, 0.0); assert(!ptt.Tick(0.0).transmit);
		input.whisperDown = true;
		ptt.Poll(input, 0.01);
		assert(ptt.Tick(0.02).tier == Tier::Normal);
		input.shoutDown = true;
		ptt.Poll(input, 0.03);
		assert(ptt.Tick(0.04).tier == Tier::Whisper);
		input.whisperDown = false; input.shoutDown = false;
		ptt.Poll(input, 0.05);
		assert(ptt.Tick(0.06).tier == Tier::Shout);
		assert(ptt.Tick(0.08).tier == Tier::Normal);
	}

	void TestPttInputBlockedCancelsImmediately()
	{
		PttState ptt{};
		ptt.Poll(Key(true), 0.0); assert(!ptt.Tick(0.0).transmit);
		ptt.Poll(Key(true, false), 0.5);
		const PttTick held = ptt.Tick(0.5);
		assert(held.transmit && held.burstEnd);
		assert(!ptt.Tick(0.52).transmit);
		ptt.Poll(Key(true), 1.0); assert(!ptt.Tick(1.0).transmit);
		ptt.Poll(Key(false), 1.5);
		const PttTick tail = ptt.Tick(1.5);
		assert(tail.transmit && !tail.burstEnd);
		ptt.Poll(Key(false, false), 1.52);
		const PttTick cut = ptt.Tick(1.52);
		assert(cut.transmit && cut.burstEnd);
		assert(!ptt.Tick(1.54).transmit);
	}

	void TestPttHoldNeverCutsOff()
	{
		PttState ptt{};
		for (std::size_t poll = 0; poll <= 18000; ++poll) {
			const double now = static_cast<double>(poll) * PollSeconds;
			ptt.Poll(Key(true), now);
			if (poll % PollsPerTick != 0) {
				continue;
			}
			const PttTick tick = ptt.Tick(now);
			if (poll > 0) {
				assert(tick.transmit && tick.burstStart == (poll == PollsPerTick) && !tick.burstEnd);
			}
		}
	}

	void TestPttJitteredTicksEndBurstOnce()
	{
		constexpr std::array<std::array<double, 9>, 2> Sequences{ {
			{ 0.52, 0.54, 0.56, 0.58, 0.60, 0.62, 0.634, 0.648, 0.66 },
			{ 0.52, 0.54, 0.56, 0.58, 0.60, 0.62, 0.628, 0.652, 0.67 } } };
		for (const auto& ticks : Sequences) {
			PttState ptt{};
			ptt.Poll(Key(true), 0.0); assert(!ptt.Tick(0.0).transmit);
			ptt.Poll(Key(false), 0.5);
			std::size_t starts = 0;
			std::size_t ends = 0;
			for (const double now : ticks) {
				const PttTick out = ptt.Tick(now);
				assert((!out.burstStart && !out.burstEnd) || out.transmit);
				starts += out.burstStart ? 1 : 0;
				ends += out.burstEnd ? 1 : 0;
			}
			assert(starts == 1 && ends == 1);
		}
	}

	void TestOpenMicThreshold()
	{
		OpenMicGate closedGate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		const GateTick belowLoud = closedGate.Tick(Constant(184));
		assert(!belowLoud.send);
		const GateTick silent = closedGate.Tick(Constant(0));
		assert(!silent.send);

		OpenMicGate openGate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		const GateTick aboveThreshold = openGate.Tick(Constant(185));
		assert(aboveThreshold.send);

		const double exactThreshold = 20.0 * std::log10(1000.0 / 32768.0);
		OpenMicGate justBelow{ exactThreshold };
		assert(!justBelow.Tick(Constant(999)).send);
		OpenMicGate atThreshold{ exactThreshold };
		assert(atThreshold.Tick(Constant(1000)).send);

		Frame peakOnly{};
		peakOnly[0] = 2000;
		OpenMicGate peakGate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		assert(!peakGate.Tick(peakOnly).send);
	}

	void TestOpenMicHangoverIsTenTicks()
	{
		OpenMicGate gate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		const GateTick open = gate.Tick(Constant(1000));
		assert(open.send && open.burstStart && !open.burstEnd && open.preRoll == 0);

		for (std::size_t tick = 1; tick <= 10; ++tick) {
			const GateTick hang = gate.Tick(Constant(0));
			assert(hang.send);
			assert(!hang.burstStart);
			assert(hang.burstEnd == (tick == 10));
		}
		const GateTick reopen = gate.Tick(Constant(1000));
		assert(reopen.send && reopen.burstStart && reopen.preRoll == 0);
	}

	void TestOpenMicPreRollReturnsPreviousFramesInOrder()
	{
		OpenMicGate gate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		for (std::int16_t marker = 1; marker <= 4; ++marker) {
			const GateTick quiet = gate.Tick(Marked(marker));
			assert(!quiet.send);
		}
		const GateTick open = gate.Tick(Constant(1000));
		assert(open.send && open.burstStart);
		assert(open.preRoll == 3);
		assert(gate.Frame(3)[0] == 2);
		assert(gate.Frame(2)[0] == 3);
		assert(gate.Frame(1)[0] == 4);
		assert(gate.Frame(0)[0] == 1000);
	}

	void TestOpenMicPreRollSkipsFramesAlreadySent()
	{
		OpenMicGate gate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		const GateTick first = gate.Tick(Constant(1000));
		assert(first.send);
		for (std::size_t tick = 1; tick <= GateHangoverTicks; ++tick) {
			const GateTick hang = gate.Tick(Constant(0));
			assert(hang.send);
		}
		const GateTick unsentA = gate.Tick(Marked(11));
		assert(!unsentA.send);
		const GateTick unsentB = gate.Tick(Marked(12));
		assert(!unsentB.send);
		const GateTick reopen = gate.Tick(Constant(1000));
		assert(reopen.send && reopen.burstStart);
		assert(reopen.preRoll == 2);
		assert(gate.Frame(2)[0] == 11);
		assert(gate.Frame(1)[0] == 12);
	}

	void TestMicFailsAfterHalfSecondWithoutCallback()
	{
		MicHealth health{ 0.0 };
		health.OnCallback(0.0, -30.0, false);
		assert(!health.Failed(0.499));
		assert(health.Failed(0.5));

		health.OnCallback(0.5, -30.0, false);
		assert(!health.Failed(0.6));

		MicHealth offsetStart{ 0.2 };
		assert(offsetStart.Failed(0.7));
	}

	void TestMicDeviceErrorFailsUntilCallback()
	{
		MicHealth health{ 0.0 };
		health.OnDeviceError();
		assert(health.Failed(0.0));
		health.OnCallback(0.0, -30.0, false);
		assert(!health.Failed(0.0));

		MicHealth quiet{ 0.0 };
		quiet.OnCallback(0.0, -85.0, true);
		quiet.OnCallback(0.25, -85.0, true);
		quiet.OnDeviceError();
		quiet.OnCallback(0.5625, -85.0, true);
		assert(!quiet.SilentHint(3.0));
		assert(quiet.SilentHint(3.5625));
	}

	void TestQuietWorkingMicIsIdle()
	{
		OpenMicGate gate{ F4T::Voice::DefaultOpenMicThresholdDbfs };
		MicHealth health{ 0.0 };
		const Frame quiet = Constant(10);
		const double quietPeak = FrameLevelDbfs(quiet).peakDbfs;
		GateTick tick{};
		for (std::size_t step = 0; step <= 300; ++step) {
			const double now = static_cast<double>(step) * F4T::Voice::VoiceTickSeconds;
			tick = gate.Tick(quiet);
			health.OnCallback(now, quietPeak, true);
		}
		const double end = 300.0 * F4T::Voice::VoiceTickSeconds;
		assert(!tick.send);
		assert(!health.Failed(end));
		assert(!health.SilentHint(end));
		assert(health.Idle(end, tick.send));

		VoiceStatusInputs inputs{};
		inputs.idle = health.Idle(end, tick.send);
		assert(SelectVoiceStatus(inputs) == VoiceStatus::idle);

		const GateTick loud = gate.Tick(Constant(1000));
		assert(loud.send);
		assert(!health.Idle(end, loud.send));
	}

	void TestMicRecoveringWhileReopening()
	{
		MicHealth health{ 0.0 };
		health.OnDeviceError();
		assert(health.Failed(0.0));
		assert(!health.Recovering());

		VoiceStatusInputs inputs{};
		inputs.micFailed = health.Failed(0.0);
		inputs.micRecovering = health.Recovering();
		assert(SelectVoiceStatus(inputs) == VoiceStatus::micFailed);

		health.OnReopenAttempt();
		assert(health.Recovering());
		assert(!health.Idle(0.0, false));
		inputs.micRecovering = health.Recovering();
		assert(SelectVoiceStatus(inputs) == VoiceStatus::micRecovering);

		health.OnCallback(0.02, -30.0, false);
		assert(!health.Recovering());
		assert(!health.Failed(0.02));
	}

	void TestSilentPeakWithPttHeldHintsAfterThreeSeconds()
	{
		const double quietPeak = FrameLevelDbfs(Constant(2)).peakDbfs;
		assert(quietPeak < F4T::Voice::MicSilentPeakDbfs);

		MicHealth health{ 0.0 };
		for (std::size_t step = 0; step <= 96; ++step) {
			const double now = static_cast<double>(step) / 32.0;
			health.OnCallback(now, quietPeak, true);
			if (step == 93) {
				assert(!health.SilentHint(now));
			}
		}
		assert(health.SilentHint(3.0));

		MicHealth notHeld{ 0.0 };
		for (std::size_t step = 0; step <= 96; ++step) {
			notHeld.OnCallback(static_cast<double>(step) / 32.0, quietPeak, false);
		}
		assert(!notHeld.SilentHint(3.0));
	}

	void TestSilentHintThresholdIsEightyDbfs()
	{
		MicHealth justAbove{ 0.0 }, justBelow{ 0.0 }, atBoundary{ 0.0 };
		for (std::size_t step = 0; step <= 96; ++step) {
			justAbove.OnCallback(static_cast<double>(step) / 32.0, -79.9, true);
			justBelow.OnCallback(static_cast<double>(step) / 32.0, -80.1, true);
			atBoundary.OnCallback(static_cast<double>(step) / 32.0, -80.0, true);
		}
		assert(!justAbove.SilentHint(3.0) && !atBoundary.SilentHint(3.0));
		assert(justBelow.SilentHint(3.0));

		MicHealth offsetStart{ 1.004 };
		offsetStart.OnCallback(1.004, -85.0, true);
		assert(offsetStart.SilentHint(4.004));
	}

	void TestMicQuietIntervalRestartsAfterGapAndLoudCallback()
	{
		constexpr double QuietPeak = -85.0;
		MicHealth gap{ 0.0 };
		for (std::size_t step = 0; step <= 32; ++step) {
			gap.OnCallback(static_cast<double>(step) / 32.0, QuietPeak, true);
		}
		for (std::size_t step = 0; step <= 96; ++step) {
			const double now = 2.0 + static_cast<double>(step) / 32.0;
			gap.OnCallback(now, QuietPeak, true);
			assert(gap.SilentHint(now) == (step == 96));
		}

		MicHealth loud{ 0.0 };
		for (std::size_t step = 0; step <= 64; ++step) {
			loud.OnCallback(static_cast<double>(step) / 32.0, QuietPeak, true);
		}
		loud.OnCallback(2.03125, -30.0, true);
		for (std::size_t step = 66; step <= 162; ++step) {
			const double now = static_cast<double>(step) / 32.0;
			loud.OnCallback(now, QuietPeak, true);
			assert(loud.SilentHint(now) == (step == 162));
		}

		MicHealth health{ 0.2 };
		health.OnCallback(0.2, -90.0, true);
		health.OnCallback(0.7, -90.0, true);
		assert(!health.SilentHint(3.2) && health.SilentHint(3.7));
	}
}

int main()
{
	TestFrameLevel();
	TestPttTailEndsOnExactTick();
	TestPttTierKeys();
	TestPttTailUsesCurrentKeys();
	TestPttTailBoundaryOnOddPoll();
	TestPttTailEndOnTwentyMsGrid();
	TestPttTailStillTransmitsAt149Ms();
	TestPttTailBoundaryIsToleranceInclusive();
	TestPttPressDuringTailContinuesStream();
	TestPttInputBlockedCancelsImmediately();
	TestPttHoldNeverCutsOff();
	TestPttJitteredTicksEndBurstOnce();
	TestOpenMicThreshold();
	TestOpenMicHangoverIsTenTicks();
	TestOpenMicPreRollReturnsPreviousFramesInOrder();
	TestOpenMicPreRollSkipsFramesAlreadySent();
	TestMicFailsAfterHalfSecondWithoutCallback();
	TestMicDeviceErrorFailsUntilCallback();
	TestMicRecoveringWhileReopening();
	TestQuietWorkingMicIsIdle();
	TestSilentPeakWithPttHeldHintsAfterThreeSeconds();
	TestSilentHintThresholdIsEightyDbfs();
	TestMicQuietIntervalRestartsAfterGapAndLoudCallback();
	return 0;
}
