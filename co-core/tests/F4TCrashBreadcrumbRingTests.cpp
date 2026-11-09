#include "F4TCrashBreadcrumbRing.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

int main()
{
	using namespace F4T::CrashBreadcrumbs;

	static_assert(Capacity == 256);
	assert(Name(Code::kPrivateBaseParked) == "private-base-parked");
	assert(Name(Code::kUnknown) == "unknown");

	Ring ring;
	std::array<Event, 4> out{};
	assert(ring.Total() == 0);
	assert(ring.Snapshot(out) == 0);

	ring.Record(10, 111, Code::kCellChange, 0x14, 0);
	ring.Record(20, 222, Code::kPrivateBaseReused, 0xFF0012FF, 5);
	assert(ring.Total() == 2);
	std::size_t count = ring.Snapshot(out);
	assert(count == 2);
	assert(out[0].seq == 0 && out[0].code == Code::kCellChange && out[0].threadId == 111 && out[0].tickMs == 10);
	assert(out[1].seq == 1 && out[1].code == Code::kPrivateBaseReused && out[1].refId == 0xFF0012FFU && out[1].aux == 5);

	for (std::uint32_t index = 0; index < 300; ++index) {
		ring.Record(100 + index, 7, Code::kPuppetDelete, index, index * 2);
	}
	std::array<Event, Capacity> all{};
	count = ring.Snapshot(all);
	assert(count == Capacity);
	assert(all[0].seq == 302 - Capacity);
	assert(all[Capacity - 1].seq == 301);
	assert(all[Capacity - 1].refId == 299 && all[Capacity - 1].aux == 598);
	for (std::size_t index = 1; index < count; ++index) {
		assert(all[index].seq == all[index - 1].seq + 1);
	}

	std::array<Event, 3> small{};
	count = ring.Snapshot(small);
	assert(count == 3 && small[2].seq == 301 && small[0].seq == 299);

	std::array<char, 160> line{};
	Event sample{ 42, 1234, 4732, Code::kProxyDespawn, 0xFF001303, 9 };
	const auto size = FormatLine(sample, line);
	const std::string_view text{ line.data(), size };
	assert(text == "#42 +1234ms t=4732 proxy-despawn form=FF001303 aux=9\n");

	std::array<char, 8> tiny{};
	const auto truncated = FormatLine(sample, tiny);
	assert(truncated == tiny.size());
	assert(std::string_view(tiny.data(), tiny.size()) == "#42 +123");

	Event zero{};
	std::array<char, 80> zeroLine{};
	const auto zeroSize = FormatLine(zero, zeroLine);
	assert(std::string_view(zeroLine.data(), zeroSize) == "#0 +0ms t=0 unknown form=00000000 aux=0\n");
	return 0;
}
