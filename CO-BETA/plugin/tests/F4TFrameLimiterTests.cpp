#include "F4TFrameLimiterPacing.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(expr)                                                                 \
	do {                                                                            \
		if (!(expr)) {                                                              \
			std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr); \
			std::abort();                                                           \
		}                                                                           \
	} while (false)

int main()
{
	namespace Pacing = F4T::FrameLimiterPacing;
	constexpr std::int64_t kPeriod = 100;

	CHECK(Pacing::BlockNs(1000, 100) == 900);
	CHECK(Pacing::BlockNs(100, 100) == 0);
	CHECK(Pacing::BlockNs(50, 100) == 0);

	CHECK(Pacing::NextAnchorNs(0, kPeriod, 100) == 100);
	CHECK(Pacing::NextAnchorNs(0, kPeriod, 150) == 100);
	CHECK(Pacing::NextAnchorNs(0, kPeriod, 199) == 100);
	CHECK(Pacing::NextAnchorNs(0, kPeriod, 200) == 200);
	CHECK(Pacing::NextAnchorNs(0, kPeriod, 1000) == 1000);

	std::int64_t anchor = 0;
	for (int frame = 0; frame < 60; ++frame) {
		anchor = Pacing::NextAnchorNs(anchor, kPeriod, anchor + kPeriod);
	}
	CHECK(anchor == 60 * kPeriod);

	CHECK(Pacing::ShouldCap(true, false, false));
	CHECK(!Pacing::ShouldCap(false, false, false));
	CHECK(!Pacing::ShouldCap(true, true, false));
	CHECK(!Pacing::ShouldCap(true, false, true));

	std::printf("frame limiter pacing contract: pass\n");
	return 0;
}
