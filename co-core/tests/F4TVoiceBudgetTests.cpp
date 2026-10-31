#include "F4TVoiceBudget.h"

#include <cassert>
#include <limits>

int main()
{
	using namespace F4T;
	using namespace F4T::Voice;
	using PacketRateLimit::Verdict;

	{
		auto state = PacketRateLimit::Initialize(0.0);
		for (int packet = 0; packet < 500; ++packet) {
			assert(ObserveVoiceHard(state, packet / 50.0, 172).verdict == Verdict::Allowed);
		}
		assert(state.violationWindows == 0);
	}

	{
		auto state = PacketRateLimit::Initialize(0.0);
		for (int attempt = 0; attempt < 600; ++attempt) {
			const int window = attempt / 200;
			const int inWindow = attempt % 200;
			const auto result = ObserveVoiceHard(state, attempt / 200.0, 172);
			const auto expected = inWindow < 150 ? Verdict::Allowed : (window < 2 ? Verdict::Limited : Verdict::Disconnect);
			assert(result.verdict == expected);
			assert(result.newViolationWindow == (inWindow == 150));
		}
		assert(state.violationWindows == 3);
	}

	{
		auto state = PacketRateLimit::Initialize(0.0);
		for (int window = 0; window < 3; ++window) {
			for (int attempt = 0; attempt < 100; ++attempt) {
				const auto result = ObserveVoiceHard(state, window + attempt / 100.0, 400);
				const bool overBytes = attempt >= 81;
				assert(result.byteBudgetExceeded == overBytes);
				assert(result.verdict == (!overBytes ? Verdict::Allowed : (window < 2 ? Verdict::Limited : Verdict::Disconnect)));
			}
		}
	}

	{
		auto bucket = MakeVoiceSoftBucket(0.0);
		assert(bucket.tokens == 10.0);
		for (int token = 0; token < 10; ++token) assert(TakeVoiceSoftToken(bucket, 0.0));
		assert(!TakeVoiceSoftToken(bucket, 0.0));
	}

	{
		auto bucket = MakeVoiceSoftBucket(0.0);
		auto monitor = MakeSoftDropMonitor(0.0);
		auto hard = PacketRateLimit::Initialize(0.0);
		int dropped = 0;
		for (int attempt = 0; attempt < 600; ++attempt) {
			const double now = attempt / 60.0;
			assert(ObserveVoiceHard(hard, now, 172).verdict == Verdict::Allowed);
			const bool accepted = TakeVoiceSoftToken(bucket, now);
			dropped += accepted ? 0 : 1;
			const bool autoMuted = Record(monitor, now, !accepted);
			assert(!autoMuted);
		}
		assert(dropped == 41);
	}

	{
		auto bucket = MakeVoiceSoftBucket(0.0);
		auto monitor = MakeSoftDropMonitor(0.0);
		auto hard = PacketRateLimit::Initialize(0.0);
		double mutedAt = -1.0;
		for (int attempt = 0; attempt < 800; ++attempt) {
			const double now = attempt / 80.0;
			assert(ObserveVoiceHard(hard, now, 172).verdict == Verdict::Allowed);
			if (IsMuted(monitor, now)) {
				continue;
			}
			const bool accepted = TakeVoiceSoftToken(bucket, now);
			const bool autoMuted = Record(monitor, now, !accepted);
			if (autoMuted) {
				mutedAt = now;
			}
		}
		assert(mutedAt == 6.0);
		assert(monitor.mutedUntil == 36.0);
		assert(IsMuted(monitor, 35.999));
		assert(!IsMuted(monitor, 36.0));
		assert(IsMuted(monitor, std::numeric_limits<double>::quiet_NaN()));
	}

	{
		auto bucket = MakeVoiceSoftBucket(0.0);
		auto monitor = MakeSoftDropMonitor(0.0);
		double mutedAt = -1.0;
		for (int attempt = 0; attempt < 800; ++attempt) {
			const double now = attempt / 80.0 + 0.001 * (attempt % 7);
			if (IsMuted(monitor, now)) {
				continue;
			}
			const bool accepted = TakeVoiceSoftToken(bucket, now);
			const bool autoMuted = Record(monitor, now, !accepted);
			if (autoMuted) {
				mutedAt = now;
			}
		}
		assert(mutedAt > 6.003 && mutedAt < 6.005);
		assert(monitor.mutedUntil == mutedAt + 30.0);
	}

	const auto feed = [](SoftDropMonitor& a_monitor, double a_second, int a_accepted, int a_dropped) {
		bool muted = false;
		for (int attempt = 0; attempt < a_accepted; ++attempt) {
			muted = Record(a_monitor, a_second, false) || muted;
		}
		for (int attempt = 0; attempt < a_dropped; ++attempt) {
			muted = Record(a_monitor, a_second, true) || muted;
		}
		return muted;
	};

	{
		auto monitor = MakeSoftDropMonitor(0.5);
		for (int second = 1; second <= 5; ++second) assert(!feed(monitor, second, 1, 3));
		assert(feed(monitor, 6.25, 1, 0));
		assert(monitor.mutedUntil == 36.25);
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		for (int second = 0; second < 3; ++second) assert(!feed(monitor, second, 1, 3));
		assert(!feed(monitor, 3, 0, 1));
		assert(!feed(monitor, 5, 0, 1));
		assert(!feed(monitor, 6, 0, 1));
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		for (int second = 0; second < 4; ++second) assert(!feed(monitor, second, 1, 3));
		assert(!feed(monitor, 4, 3, 1));
		for (int second = 5; second < 10; ++second) assert(!feed(monitor, second, 1, 3));
		assert(feed(monitor, 10, 1, 0));
		assert(monitor.mutedUntil == 40.0);
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		for (int second = 0; second < 4; ++second) assert(!feed(monitor, second, 1, 3));
		assert(!feed(monitor, 6, 1, 3));
		for (int second = 7; second < 11; ++second) assert(!feed(monitor, second, 1, 3));
		assert(feed(monitor, 11, 1, 0));
		assert(IsMuted(monitor, 40.5));
		assert(!IsMuted(monitor, 41.0));
		for (int second = 41; second < 46; ++second) assert(!feed(monitor, second, 0, 4));
		assert(feed(monitor, 46, 1, 0));
		assert(monitor.mutedUntil == 76.0);
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		for (int second = 0; second < 5; ++second) assert(!feed(monitor, second, 74, 26));
		assert(feed(monitor, 5, 1, 0));
		assert(monitor.mutedUntil == 35.0);
	}

	{
		auto bucket = MakeVoiceControlBucket(0.0);
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(!TakeVoiceControlToken(bucket, 0.0));
		assert(!TakeVoiceControlToken(bucket, 0.5));
		assert(TakeVoiceControlToken(bucket, 1.0));
		assert(!TakeVoiceControlToken(bucket, 1.0));
		assert(TakeVoiceControlToken(bucket, 2.0));
		assert(!TakeVoiceControlToken(bucket, 2.0));
	}

	{
		auto bucket = MakeVoiceEgressBucket(0.0);
		assert(bucket.bytes == 16384.0);
		assert(TakeVoiceEgressBytes(bucket, 0.0, 16384));
		assert(!TakeVoiceEgressBytes(bucket, 0.0, 1));
		for (int step = 1; step <= 1024; ++step) {
			assert(TakeVoiceEgressBytes(bucket, step / 1024.0, 187));
		}
		assert(TakeVoiceEgressBytes(bucket, 1.0, 512));
		assert(!TakeVoiceEgressBytes(bucket, 1.0, 1));
		assert(TakeVoiceEgressBytes(bucket, 2.0, 16384));
		assert(!TakeVoiceEgressBytes(bucket, 2.0, 1));
	}

	{
		auto bucket = MakeVoiceEgressBucket(0.0);
		assert(TakeVoiceEgressBytes(bucket, 0.0, 16000));
		assert(!TakeVoiceEgressBytes(bucket, 0.0, 1000));
		assert(TakeVoiceEgressBytes(bucket, 0.0, 384));
	}

	{
		auto bucket = MakeVoiceSoftBucket(0.0);
		for (int token = 0; token < 10; ++token) assert(TakeVoiceSoftToken(bucket, 0.0));
		for (int token = 0; token < 10; ++token) assert(TakeVoiceSoftToken(bucket, 10.0));
		assert(!TakeVoiceSoftToken(bucket, 10.0));
	}

	{
		auto bucket = MakeVoiceControlBucket(0.0);
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(TakeVoiceControlToken(bucket, 10.0));
		assert(TakeVoiceControlToken(bucket, 10.0));
		assert(!TakeVoiceControlToken(bucket, 10.0));
	}

	{
		auto bucket = MakeVoiceEgressBucket(0.0);
		assert(TakeVoiceEgressBytes(bucket, std::numeric_limits<double>::infinity(), 16384));
		assert(TakeVoiceEgressBytes(bucket, 1.0, 16384));
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		assert(!Record(monitor, std::numeric_limits<double>::infinity(), false));
		bool muted = false;
		for (int second = 0; second < 10; ++second) muted = feed(monitor, second, 1, 3) || muted;
		assert(muted);
		assert(monitor.mutedUntil == 35.0);
	}

	{
		const double starts[] = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
		for (const double start : starts) {
			auto monitor = MakeSoftDropMonitor(start);
			bool muted = false;
			for (int second = 0; second < 10; ++second) muted = feed(monitor, second, 1, 3) || muted;
			assert(muted);
			assert(monitor.mutedUntil == 35.0);
		}
	}

	{
		auto egress = MakeVoiceEgressBucket(std::numeric_limits<double>::quiet_NaN());
		assert(TakeVoiceEgressBytes(egress, 1.0, 16384));
		assert(TakeVoiceEgressBytes(egress, 2.0, 1));
		auto soft = MakeVoiceSoftBucket(std::numeric_limits<double>::quiet_NaN());
		for (int token = 0; token < 10; ++token) assert(TakeVoiceSoftToken(soft, 1.0));
		assert(!TakeVoiceSoftToken(soft, 1.0));
		assert(TakeVoiceSoftToken(soft, 2.0));
		auto control = MakeVoiceControlBucket(std::numeric_limits<double>::quiet_NaN());
		assert(TakeVoiceControlToken(control, 0.0));
		assert(TakeVoiceControlToken(control, 0.0));
		assert(!TakeVoiceControlToken(control, 0.0));
		assert(TakeVoiceControlToken(control, 1.0));
	}

	{
		assert(!IsMuted(MakeSoftDropMonitor(-5.0), -6.0));
		assert(!IsMuted(MakeSoftDropMonitor(-5.0), -4.0));
		assert(!IsMuted(MakeSoftDropMonitor(0.0), std::numeric_limits<double>::quiet_NaN()));
	}

	{
		auto state = PacketRateLimit::Initialize(0.0);
		assert(ObserveVoiceHard(state, 0.0, 32768).verdict == Verdict::Allowed);
		assert(ObserveVoiceHard(state, 0.1, 1).byteBudgetExceeded);
	}

	{
		auto bucket = MakeVoiceControlBucket(0.0);
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(TakeVoiceControlToken(bucket, 0.0));
		assert(!TakeVoiceControlToken(bucket, 0.95));
	}

	{
		auto monitor = MakeSoftDropMonitor(0.0);
		double mutedAt = -1.0;
		for (int second = 0; second < 7; ++second) {
			if (Record(monitor, second, false)) {
				mutedAt = second;
			}
			if (Record(monitor, second + 0.25, false)) {
				mutedAt = second + 0.25;
			}
			if (Record(monitor, second + 0.5, true)) {
				mutedAt = second + 0.5;
			}
		}
		assert(mutedAt == 5.0);
		assert(monitor.mutedUntil == 35.0);
	}

	return 0;
}
