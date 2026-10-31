#include "F4TVoiceWire.h"
#include "ServerVoiceMeter.h"

#include <cassert>
#include <cstddef>
#include <string_view>
#include <vector>

namespace
{
	using Bytes = std::vector<std::byte>;
	using CommonwealthOnline::Server::ObserveInboundDatagram;
	using F4T::PacketRateLimit::Result;
	using F4T::PacketRateLimit::State;
	using F4T::PacketRateLimit::Verdict;

	struct Meters
	{
		State global;
		State voice;
	};

	Meters NewMeters()
	{
		return Meters{ F4T::PacketRateLimit::Initialize(0.0), F4T::PacketRateLimit::Initialize(0.0) };
	}

	Result Observe(Meters& a_meters, double a_nowSeconds, const Bytes& a_datagram)
	{
		return ObserveInboundDatagram(a_meters.global, a_meters.voice, a_nowSeconds, a_datagram);
	}

	bool SameState(const State& a_left, const State& a_right)
	{
		return a_left.windowStartSeconds == a_right.windowStartSeconds &&
			a_left.windowCount == a_right.windowCount &&
			a_left.windowBytes == a_right.windowBytes &&
			a_left.violationWindows == a_right.violationWindows &&
			a_left.windowBlocked == a_right.windowBlocked;
	}

	Bytes Text(std::string_view a_text)
	{
		Bytes bytes;
		for (const char character : a_text) {
			bytes.push_back(static_cast<std::byte>(character));
		}
		return bytes;
	}

	Bytes VoiceDatagram(std::size_t a_payloadBytes, std::uint32_t a_sequence = 1)
	{
		const Bytes payload(a_payloadBytes, std::byte{ 0x5A });
		Bytes datagram(F4T::Voice::kVoiceMaxDatagramBytes);
		F4T::Voice::VoiceHeader header{};
		header.sequence = a_sequence;
		const auto size = F4T::Voice::EncodeVoicePacket(header, payload, datagram);
		assert(size == F4T::Voice::kVoiceHeaderBytes + a_payloadBytes);
		datagram.resize(size);
		return datagram;
	}

	Bytes VoiceGarbage(std::size_t a_size)
	{
		Bytes datagram = Text("COV1");
		datagram.resize(a_size, std::byte{ 0xFF });
		return datagram;
	}

	Bytes GameplayDatagram()
	{
		return Text(R"({"type":"transform","x":1.0})");
	}

	int FirstDisconnectIndex(Meters& a_meters, const Bytes& a_datagram, int a_perSecond, int a_count)
	{
		for (int index = 0; index < a_count; ++index) {
			const auto result = Observe(a_meters, static_cast<double>(index) / a_perSecond, a_datagram);
			if (result.verdict == Verdict::Disconnect) {
				return index;
			}
		}
		return -1;
	}

	void SteadyVoiceAndGameplayAreAllowedWithoutDoubleCounting()
	{
		Meters meters = NewMeters();
		const auto voice = VoiceDatagram(60);
		const auto gameplay = GameplayDatagram();
		for (int ms = 0; ms < 10000; ++ms) {
			const double now = ms / 1000.0;
			if (ms % 20 == 0) {
				const auto globalBefore = meters.global;
				const auto result = Observe(meters, now, voice);
				assert(result.verdict == Verdict::Allowed);
				assert(SameState(globalBefore, meters.global));
			}
			if (ms % 10 == 0) {
				const auto voiceBefore = meters.voice;
				const auto result = Observe(meters, now, gameplay);
				assert(result.verdict == Verdict::Allowed);
				assert(SameState(voiceBefore, meters.voice));
			}
		}
		assert(meters.global.violationWindows == 0);
		assert(meters.voice.violationWindows == 0);
	}

	void VoiceFloodDisconnectsInTheThirdWindowForEveryVariant()
	{
		const std::vector<Bytes> variants{ VoiceDatagram(60), VoiceDatagram(60, 2), VoiceGarbage(40), VoiceDatagram(160) };
		for (const auto& datagram : variants) {
			Meters meters = NewMeters();
			const auto globalBefore = meters.global;
			const auto index = FirstDisconnectIndex(meters, datagram, 200, 1000);
			assert(index == 550);
			assert(SameState(globalBefore, meters.global));
		}
	}

	void FourHundredByteVoiceBurstHitsTheByteBudget()
	{
		Meters meters = NewMeters();
		const auto datagram = VoiceGarbage(400);
		int firstLimited = -1;
		Verdict verdict = Verdict::Allowed;
		bool byteBudget = false;
		for (int index = 0; index < 100; ++index) {
			const auto result = Observe(meters, index / 100.0, datagram);
			if (result.verdict != Verdict::Allowed && firstLimited < 0) {
				firstLimited = index;
				verdict = result.verdict;
				byteBudget = result.byteBudgetExceeded;
			}
		}
		assert(firstLimited == 81);
		assert(verdict == Verdict::Limited);
		assert(byteBudget);
		assert(meters.voice.violationWindows == 1);
		assert(meters.global.windowCount == 0);
	}

	void ExactlyFullByteBudgetIsAllowedAndTheNextDatagramIsLimited()
	{
		Meters meters = NewMeters();
		const auto datagram = VoiceGarbage(4096);
		for (int index = 0; index < 8; ++index) {
			const auto result = Observe(meters, 0.0, datagram);
			assert(result.verdict == Verdict::Allowed);
		}
		assert(meters.voice.windowBytes == 32768);
		const auto result = Observe(meters, 0.0, datagram);
		assert(result.verdict == Verdict::Limited);
		assert(result.byteBudgetExceeded);
		assert(meters.global.windowCount == 0);
	}

	void GameplayFloodTripsOnlyTheGlobalMeter()
	{
		Meters meters = NewMeters();
		const auto voiceBefore = meters.voice;
		const auto index = FirstDisconnectIndex(meters, GameplayDatagram(), 200, 1000);
		assert(index == 520);
		assert(SameState(voiceBefore, meters.voice));
	}

	void VoiceBelowItsLimitStaysOpenWhileGameplayAtTheSameRateIsDisconnected()
	{
		Meters voiceMeters = NewMeters();
		const auto voiceIndex = FirstDisconnectIndex(voiceMeters, VoiceDatagram(60), 130, 1300);
		assert(voiceIndex == -1);
		assert(voiceMeters.voice.violationWindows == 0);

		Meters gameplayMeters = NewMeters();
		const auto gameplayIndex = FirstDisconnectIndex(gameplayMeters, GameplayDatagram(), 130, 1300);
		assert(gameplayIndex == 380);
	}

	void OnlyACovPrefixOfFourBytesReachesTheVoiceMeter()
	{
		const std::vector<Bytes> gameplayOnly{ Bytes{}, Text("C"), Text("COV"), Text("COV2 payload"), Text("COG2 payload") };
		for (const auto& datagram : gameplayOnly) {
			Meters meters = NewMeters();
			(void)Observe(meters, 0.0, datagram);
			assert(meters.global.windowCount == 1);
			assert(meters.voice.windowCount == 0);
		}

		Meters meters = NewMeters();
		(void)Observe(meters, 0.0, Text("COV1"));
		assert(meters.voice.windowCount == 1);
		assert(meters.voice.windowBytes == 4);
		assert(meters.global.windowCount == 0);

		Meters full = NewMeters();
		(void)Observe(full, 0.0, VoiceDatagram(160));
		assert(full.voice.windowBytes == F4T::Voice::kVoiceMaxDatagramBytes);
		assert(full.global.windowCount == 0);
	}
}

int main()
{
	SteadyVoiceAndGameplayAreAllowedWithoutDoubleCounting();
	VoiceFloodDisconnectsInTheThirdWindowForEveryVariant();
	FourHundredByteVoiceBurstHitsTheByteBudget();
	ExactlyFullByteBudgetIsAllowedAndTheNextDatagramIsLimited();
	GameplayFloodTripsOnlyTheGlobalMeter();
	VoiceBelowItsLimitStaysOpenWhileGameplayAtTheSameRateIsDisconnected();
	OnlyACovPrefixOfFourBytesReachesTheVoiceMeter();
	return 0;
}
