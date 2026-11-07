#include "F4TVoiceWire.h"
#include "ServerVoiceMeter.h"

#include <cassert>
#include <cstddef>
#include <map>
#include <string>
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
		return ObserveInboundDatagram(a_meters.global, a_meters.voice, a_nowSeconds, a_datagram, F4T::PacketRateLimit::Config{});
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

	void WireBudgetCountsEveryFrameBeforeDecoding()
	{
		auto wire = F4T::PacketRateLimit::Initialize(0.0);
		for (int i = 0; i < 512; ++i) {
			assert(CommonwealthOnline::Server::ObserveInboundWire(wire, 0.0, 15).verdict == Verdict::Allowed);
		}
		assert(CommonwealthOnline::Server::ObserveInboundWire(wire, 0.0, 15).verdict == Verdict::Limited);
		for (int window = 1; window <= 2; ++window) {
			for (int i = 0; i < 512; ++i) {
				assert(CommonwealthOnline::Server::ObserveInboundWire(wire, window, 15).verdict == Verdict::Allowed);
			}
			const auto result = CommonwealthOnline::Server::ObserveInboundWire(wire, window, 15);
			assert(result.verdict == (window == 2 ? Verdict::Disconnect : Verdict::Limited));
		}

		auto bytes = F4T::PacketRateLimit::Initialize(0.0);
		assert(CommonwealthOnline::Server::ObserveInboundWire(bytes, 0.0, 8ULL * 1024ULL * 1024ULL).verdict == Verdict::Allowed);
		const auto over = CommonwealthOnline::Server::ObserveInboundWire(bytes, 0.0, 1);
		assert(over.verdict == Verdict::Limited && over.byteBudgetExceeded);
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

	void ConfiguredBudgetReplacesTheDefaults()
	{
		const F4T::PacketRateLimit::Config generous{ 200, 1024ULL * 1024ULL, 3, 1.0 };
		auto global = F4T::PacketRateLimit::Initialize(0.0);
		auto voice = F4T::PacketRateLimit::Initialize(0.0);
		const auto datagram = Text("{\"type\":\"transform\"}");
		for (int packet = 0; packet < 200; ++packet) {
			assert(ObserveInboundDatagram(global, voice, 0.0, datagram, generous).verdict == Verdict::Allowed);
		}
		assert(ObserveInboundDatagram(global, voice, 0.0, datagram, generous).verdict == Verdict::Limited);

		auto strict = F4T::PacketRateLimit::Initialize(0.0);
		const F4T::PacketRateLimit::Config tight{ 120, 1024ULL, 3, 1.0 };
		assert(ObserveInboundDatagram(strict, voice, 0.0, datagram, tight).verdict == Verdict::Allowed);
		const auto over = ObserveInboundDatagram(strict, voice, 0.0, Bytes(2048, std::byte{ 0x20 }), tight);
		assert(over.verdict == Verdict::Limited && over.byteBudgetExceeded);
	}

	void SustainedMinigunBurstStaysUnderTheDefaultBudget()
	{
		Meters meters = NewMeters();
		const auto shot = Text(std::string(2048, 'a'));
		for (int index = 0; index < 20 * 30; ++index) {
			const auto result = Observe(meters, static_cast<double>(index) / 20.0, shot);
			assert(result.verdict == Verdict::Allowed);
		}
		assert(meters.global.violationWindows == 0);
	}

	void PerShotFullStateFloodIsStillKicked()
	{
		Meters meters = NewMeters();
		const auto fullState = Bytes(33000, std::byte{ 0x20 });
		const auto index = FirstDisconnectIndex(meters, fullState, 20, 20 * 10);
		assert(index >= 0 && index <= 20 * 4);
	}

	void WindowedTypeTallyCountsBytesByTypeAndResetsEachWindow()
	{
		std::map<std::string, std::uint64_t> tally;
		CommonwealthOnline::Server::NoteInboundWindowBytes(tally, false, "playerState", 100);
		CommonwealthOnline::Server::NoteInboundWindowBytes(tally, false, "transform", 400);
		CommonwealthOnline::Server::NoteInboundWindowBytes(tally, false, "playerState", 50);
		assert(tally.at("playerState") == 150 && tally.at("transform") == 400);
		assert(CommonwealthOnline::Server::DescribeInboundWindowBytes(tally) == "transform=400, playerState=150");

		CommonwealthOnline::Server::NoteInboundWindowBytes(tally, true, "npcHit", 10);
		assert(tally.size() == 1 && tally.at("npcHit") == 10);
		assert(CommonwealthOnline::Server::DescribeInboundWindowBytes({}) == "none");

		std::map<std::string, std::uint64_t> crowded;
		for (int type = 0; type < 40; ++type) {
			CommonwealthOnline::Server::NoteInboundWindowBytes(crowded, false, "type" + std::to_string(type), 1);
		}
		assert(crowded.size() == CommonwealthOnline::Server::kMaximumInboundWindowTypes + 1);
		assert(crowded.at("other") == 40 - CommonwealthOnline::Server::kMaximumInboundWindowTypes);
		CommonwealthOnline::Server::NoteInboundWindowBytes(crowded, false, std::string(100, 'x'), 5);
		assert(crowded.size() == CommonwealthOnline::Server::kMaximumInboundWindowTypes + 1);
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
	WireBudgetCountsEveryFrameBeforeDecoding();
	OnlyACovPrefixOfFourBytesReachesTheVoiceMeter();
	ConfiguredBudgetReplacesTheDefaults();
	SustainedMinigunBurstStaysUnderTheDefaultBudget();
	PerShotFullStateFloodIsStillKicked();
	WindowedTypeTallyCountsBytesByTypeAndResetsEachWindow();
	return 0;
}
