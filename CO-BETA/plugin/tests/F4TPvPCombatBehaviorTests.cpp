#include "F4TPvPCombatContract.h"

#include <cassert>
#include <limits>

namespace
{
	void VerifyRoutingValidation()
	{
		using namespace F4T::PvPCombatContract;

		constexpr std::uint32_t localPlayer = 20;
		constexpr std::uint32_t remoteSender = 10;
		constexpr std::uint32_t otherPlayer = 30;

		assert(ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 25.0F));
		assert(!ValidateHitRouting(remoteSender, otherPlayer, localPlayer, 1, 25.0F));
		assert(!ValidateHitRouting(localPlayer, localPlayer, localPlayer, 1, 25.0F));
		assert(!ValidateHitRouting(0, localPlayer, localPlayer, 1, 25.0F));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 0, 25.0F));

		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 0.0F));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, -1.0F));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 10000.1F));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, std::numeric_limits<float>::quiet_NaN()));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, std::numeric_limits<float>::infinity()));
		assert(!ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, -std::numeric_limits<float>::infinity()));

		assert(ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 0.001F));
		assert(ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 50.0F));
		assert(ValidateHitRouting(remoteSender, localPlayer, localPlayer, 1, 10000.0F));
	}

	void VerifySequenceDeduplicationAndStaleness()
	{
		using namespace F4T::PvPCombatContract;

		InboundHitTracker tracker;
		constexpr std::uint32_t sender = 10;

		assert(tracker.QueueHit(sender, 10, 10.0F));
		assert(tracker.pendingHits.size() == 1);
		assert(tracker.lastSeenSequence[sender] == 10);

		assert(!tracker.QueueHit(sender, 10, 10.0F));
		assert(!tracker.QueueHit(sender, 9, 10.0F));
		assert(!tracker.QueueHit(sender, 1, 10.0F));
		assert(tracker.pendingHits.size() == 1);

		assert(tracker.QueueHit(sender, 11, 15.0F));
		assert(tracker.pendingHits.size() == 2);
		assert(tracker.lastSeenSequence[sender] == 11);

		tracker.lastSeenSequence[sender] = (std::numeric_limits<std::uint32_t>::max)();
		assert(tracker.QueueHit(sender, 1, 5.0F));
		assert(tracker.lastSeenSequence[sender] == 1);
		assert(!tracker.QueueHit(sender, (std::numeric_limits<std::uint32_t>::max)(), 5.0F));
	}

	void VerifyDisconnectReset()
	{
		using namespace F4T::PvPCombatContract;

		InboundHitTracker tracker;
		constexpr std::uint32_t sender = 10;

		assert(tracker.QueueHit(sender, 5, 20.0F));
		assert(tracker.QueueHit(sender, 6, 20.0F));
		assert(!tracker.QueueHit(sender, 4, 20.0F));
		assert(!tracker.QueueHit(sender, 6, 20.0F));

		tracker.ForgetSenderSequence(sender);
		assert(tracker.lastSeenSequence.find(sender) == tracker.lastSeenSequence.end());

		assert(tracker.QueueHit(sender, 1, 20.0F));
		assert(tracker.lastSeenSequence[sender] == 1);
		assert(tracker.QueueHit(sender, 2, 20.0F));
	}

	void VerifyPendingHitsSurviveSenderForget()
	{
		using namespace F4T::PvPCombatContract;

		InboundHitTracker tracker;
		assert(tracker.QueueHit(10, 1, 10.0F));
		assert(tracker.QueueHit(10, 2, 15.0F));
		assert(tracker.QueueHit(11, 1, 20.0F));
		assert(tracker.QueueHit(11, 2, 25.0F));
		assert(tracker.pendingHits.size() == 4);

		tracker.ForgetSenderSequence(10);
		assert(tracker.pendingHits.size() == 4);
		assert(tracker.pendingHits.front().senderId == 10);
		assert(tracker.QueueHit(10, 1, 5.0F));
		assert(tracker.pendingHits.size() == 5);
	}

	void VerifyQueueCapacityCoalescesSameSender()
	{
		using namespace F4T::PvPCombatContract;

		InboundHitTracker tracker;
		constexpr std::uint32_t sender = 10;
		for (std::uint32_t seq = 1; seq <= 40; ++seq) {
			assert(tracker.QueueHit(sender, seq, 1.0F));
		}
		assert(tracker.pendingHits.size() == kMaxPendingHits);
		assert(tracker.evictions == 0);

		float damage = 0.0F;
		std::uint32_t count = 0;
		for (const auto& hit : tracker.pendingHits) {
			damage += hit.damage;
			count += hit.count;
		}
		assert(damage == 40.0F);
		assert(count == 40);
		assert(tracker.pendingHits.front().sequence == 1);
		assert(tracker.pendingHits.back().sequence == 32);
	}

	void VerifyFullQueueEvictsOldestWithoutMatch()
	{
		using namespace F4T::PvPCombatContract;

		InboundHitTracker senders;
		for (std::uint32_t senderId = 1; senderId <= kMaxPendingHits + 1; ++senderId) {
			assert(senders.QueueHit(senderId, 1, 1.0F));
		}
		assert(senders.pendingHits.size() == kMaxPendingHits);
		assert(senders.evictions == 1);
		assert(senders.pendingHits.front().senderId == 2);

		InboundHitTracker npcs;
		for (std::uint32_t seq = 1; seq <= 32; ++seq) {
			assert(npcs.QueueHit(10, seq, 1.0F, 1));
		}
		assert(npcs.QueueHit(10, 33, 5.0F, 2));
		assert(npcs.evictions == 1);
		assert(npcs.pendingHits.back().npcId == 2 && npcs.pendingHits.back().count == 1);
	}
}

int main()
{
	VerifyRoutingValidation();
	VerifySequenceDeduplicationAndStaleness();
	VerifyDisconnectReset();
	VerifyPendingHitsSurviveSenderForget();
	VerifyQueueCapacityCoalescesSameSender();
	VerifyFullQueueEvictsOldestWithoutMatch();
	return 0;
}
