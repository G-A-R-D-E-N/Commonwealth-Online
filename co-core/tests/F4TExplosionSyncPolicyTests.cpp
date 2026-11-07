#include "F4TExplosionSyncPolicy.h"

#include <cassert>
#include <chrono>

int main()
{
	using namespace F4T::ExplosionSyncPolicy;
	using std::chrono::milliseconds;

	{
		assert(WithinRadius(Point{ 300.0, 0.0, 0.0 }, Point{}, kProxyMatchRadius));
		assert(!WithinRadius(Point{ 300.5, 0.0, 0.0 }, Point{}, kProxyMatchRadius));
		assert(!WithinRadius(Point{ 0.0, 250.0, 250.0 }, Point{}, kProxyMatchRadius));
	}
	{
		assert(InReplayRange(Point{ 9000.0, 0.0, 0.0 }, Point{}));
		assert(!InReplayRange(Point{ 9000.5, 0.0, 0.0 }, Point{}));
	}
	{
		assert(SameSpace(true, 0x1234, 0x3C, 0x5678, 0x3C));
		assert(!SameSpace(true, 0x1234, 0x3C, 0x1234, 0x3D));
		assert(!SameSpace(true, 0x1234, 0, 0x1234, 0));
		assert(SameSpace(false, 0x1234, 0x3C, 0x1234, 0));
		assert(!SameSpace(false, 0x1234, 0x3C, 0x5678, 0x3C));
		assert(!SameSpace(false, 0, 0x3C, 0, 0));
	}
	{
		assert(ProxyBlastMatches(7, 7, Point{ 100.0, 0.0, 0.0 }, Point{}, milliseconds{ 500 }));
		assert(!ProxyBlastMatches(7, 7, Point{ 100.0, 0.0, 0.0 }, Point{}, milliseconds{ 501 }));
		assert(!ProxyBlastMatches(7, 7, Point{ 301.0, 0.0, 0.0 }, Point{}, milliseconds{ 0 }));
		assert(!ProxyBlastMatches(7, 8, Point{ 100.0, 0.0, 0.0 }, Point{}, milliseconds{ 0 }));
	}
	{
		assert(Decide(DecisionInput{ false, true, true, true, milliseconds{ 0 } }) == Decision::Drop);
		assert(Decide(DecisionInput{ true, false, false, false, milliseconds{ 0 } }) == Decision::Drop);
		assert(Decide(DecisionInput{ true, true, false, false, milliseconds{ 0 } }) == Decision::Replay);
		assert(Decide(DecisionInput{ true, true, true, true, milliseconds{ 0 } }) == Decision::Drop);
		assert(Decide(DecisionInput{ true, true, true, false, milliseconds{ 499 } }) == Decision::Wait);
		assert(Decide(DecisionInput{ true, true, true, false, milliseconds{ 500 } }) == Decision::Replay);
	}

	{
		const std::vector<SeenRef> seen{ SeenRef{ 0x1000, 0x42 } };
		assert(!IsNewRef(seen, SeenRef{ 0x1000, 0x42 }));
		assert(IsNewRef(seen, SeenRef{ 0x2000, 0x42 }));
		assert(IsNewRef(seen, SeenRef{ 0x1000, 0x43 }));
		assert(IsNewRef({}, SeenRef{ 0x1000, 0x42 }));
	}

	{
		using Clock = std::chrono::steady_clock;
		const auto start = Clock::time_point{};
		std::vector<ChainChildRecord> records;
		RecordChainChild(records, 0, start);
		assert(records.empty());
		assert(!IsChainChild(records, 0));
		RecordChainChild(records, 0x0100AB, start);
		assert(IsChainChild(records, 0x0100AB));
		assert(!IsChainChild(records, 0x0100AC));
		RecordChainChild(records, 0x0100AB, start + std::chrono::seconds{ 5 });
		assert(records.size() == 1);
		PruneChainChildren(records, start + std::chrono::seconds{ 14 }, std::chrono::seconds{ 10 });
		assert(IsChainChild(records, 0x0100AB));
		PruneChainChildren(records, start + std::chrono::seconds{ 16 }, std::chrono::seconds{ 10 });
		assert(records.empty());
	}

	{
		assert(GiveBackAllowed(1.0));
		assert(!GiveBackAllowed(0.0));
		assert(!GiveBackAllowed(-30.0));
	}

	return 0;
}
