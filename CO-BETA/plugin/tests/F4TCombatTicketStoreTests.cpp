#include "F4TCombatTicketStore.h"

#include <array>
#include <barrier>
#include <cassert>
#include <chrono>
#include <thread>

int main()
{
	using namespace F4T::CombatTicketStore;
	assert(ShotScope::Current() == nullptr);
	std::uint64_t outerShot{};
	{
		ShotScope outer(1);
		outerShot = outer.Shot();
		assert(outerShot != 0 && ShotScope::Current() == &outer);
		assert(!outer.HasLaunch());
		{
			ShotScope masked(0);
			assert(masked.Shot() == 0 && ShotScope::Current() == &masked);
			{
				ShotScope inner(1);
				assert(inner.Shot() != 0 && inner.Shot() != outerShot);
				assert(ShotScope::Current() == &inner);
				inner.MarkLaunched();
				assert(inner.HasLaunch() && !outer.HasLaunch());
			}
			assert(ShotScope::Current() == &masked);
		}
		assert(ShotScope::Current() == &outer);
		outer.MarkLaunched();
		outer.MarkLaunched();
		assert(outer.HasLaunch());
	}
	assert(ShotScope::Current() == nullptr);
	std::uint64_t concurrentShot{};
	std::thread otherThread([&] {
		assert(ShotScope::Current() == nullptr);
		ShotScope shot(1);
		concurrentShot = shot.Shot();
	});
	otherThread.join();
	assert(concurrentShot != 0 && concurrentShot != outerShot);
	std::array<std::uint64_t, 2> simultaneousShots{};
	std::barrier rendezvous(2);
	const auto captureShot = [&](std::size_t a_index) {
		ShotScope shot(1);
		rendezvous.arrive_and_wait();
		assert(ShotScope::Current() == &shot);
		simultaneousShots[a_index] = shot.Shot();
	};
	std::thread firstShot(captureShot, 0);
	std::thread secondShot(captureShot, 1);
	firstShot.join();
	secondShot.join();
	assert(simultaneousShots[0] != 0 && simultaneousShots[1] != 0);
	assert(simultaneousShots[0] != simultaneousShots[1]);

	Store store;
	assert(store.SetSession(1));
	assert(!store.CommitShot(1, 10, 100));
	assert(store.TrackShot(1, 10));
	assert(store.Sequence(1, 10) == 0);
	assert(store.CommitShot(1, 10, 100));
	assert(store.Sequence(1, 10) == 100);
	assert(!store.CommitShot(1, 10, 101));
	assert(store.TrackShot(1, 10));
	assert(store.Sequence(1, 10) == 100);

	assert(store.TrackShot(1, 11));
	assert(store.SetSession(0));
	assert(!store.SetSession(1));
	assert(store.SetSession(2));
	assert(store.Sequence(2, 10) == 0);
	assert(!store.CommitShot(1, 10, 100));

	assert(store.TrackShot(2, 12));
	assert(store.TrackShot(2, 13));
	assert(store.TrackShot(2, 14));
	assert(store.TrackShot(2, 10 + Store::kCapacity));
	assert(!store.TrackShot(2, 10));
	assert(store.Sequence(2, 10) == 0);
	assert(!store.CommitShot(2, 10, 100));

	using Clock = std::chrono::steady_clock;
	const auto base = Clock::now();
	const auto at = [&](int a_ms) { return base + std::chrono::milliseconds{ a_ms }; };

	Store ledger;
	assert(ledger.SetSession(1));
	assert(ledger.RecordLaunch(1, 7, 0x1234, at(0)));
	assert(ledger.ShotForHit(1, 0x1234, at(10)) == 7);
	assert(ledger.ShotForHit(1, 0x1234, at(5000)) == 7);
	assert(ledger.ShotForHit(1, 0x1234, at(-1)) == 0);

	Store relaunch;
	assert(relaunch.SetSession(1));
	assert(relaunch.RecordLaunch(1, 7, 0x1234, at(0)));
	assert(relaunch.RecordLaunch(1, 8, 0x1234, at(1000)));
	assert(relaunch.ShotForHit(1, 0x1234, at(500)) == 7);
	assert(relaunch.ShotForHit(1, 0x1234, at(1050)) == 0);
	assert(relaunch.ShotForHit(1, 0x1234, at(1000) + kRelaunchAmbiguity - std::chrono::milliseconds{ 1 }) == 0);
	assert(relaunch.ShotForHit(1, 0x1234, at(1000) + kRelaunchAmbiguity) == 8);
	assert(relaunch.ShotForHit(1, 0x1234, at(1200)) == 0);
	assert(relaunch.ShotForHit(1, 0x1234, at(5000)) == 8);

	Store spaced;
	assert(spaced.SetSession(1));
	assert(spaced.RecordLaunch(1, 7, 0x1234, at(0)));
	assert(spaced.RecordLaunch(1, 8, 0x1234, at(3000)));
	assert(spaced.ShotForHit(1, 0x1234, at(3010)) == 8);

	assert(!ledger.RecordLaunch(1, 9, 0, at(200)));
	assert(!ledger.RecordLaunch(0, 9, 0x1234, at(200)));
	assert(!ledger.RecordLaunch(1, 0, 0x1234, at(200)));
	assert(!ledger.RecordLaunch(2, 9, 0x1234, at(200)));
	assert(ledger.ShotForHit(1, 0x1234, at(300)) == 7);
	assert(ledger.ShotForHit(1, 0, at(300)) == 0);
	assert(ledger.ShotForHit(0, 0x1234, at(300)) == 0);

	assert(ledger.SetSession(2));
	assert(ledger.ShotForHit(1, 0x1234, at(300)) == 0);
	assert(ledger.ShotForHit(2, 0x1234, at(300)) == 0);
	assert(!ledger.RecordLaunch(1, 12, 0x1234, at(400)));
	assert(ledger.RecordLaunch(2, 11, 0x1234, at(300)));
	assert(ledger.SetSession(2));
	assert(ledger.ShotForHit(2, 0x1234, at(400)) == 11);
	assert(ledger.ShotForHit(1, 0x1234, at(400)) == 0);

	Store ring;
	assert(ring.SetSession(1));
	constexpr std::uint32_t ringBase = 0x4000;
	for (int i = 1; i <= 300; ++i) {
		assert(ring.RecordLaunch(1, static_cast<std::uint64_t>(i), ringBase + static_cast<std::uint32_t>(i), at(i)));
	}
	assert(ring.ShotForHit(1, ringBase + 1, at(1)) == 0);
	assert(ring.ShotForHit(1, ringBase + 44, at(44)) == 0);
	assert(ring.ShotForHit(1, ringBase + 45, at(45)) == 45);
	assert(ring.ShotForHit(1, ringBase + 300, at(300)) == 300);

	Store handles;
	assert(handles.SetSession(1));
	assert(handles.RecordLaunch(1, 21, 0xA, at(0)));
	assert(handles.RecordLaunch(1, 22, 0xB, at(5)));
	assert(handles.ShotForHit(1, 0xA, at(10)) == 21);
	assert(handles.ShotForHit(1, 0xB, at(10)) == 22);
	assert(handles.ShotForHit(1, 0xC, at(10)) == 0);
	assert(handles.ShotForHit(1, 0xA, at(4)) == 21);
	assert(handles.ShotForHit(1, 0xB, at(4)) == 0);
	assert(handles.RecordLaunch(1, 23, 0xA, at(20)));
	assert(handles.ShotForHit(1, 0xA, at(30)) == 0);
	assert(handles.ShotForHit(1, 0xB, at(30)) == 22);
	return 0;
}
