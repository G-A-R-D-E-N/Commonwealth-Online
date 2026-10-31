#include "F4TGnsSnapshotEnvelope.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>

int main()
{
	using namespace F4T::GnsSnapshotEnvelope;

	std::array<std::uint8_t, HeaderSize + 1> transform{};
	assert(WriteHeader(transform, Family::Transform, 0x01020304U));
	assert(transform[0] == 'C');
	assert(transform[1] == 'O');
	assert(transform[2] == 'G');
	assert(transform[3] == '2');
	assert(transform[4] == 1);
	assert(transform[5] == 1);
	assert(transform[6] == 0);
	assert(transform[7] == 0);
	assert(transform[8] == 0x01);
	assert(transform[9] == 0x02);
	assert(transform[10] == 0x03);
	assert(transform[11] == 0x04);
	transform[HeaderSize] = '{';

	Header decoded{};
	assert(Decode(transform, decoded) == DecodeResult::Valid);
	assert(decoded.family == Family::Transform);
	assert(decoded.sequence == 0x01020304U);

	std::array<std::uint8_t, HeaderSize + 1> npc{};
	assert(WriteHeader(npc, Family::NpcState, 7));
	npc[HeaderSize] = '{';
	assert(Decode(npc, decoded) == DecodeResult::Valid);
	assert(decoded.family == Family::NpcState);
	assert(decoded.sequence == 7);

	std::array<std::uint8_t, HeaderSize> empty{};
	assert(WriteHeader(empty, Family::Transform, 1));
	assert(Decode(empty, decoded) == DecodeResult::EmptyPayload);

	auto invalidVersion = transform;
	invalidVersion[4] = 2;
	assert(Decode(invalidVersion, decoded) == DecodeResult::InvalidHeader);

	auto invalidReserved = transform;
	invalidReserved[7] = 1;
	assert(Decode(invalidReserved, decoded) == DecodeResult::InvalidHeader);

	auto invalidFamily = transform;
	invalidFamily[5] = 3;
	assert(Decode(invalidFamily, decoded) == DecodeResult::UnknownFamily);

	auto zeroSequence = transform;
	zeroSequence[8] = 0;
	zeroSequence[9] = 0;
	zeroSequence[10] = 0;
	zeroSequence[11] = 0;
	assert(Decode(zeroSequence, decoded) == DecodeResult::ZeroSequence);

	const std::array<std::uint8_t, 4> notEnvelope{ '{', '}', '\n', 0 };
	assert(Decode(notEnvelope, decoded) == DecodeResult::NotEnvelope);
	assert(Decode(std::span<const std::uint8_t>{ transform.data(), 8 }, decoded) == DecodeResult::Truncated);

	assert(!WriteHeader(transform, Family::Transform, 0));
	assert(NextSequence(0) == 1);
	assert(NextSequence((std::numeric_limits<std::uint32_t>::max)()) == 1);
	assert(IsNewer(1, 0));
	assert(IsNewer(2, 1));
	assert(!IsNewer(1, 1));
	assert(!IsNewer(1, 2));
	assert(IsNewer(1, (std::numeric_limits<std::uint32_t>::max)()));
	assert(!IsNewer(0, (std::numeric_limits<std::uint32_t>::max)()));

	return 0;
}
