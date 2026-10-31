#include "F4TVoiceWire.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

namespace
{
	using namespace F4T::Voice;

	using Buffer = std::array<std::byte, kVoiceMaxDatagramBytes + 1>;
	using Payload = std::array<std::byte, kVoiceMaxPayloadBytes + 1>;

	std::span<const std::byte> View(const Buffer& a_buffer, std::size_t a_size) noexcept
	{
		return std::span<const std::byte>(a_buffer.data(), a_size);
	}

	std::span<const std::byte> PayloadView(const Payload& a_payload, std::size_t a_size) noexcept
	{
		return std::span<const std::byte>(a_payload.data(), a_size);
	}

	std::byte Octet(std::uint8_t a_value) noexcept
	{
		return static_cast<std::byte>(a_value);
	}

	VoiceHeader MakeHeader(Tier a_tier, bool a_start, bool a_end, std::uint16_t a_speaker, std::uint32_t a_sequence) noexcept
	{
		VoiceHeader header{};
		header.tier = a_tier;
		header.burstStart = a_start;
		header.burstEnd = a_end;
		header.speakerId = a_speaker;
		header.sequence = a_sequence;
		return header;
	}

	void TestHeaderAndRoundTrip()
	{
		assert(kVoiceHeaderBytes == 12);
		assert(kVoiceMaxPayloadBytes == 160);
		assert(kVoiceMaxDatagramBytes == 172);
		assert(kVoiceSequenceWindowTicks == 64);

		Payload payload{};
		for (std::size_t i = 0; i < payload.size(); ++i) {
			payload[i] = static_cast<std::byte>(i + 1);
		}
		Buffer buffer{};

		const auto single = EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 1), PayloadView(payload, 1), buffer);
		assert(single == kVoiceHeaderBytes + 1);
		assert(std::to_integer<std::uint8_t>(buffer[5]) == 0x01);
		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, true, false, 0, 1), PayloadView(payload, 1), buffer) == kVoiceHeaderBytes + 1);
		assert(std::to_integer<std::uint8_t>(buffer[5]) == 0x05);
		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, true, 0, 1), PayloadView(payload, 1), buffer) == kVoiceHeaderBytes + 1);
		assert(std::to_integer<std::uint8_t>(buffer[5]) == 0x09);

		const std::array<Tier, 3> tiers{ Tier::Whisper, Tier::Normal, Tier::Shout };
		for (const auto tier : tiers) {
			for (std::uint8_t burst = 0; burst < 4; ++burst) {
				const auto sent = MakeHeader(tier, (burst & 1U) != 0, (burst & 2U) != 0, 0x0042, 0x89ABCDEFU);
				const auto written = EncodeVoicePacket(sent, PayloadView(payload, 5), buffer);
				assert(written == kVoiceHeaderBytes + 5);

				VoicePacket packet{};
				assert(DecodeVoicePacket(View(buffer, written), VoiceDirection::ServerToClient, packet) == VoiceDecodeResult::Ok);
				assert(packet.header.profile == kVoiceProfileOpus48kMono20ms);
				assert(packet.header.tier == tier);
				assert(packet.header.burstStart == sent.burstStart);
				assert(packet.header.burstEnd == sent.burstEnd);
				assert(packet.header.speakerId == 0x0042);
				assert(packet.header.sequence == 0x89ABCDEFU);
				assert(packet.payload.size() == 5);
				assert(packet.payload[0] == payload[0]);
				assert(packet.payload[4] == payload[4]);
			}
		}

		assert(std::to_integer<std::uint8_t>(buffer[4]) == 0);
		assert(std::to_integer<std::uint8_t>(buffer[8]) == 0x89);
		assert(std::to_integer<std::uint8_t>(buffer[9]) == 0xAB);
		assert(std::to_integer<std::uint8_t>(buffer[10]) == 0xCD);
		assert(std::to_integer<std::uint8_t>(buffer[11]) == 0xEF);
	}

	void TestPayloadBoundaries()
	{
		Payload payload{};
		Buffer buffer{};
		const auto header = MakeHeader(Tier::Normal, false, false, 0, 9);

		assert(EncodeVoicePacket(header, PayloadView(payload, 0), buffer) == 0);
		assert(EncodeVoicePacket(header, PayloadView(payload, 161), buffer) == 0);
		assert(EncodeVoicePacket(header, PayloadView(payload, 1), buffer) == 13);
		assert(EncodeVoicePacket(header, PayloadView(payload, 160), buffer) == 172);
		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 0), PayloadView(payload, 1), buffer) == 0);
		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 4), PayloadView(payload, 2), std::span<std::byte>(buffer.data(), 13)) == 0);

		VoicePacket packet{};
		assert(DecodeVoicePacket(View(buffer, 13), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::Ok);
		assert(packet.payload.size() == 1);
		assert(DecodeVoicePacket(View(buffer, 172), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::Ok);
		assert(packet.payload.size() == 160);
		assert(DecodeVoicePacket(View(buffer, 12), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadSize);
		assert(DecodeVoicePacket(View(buffer, 173), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadSize);
	}

	void TestRejectedFields()
	{
		Payload payload{};
		Buffer buffer{};
		VoicePacket packet{};

		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 3), PayloadView(payload, 2), buffer) == 14);

		buffer[4] = Octet(1);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadProfile);
		buffer[4] = Octet(0);

		for (std::uint8_t bit = 4; bit < 8; ++bit) {
			buffer[5] = Octet(static_cast<std::uint8_t>(1U << bit) | 0x01U);
			assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadFlags);
		}

		buffer[5] = Octet(0x03);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadTier);
		buffer[5] = Octet(0x07);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadTier);

		buffer[5] = Octet(0x01);
		buffer[8] = Octet(0);
		buffer[9] = Octet(0);
		buffer[10] = Octet(0);
		buffer[11] = Octet(0);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadSequence);
		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 0), PayloadView(payload, 2), buffer) == 0);
	}

	void TestPrefixAndSpeaker()
	{
		Payload payload{};
		Buffer buffer{};
		VoicePacket packet{};

		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 4), PayloadView(payload, 2), buffer) == 14);

		assert(HasVoicePrefix(View(buffer, 14)));
		assert(!HasVoicePrefix(View(buffer, 3)));
		assert(!HasVoicePrefix(View(buffer, 0)));
		assert(DecodeVoicePacket(View(buffer, 0), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);
		for (std::size_t size = 0; size < 4; ++size) {
			assert(DecodeVoicePacket(View(buffer, size), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);
		}
		assert(HasVoicePrefix(View(buffer, 4)));
		assert(DecodeVoicePacket(View(buffer, 4), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadSize);

		buffer[3] = Octet('2');
		assert(!HasVoicePrefix(View(buffer, 14)));
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);

		buffer[0] = Octet('{');
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);

		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 4), PayloadView(payload, 2), buffer) == 14);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::Ok);
		assert(packet.header.speakerId == 0);

		buffer[1] = Octet('X');
		assert(!HasVoicePrefix(View(buffer, 14)));
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);
		buffer[1] = Octet('O');
		buffer[2] = Octet('X');
		assert(!HasVoicePrefix(View(buffer, 14)));
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::NotVoice);
		buffer[2] = Octet('V');
		assert(HasVoicePrefix(View(buffer, 14)));

		buffer[6] = Octet(0x00);
		buffer[7] = Octet(0x01);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ClientToServer, packet) == VoiceDecodeResult::BadSpeaker);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ServerToClient, packet) == VoiceDecodeResult::Ok);
		assert(packet.header.speakerId == 1);

		assert(EncodeVoicePacket(MakeHeader(Tier::Normal, false, false, 0, 4), PayloadView(payload, 2), buffer) == 14);
		assert(WriteSpeakerId(std::span<std::byte>(buffer.data(), 14), 0x0ABC));
		assert(std::to_integer<std::uint8_t>(buffer[6]) == 0x0A);
		assert(std::to_integer<std::uint8_t>(buffer[7]) == 0xBC);
		assert(DecodeVoicePacket(View(buffer, 14), VoiceDirection::ServerToClient, packet) == VoiceDecodeResult::Ok);
		assert(packet.header.speakerId == 0x0ABC);
		assert(packet.header.sequence == 4);
		assert(!WriteSpeakerId(std::span<std::byte>(buffer.data(), 11), 0x0001));

		Buffer text{};
		text.fill(Octet('N'));
		assert(!WriteSpeakerId(std::span<std::byte>(text.data(), 14), 0x0ABC));
		assert(text[6] == Octet('N'));
		assert(text[7] == Octet('N'));
	}

	void TestTicks()
	{
		assert(NextTick(1) == 2);
		assert(NextTick(0xFFFFFFFEU) == 0xFFFFFFFFU);
		assert(NextTick(0xFFFFFFFFU) == 1);

		assert(TickDistance(10, 10) == 0);
		assert(TickDistance(10, 13) == 3);
		assert(TickDistance(0xFFFFFFFFU, 1) == 1);
		assert(TickDistance(0xFFFFFFFEU, 0xFFFFFFFFU) == 1);
		assert(TickDistance(0xFFFFFFFEU, 2) == 3);
		assert(TickDistance(1, 2) == 1);
		assert(TickDistance(5, 3) == 0xFFFFFFFDU);
	}

	void TestSequenceWindow()
	{
		SequenceWindow window{};
		assert(Check(window, 0) == SequenceVerdict::Zero);
		assert(Check(window, 102) == SequenceVerdict::Accept);
		Commit(window, 102);
		assert(Check(window, 102) == SequenceVerdict::Duplicate);
		assert(Check(window, 101) == SequenceVerdict::Accept);
		Commit(window, 101);
		assert(Check(window, 101) == SequenceVerdict::Duplicate);
		assert(Check(window, 102) == SequenceVerdict::Duplicate);

		SequenceWindow edge{};
		Commit(edge, 1000);
		assert(Check(edge, 937) == SequenceVerdict::Accept);
		assert(Check(edge, 936) == SequenceVerdict::TooOld);
		Commit(edge, 937);
		assert(Check(edge, 937) == SequenceVerdict::Duplicate);

		SequenceWindow wrap{};
		const std::array<std::uint32_t, 4> order{ 0xFFFFFFFEU, 0xFFFFFFFFU, 1U, 2U };
		for (const auto sequence : order) {
			assert(Check(wrap, sequence) == SequenceVerdict::Accept);
			Commit(wrap, sequence);
		}
		assert(Check(wrap, 0xFFFFFFFFU) == SequenceVerdict::Duplicate);
		assert(TickDistance(0xFFFFFFC2U, 2U) == 63);
		assert(Check(wrap, 0xFFFFFFC2U) == SequenceVerdict::Accept);
		assert(Check(wrap, 0xFFFFFFC1U) == SequenceVerdict::TooOld);
		assert(TickDistance(0xFFFFFFFEU, 2U) == 3);

		SequenceWindow untouched{};
		Commit(untouched, 50);
		const auto before = untouched;
		assert(Check(untouched, 51) == SequenceVerdict::Accept);
		assert(Check(untouched, 49) == SequenceVerdict::Accept);
		assert(untouched.highWater == before.highWater);
		assert(untouched.seen == before.seen);

		SequenceWindow shift{};
		Commit(shift, 100);
		Commit(shift, 103);
		assert(Check(shift, 100) == SequenceVerdict::Duplicate);
		assert(Check(shift, 101) == SequenceVerdict::Accept);

		SequenceWindow shift63{};
		Commit(shift63, 100);
		Commit(shift63, 163);
		assert(Check(shift63, 100) == SequenceVerdict::Duplicate);

		SequenceWindow jump{};
		Commit(jump, 1000);
		Commit(jump, 999);
		Commit(jump, 1064);
		assert(Check(jump, 1063) == SequenceVerdict::Accept);

		SequenceWindow stale{};
		Commit(stale, 1000);
		Commit(stale, 935);
		assert(Check(stale, 999) == SequenceVerdict::Accept);
	}
}

int main()
{
	TestHeaderAndRoundTrip();
	TestPayloadBoundaries();
	TestRejectedFields();
	TestPrefixAndSpeaker();
	TestTicks();
	TestSequenceWindow();
	return 0;
}
