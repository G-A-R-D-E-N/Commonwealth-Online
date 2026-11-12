#include "F4TSettlementCapturePolicy.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace
{
	struct TestRecord
	{
		std::string status;
		std::chrono::steady_clock::time_point receivedAt{};
	};
}

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

	const Anchor bench{ 8, true, Point{ 400.0F, 0.0F, 0.0F }, kDefaultCaptureRadius };
	assert(IsAtWorkbench(origin, bench));
	assert(!IsAtWorkbench(Point{ 1000.0F, 0.0F, 0.0F }, bench));
	assert(!IsAtWorkbench(origin, Anchor{ 9, false, Point{}, kDefaultCaptureRadius }));

	assert(CanStartCapture(false, "bob", "alice"));
	assert(CanStartCapture(false, "", "alice"));
	assert(!CanStartCapture(true, "bob", "alice"));
	assert(!CanStartCapture(false, "alice", "alice"));
	assert(!CanStartCapture(false, "bob", ""));

	assert(IsActiveStatus("started") && IsActiveStatus("holding") && IsActiveStatus("contested"));
	assert(!IsActiveStatus("cancelled") && !IsActiveStatus("captured"));

	const auto start = std::chrono::steady_clock::time_point{} + std::chrono::seconds{ 1000 };
	std::map<std::uint64_t, TestRecord> records{
		{ 7, TestRecord{ "holding", start } },
		{ 8, TestRecord{ "holding", start } },
		{ 9, TestRecord{ "captured", start } },
	};
	CancelOnReject(records, 7, "settlementLimit");
	assert(records.at(7).status == "cancelled");
	CancelOnReject(records, 8, "vetoed");
	assert(records.at(8).status == "holding");
	CancelOnReject(records, 9, "settlementLimit");
	assert(records.at(9).status == "captured");
	CancelOnReject(records, 10, "settlementLimit");
	assert(records.find(10) == records.end());

	std::map<std::uint64_t, TestRecord> stale{
		{ 1, TestRecord{ "holding", start } },
		{ 2, TestRecord{ "started", start + std::chrono::seconds{ 10 } } },
		{ 3, TestRecord{ "captured", start } },
		{ 4, TestRecord{ "contested", start } },
		{ 5, TestRecord{ "holding", start + std::chrono::seconds{ 1 } } },
	};
	const auto now = start + std::chrono::seconds{ 16 };
	const auto expired = CancelStale(stale, now, std::chrono::seconds{ 15 });
	assert(expired.size() == 2 && expired[0] == 1 && expired[1] == 4);
	assert(stale.at(1).status == "cancelled");
	assert(stale.at(2).status == "started");
	assert(stale.at(3).status == "captured");
	assert(stale.at(4).status == "cancelled");
	assert(stale.at(5).status == "holding");
	assert(CancelStale(stale, now, std::chrono::seconds{ 15 }).empty());

	assert(!IsStale(start, start + std::chrono::seconds{ 15 }, std::chrono::seconds{ 15 }));
	assert(IsStale(start, start + std::chrono::seconds{ 15 } + std::chrono::nanoseconds{ 1 }, std::chrono::seconds{ 15 }));

	return 0;
}
