#include "F4TSettlementCapturePolicy.h"

#include <cassert>
#include <vector>

int main()
{
	using namespace F4T::SettlementCapturePolicy;

	assert(kDefaultCaptureRadius == 800.0F);

	const Point origin{ 0.0F, 0.0F, 0.0F };
	const Anchor atOrigin{ 1, true, origin, kDefaultCaptureRadius };
	assert(IsWithinCaptureRadius(Point{ 800.0F, 0.0F, 0.0F }, atOrigin));
	assert(!IsWithinCaptureRadius(Point{ 800.5F, 0.0F, 0.0F }, atOrigin));
	assert(IsWithinCaptureRadius(Point{ 0.0F, 565.0F, 565.0F }, atOrigin));
	assert(!IsWithinCaptureRadius(Point{ 0.0F, 600.0F, 600.0F }, atOrigin));

	const Anchor wide{ 2, true, origin, 1500.0F };
	assert(IsWithinCaptureRadius(Point{ 1200.0F, 0.0F, 0.0F }, wide));
	assert(!IsWithinCaptureRadius(Point{ 1200.0F, 0.0F, 0.0F }, atOrigin));

	const Anchor noWorkbench{ 3, false, origin, kDefaultCaptureRadius };
	assert(!IsWithinCaptureRadius(origin, noWorkbench));

	const Anchor farNear{ 4, true, Point{ 100.0F, 0.0F, 0.0F }, kDefaultCaptureRadius };
	const Anchor nearest{ 5, true, Point{ 10.0F, 0.0F, 0.0F }, kDefaultCaptureRadius };
	const Anchor hidden{ 6, false, Point{ 1.0F, 0.0F, 0.0F }, kDefaultCaptureRadius };
	const std::vector<Anchor> anchors{ farNear, nearest, hidden };
	assert(FindNearest(origin, anchors) == std::optional<std::uint64_t>{ 5 });
	const std::vector<Anchor> none{ Anchor{ 7, true, Point{ 9000.0F, 0.0F, 0.0F }, kDefaultCaptureRadius } };
	assert(!FindNearest(origin, none).has_value());
	assert(!FindNearest(origin, std::vector<Anchor>{}).has_value());

	assert(CanStartCapture(false, "bob", "alice"));
	assert(CanStartCapture(false, "", "alice"));
	assert(!CanStartCapture(true, "bob", "alice"));
	assert(!CanStartCapture(false, "alice", "alice"));
	assert(!CanStartCapture(false, "bob", ""));

	return 0;
}
