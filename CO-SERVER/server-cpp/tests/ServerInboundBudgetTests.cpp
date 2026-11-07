#include "F4TVoiceWire.h"
#include "ServerVoiceMeter.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
	using Bytes = std::vector<std::byte>;
	using CommonwealthOnline::Server::InboundGameplayLimits;
	using CommonwealthOnline::Server::ObserveInboundDatagram;
	using CommonwealthOnline::Server::kJoinAllowanceSeconds;
	using F4T::PacketRateLimit::Result;
	using F4T::PacketRateLimit::State;
	using F4T::PacketRateLimit::Verdict;

	constexpr double kInfinity = std::numeric_limits<double>::infinity();
	constexpr std::size_t kDefaultBytesPerWindow = 256ULL * 1024ULL;

	Bytes Gameplay(std::size_t a_bytes)
	{
		return Bytes(a_bytes, std::byte{ 'x' });
	}

	Result Send(State& a_global, State& a_voice, double a_now, const Bytes& a_datagram, double a_joinedAt)
	{
		return ObserveInboundDatagram(a_global, a_voice, a_now, a_datagram, InboundGameplayLimits(a_now, a_joinedAt));
	}

	void JoinAllowanceBoundaryIsSixtySeconds()
	{
		assert(kJoinAllowanceSeconds == 60.0);
		const auto datagram = Gameplay(90ULL * 1024ULL);

		for (const double now : { 0.0, 30.0, 59.999 }) {
			State global = F4T::PacketRateLimit::Initialize(now);
			State voice = F4T::PacketRateLimit::Initialize(now);
			for (int i = 0; i < 3; ++i) {
				assert(Send(global, voice, now, datagram, 0.0).verdict == Verdict::Allowed);
			}
		}

		State global = F4T::PacketRateLimit::Initialize(60.0);
		State voice = F4T::PacketRateLimit::Initialize(60.0);
		assert(Send(global, voice, 60.0, datagram, 0.0).verdict == Verdict::Allowed);
		assert(Send(global, voice, 60.0, datagram, 0.0).verdict == Verdict::Allowed);
		const auto third = Send(global, voice, 60.0, datagram, 0.0);
		assert(third.verdict == Verdict::Limited);
		assert(third.byteBudgetExceeded);
	}

	void JoinAllowanceIsFourTimesTheByteBudgetAndNoMore()
	{
		const auto limits = InboundGameplayLimits(1.0, 0.0);
		assert(limits.maximumBytesPerWindow == 4ULL * kDefaultBytesPerWindow);
		assert(limits.maximumPacketsPerWindow == F4T::PacketRateLimit::DefaultMaximumPacketsPerSecond);

		State global = F4T::PacketRateLimit::Initialize(1.0);
		State voice = F4T::PacketRateLimit::Initialize(1.0);
		const auto full = Gameplay(kDefaultBytesPerWindow);
		for (int i = 0; i < 4; ++i) {
			assert(Send(global, voice, 1.0, full, 0.0).verdict == Verdict::Allowed);
		}
		const auto over = Send(global, voice, 1.0, Gameplay(1), 0.0);
		assert(over.verdict == Verdict::Limited);
		assert(over.byteBudgetExceeded);
	}

	void JoinAllowanceDoesNotRaisePacketCount()
	{
		State global = F4T::PacketRateLimit::Initialize(0.0);
		State voice = F4T::PacketRateLimit::Initialize(0.0);
		const auto tiny = Gameplay(16);
		for (int i = 0; i < 120; ++i) {
			assert(Send(global, voice, 0.0, tiny, 0.0).verdict == Verdict::Allowed);
		}
		assert(Send(global, voice, 0.0, tiny, 0.0).verdict == Verdict::Limited);
	}

	void SustainedOverageDisconnectsOnTheThirdViolationWindow()
	{
		State global = F4T::PacketRateLimit::Initialize(0.0);
		State voice = F4T::PacketRateLimit::Initialize(0.0);
		const auto tiny = Gameplay(16);
		for (int window = 0; window < 3; ++window) {
			for (int i = 0; i < 130; ++i) {
				const double now = window + static_cast<double>(i) / 200.0;
				const auto result = Send(global, voice, now, tiny, kInfinity);
				if (window < 2) {
					assert(result.verdict != Verdict::Disconnect);
				} else if (i < 120) {
					assert(result.verdict == Verdict::Allowed);
				} else if (i == 120) {
					assert(result.verdict == Verdict::Disconnect);
					assert(result.newViolationWindow);
				}
			}
		}
	}

	void SingleOverBudgetBurstNeverDisconnects()
	{
		State global = F4T::PacketRateLimit::Initialize(0.0);
		State voice = F4T::PacketRateLimit::Initialize(0.0);
		const auto tiny = Gameplay(16);
		for (int i = 0; i < 500; ++i) {
			const auto result = Send(global, voice, static_cast<double>(i) / 1000.0, tiny, kInfinity);
			assert(result.verdict != Verdict::Disconnect);
		}
		assert(global.violationWindows == 1);

		for (int window = 1; window <= 3; ++window) {
			for (int i = 0; i < 10; ++i) {
				const auto result = Send(global, voice, window + static_cast<double>(i) / 100.0, tiny, kInfinity);
				assert(result.verdict == Verdict::Allowed);
			}
		}
		assert(global.violationWindows == 0);
	}

	void DefaultJoinedAtGivesNoAllowance()
	{
		const auto limits = InboundGameplayLimits(0.0, kInfinity);
		assert(limits.maximumBytesPerWindow == kDefaultBytesPerWindow);

		State global = F4T::PacketRateLimit::Initialize(0.0);
		State voice = F4T::PacketRateLimit::Initialize(0.0);
		const auto big = Gameplay(200ULL * 1024ULL);
		assert(ObserveInboundDatagram(global, voice, 0.0, big, F4T::PacketRateLimit::Config{}).verdict == Verdict::Allowed);
		const auto second = ObserveInboundDatagram(global, voice, 0.0, big, F4T::PacketRateLimit::Config{});
		assert(second.verdict == Verdict::Limited);
		assert(second.byteBudgetExceeded);
	}

	void ByteReasonTextIsPlainEnglish()
	{
		Result byteResult{};
		byteResult.byteBudgetExceeded = true;
		assert(CommonwealthOnline::Server::RateLimitReason(byteResult) ==
			"Sent more data than the server allows for several seconds.");
	}
}

int main()
{
	JoinAllowanceBoundaryIsSixtySeconds();
	JoinAllowanceIsFourTimesTheByteBudgetAndNoMore();
	JoinAllowanceDoesNotRaisePacketCount();
	SustainedOverageDisconnectsOnTheThirdViolationWindow();
	SingleOverBudgetBurstNeverDisconnects();
	DefaultJoinedAtGivesNoAllowance();
	ByteReasonTextIsPlainEnglish();
	return 0;
}
