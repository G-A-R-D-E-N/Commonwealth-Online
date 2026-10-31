#include "F4TWorkshopTransactions.h"

#include <cassert>
#include <limits>
#include <string>

int main()
{
	using namespace F4T::Workshop;

	const FormKey commonwealth{ "Fallout4.esm", 1 };
	const BuildZonePolicy policy{ 7, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 1 };
	SettlementState state{ policy, 100 };
	assert(state.PolicyValid());

	Request place;
	place.playerId = 10;
	place.profileId = "profile-a";
	place.buildZoneId = 7;
	place.operationId = 1;
	place.kind = OperationKind::Place;
	place.form = { "Fallout4.esm", 0x1234 };
	place.space = commonwealth;
	place.transform = { 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F };

	const auto placed = state.Apply(place);
	assert(placed.status == ApplyStatus::Committed);
	assert(placed.revision == 1);
	assert(placed.objectId == 100);
	assert(!placed.replayed);
	assert(state.ObjectCount() == 1);

	{
		const BuildZonePolicy collisionPolicy{ 8, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 2 };
		SettlementState collisionState{ collisionPolicy, 500 };
		Request first = place;
		first.playerId = 20;
		first.buildZoneId = 8;
		const auto firstResult = collisionState.Apply(first);
		assert(firstResult.status == ApplyStatus::Committed);
		assert(firstResult.objectId == 500);

		Request second = place;
		second.playerId = 21;
		second.profileId = "profile-b";
		second.buildZoneId = 8;
		second.expectedRevision = 1;
		const auto secondResult = collisionState.Apply(second);
		assert(secondResult.status == ApplyStatus::Committed);
		assert(secondResult.objectId == 501);
		assert(!secondResult.replayed);
		assert(collisionState.ObjectCount() == 2);
	}

	const auto placedReplay = state.Apply(place);
	assert(placedReplay.status == ApplyStatus::Committed);
	assert(placedReplay.revision == 1);
	assert(placedReplay.objectId == 100);
	assert(placedReplay.replayed);
	assert(state.ObjectCount() == 1);

	Request conflictingReplay = place;
	conflictingReplay.transform.x = 11.0F;
	const auto conflict = state.Apply(conflictingReplay);
	assert(conflict.status == ApplyStatus::InvalidRequest);
	assert(!conflict.replayed);
	assert(conflict.revision == 1);
	assert(state.ObjectCount() == 1);
	assert(state.Find(100)->transform.x == 10.0F);

	Request overBudget = place;
	overBudget.operationId = 2;
	overBudget.expectedRevision = 1;
	const auto budget = state.Apply(overBudget);
	assert(budget.status == ApplyStatus::BudgetExceeded);
	assert(budget.revision == 1);
	assert(state.ObjectCount() == 1);

	Request staleMove;
	staleMove.playerId = 10;
	staleMove.profileId = "profile-a";
	staleMove.buildZoneId = 7;
	staleMove.operationId = 3;
	staleMove.expectedRevision = 0;
	staleMove.kind = OperationKind::Move;
	staleMove.objectId = 100;
	staleMove.space = commonwealth;
	staleMove.transform = place.transform;
	staleMove.transform.x = 40.0F;
	const auto stale = state.Apply(staleMove);
	assert(stale.status == ApplyStatus::StaleRevision);
	assert(stale.revision == 1);
	assert(state.Find(100)->transform.x == 10.0F);

	Request outsideMove = staleMove;
	outsideMove.operationId = 4;
	outsideMove.expectedRevision = 1;
	outsideMove.transform.x = 101.0F;
	outsideMove.transform.y = 0.0F;
	outsideMove.transform.z = 0.0F;
	const auto outside = state.Apply(outsideMove);
	assert(outside.status == ApplyStatus::OutsideBuildZone);
	assert(outside.revision == 1);
	assert(state.Find(100)->transform.x == 10.0F);

	Request wrongSpaceMove = staleMove;
	wrongSpaceMove.operationId = 5;
	wrongSpaceMove.expectedRevision = 1;
	wrongSpaceMove.space = { "Fallout4.esm", 2 };
	const auto wrongSpace = state.Apply(wrongSpaceMove);
	assert(wrongSpace.status == ApplyStatus::OutsideBuildZone);
	assert(wrongSpace.revision == 1);

	Request move = staleMove;
	move.operationId = 6;
	move.expectedRevision = 1;
	const auto moved = state.Apply(move);
	assert(moved.status == ApplyStatus::Committed);
	assert(moved.revision == 2);
	assert(state.Find(100)->transform.x == 40.0F);

	Request missingMove = move;
	missingMove.operationId = 7;
	missingMove.expectedRevision = 2;
	missingMove.objectId = 999;
	const auto missing = state.Apply(missingMove);
	assert(missing.status == ApplyStatus::ObjectNotFound);
	assert(missing.revision == 2);

	Request invalidPlace;
	invalidPlace.playerId = 10;
	invalidPlace.profileId = "profile-a";
	invalidPlace.buildZoneId = 7;
	invalidPlace.operationId = 8;
	invalidPlace.expectedRevision = 2;
	invalidPlace.kind = OperationKind::Place;
	invalidPlace.form = { "Fallout4.esm", 0x5678 };
	invalidPlace.space = commonwealth;
	invalidPlace.transform.scale = std::numeric_limits<float>::infinity();
	const auto invalid = state.Apply(invalidPlace);
	assert(invalid.status == ApplyStatus::InvalidRequest);
	assert(invalid.revision == 2);
	assert(state.ObjectCount() == 1);

	Request overScale = invalidPlace;
	overScale.operationId = 9;
	overScale.transform = place.transform;
	overScale.transform.scale = 2.1F;
	const auto scaleRejected = state.Apply(overScale);
	assert(scaleRejected.status == ApplyStatus::OutsideBuildZone);
	assert(scaleRejected.revision == 2);

	Request longPlugin = invalidPlace;
	longPlugin.operationId = 10;
	longPlugin.form.plugin = std::string(MaximumPluginNameBytes + 1, 'x');
	longPlugin.transform = place.transform;
	const auto longPluginRejected = state.Apply(longPlugin);
	assert(longPluginRejected.status == ApplyStatus::InvalidRequest);
	assert(longPluginRejected.revision == 2);

	Request scrap;
	scrap.playerId = 10;
	scrap.profileId = "profile-a";
	scrap.buildZoneId = 7;
	scrap.operationId = 11;
	scrap.expectedRevision = 2;
	scrap.kind = OperationKind::Scrap;
	scrap.objectId = 100;
	const auto scrapped = state.Apply(scrap);
	assert(scrapped.status == ApplyStatus::Committed);
	assert(scrapped.revision == 3);
	assert(state.ObjectCount() == 0);
	assert(state.Find(100) == nullptr);

	const auto scrapReplay = state.Apply(scrap);
	assert(scrapReplay.status == ApplyStatus::Committed);
	assert(scrapReplay.revision == 3);
	assert(scrapReplay.objectId == 100);
	assert(scrapReplay.replayed);
	assert(state.ObjectCount() == 0);

	Request invalidPlayerId = place;
	invalidPlayerId.playerId = 0;
	invalidPlayerId.operationId = 12;
	invalidPlayerId.expectedRevision = 3;
	const auto zeroPlayer = state.Apply(invalidPlayerId);
	assert(zeroPlayer.status == ApplyStatus::InvalidRequest);
	assert(state.CurrentRevision() == 3);

	Request invalidOperationId = place;
	invalidOperationId.operationId = 0;
	invalidOperationId.expectedRevision = 3;
	const auto zeroOperation = state.Apply(invalidOperationId);
	assert(zeroOperation.status == ApplyStatus::InvalidRequest);
	assert(state.CurrentRevision() == 3);

	const BuildZonePolicy badPolicy{ 0, {}, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0 };
	SettlementState badState{ badPolicy };
	assert(!badState.PolicyValid());
	Request badPolicyRequest = place;
	const auto badPolicyResult = badState.Apply(badPolicyRequest);
	assert(badPolicyResult.status == ApplyStatus::InvalidRequest);
	assert(badState.ObjectCount() == 0);

	SettlementState owned{ policy, 900 };
	Request ownedPlace = place;
	ownedPlace.operationId = 20;
	const auto ownedPlaced = owned.Apply(ownedPlace);
	assert(ownedPlaced.status == ApplyStatus::Committed);
	assert(ownedPlaced.objectId == 900);

	const WorkshopAuthority::CanBuildAt denyForeign = [](const std::string&, BuildZoneId) { return false; };

	Request foreignScrap;
	foreignScrap.playerId = 11;
	foreignScrap.profileId = "profile-b";
	foreignScrap.buildZoneId = 7;
	foreignScrap.operationId = 21;
	foreignScrap.expectedRevision = 1;
	foreignScrap.kind = OperationKind::Scrap;
	foreignScrap.objectId = 900;
	const auto foreignScrapped = owned.Apply(foreignScrap, denyForeign);
	assert(foreignScrapped.status == ApplyStatus::NotAuthorized);
	assert(foreignScrapped.revision == 1);
	assert(owned.ObjectCount() == 1);

	Request foreignMove = foreignScrap;
	foreignMove.operationId = 22;
	foreignMove.kind = OperationKind::Move;
	foreignMove.space = commonwealth;
	foreignMove.transform = place.transform;
	foreignMove.transform.x = 40.0F;
	const auto foreignMoved = owned.Apply(foreignMove, denyForeign);
	assert(foreignMoved.status == ApplyStatus::NotAuthorized);
	assert(foreignMoved.revision == 1);
	assert(owned.Find(900)->transform.x == 10.0F);

	Request reconnectedScrap = ownedPlace;
	reconnectedScrap.playerId = 12;
	reconnectedScrap.operationId = 23;
	reconnectedScrap.expectedRevision = 1;
	reconnectedScrap.kind = OperationKind::Scrap;
	reconnectedScrap.objectId = 900;
	const auto reconnectedScrapped = owned.Apply(reconnectedScrap);
	assert(reconnectedScrapped.status == ApplyStatus::Committed);
	assert(reconnectedScrapped.revision == 2);
	assert(owned.ObjectCount() == 0);

	Request anonymousPlace = ownedPlace;
	anonymousPlace.profileId.clear();
	anonymousPlace.operationId = 24;
	anonymousPlace.expectedRevision = 2;
	const auto anonymous = owned.Apply(anonymousPlace);
	assert(anonymous.status == ApplyStatus::InvalidRequest);
	assert(owned.CurrentRevision() == 2);
	assert(owned.ObjectCount() == 0);

	const BuildZonePolicy twoSlots{ 7, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 2 };
	SettlementState reconnectZone{ twoSlots, 950 };
	Request firstPlace = place;
	firstPlace.operationId = 30;
	const auto firstCommit = reconnectZone.Apply(firstPlace);
	assert(firstCommit.status == ApplyStatus::Committed);
	assert(firstCommit.revision == 1);
	assert(firstCommit.objectId == 950);

	Request reconnectedPlace = firstPlace;
	reconnectedPlace.playerId = 13;
	const auto reconnectReplay = reconnectZone.Apply(reconnectedPlace);
	assert(reconnectReplay.status == ApplyStatus::Committed);
	assert(reconnectReplay.replayed);
	assert(reconnectReplay.revision == 1);
	assert(reconnectReplay.objectId == 950);
	assert(reconnectZone.ObjectCount() == 1);

	Request reconnectedConflict = firstPlace;
	reconnectedConflict.playerId = 13;
	reconnectedConflict.transform.x = 11.0F;
	const auto reconnectConflict = reconnectZone.Apply(reconnectedConflict);
	assert(reconnectConflict.status == ApplyStatus::InvalidRequest);
	assert(!reconnectConflict.replayed);
	assert(reconnectConflict.revision == 1);
	assert(reconnectZone.ObjectCount() == 1);
	assert(reconnectZone.Find(950)->transform.x == 10.0F);

	Request otherProfilePlace = firstPlace;
	otherProfilePlace.playerId = 14;
	otherProfilePlace.profileId = "profile-b";
	otherProfilePlace.expectedRevision = 1;
	const auto otherProfile = reconnectZone.Apply(otherProfilePlace);
	assert(otherProfile.status == ApplyStatus::Committed);
	assert(!otherProfile.replayed);
	assert(otherProfile.revision == 2);
	assert(otherProfile.objectId == 951);
	assert(reconnectZone.ObjectCount() == 2);

	{
		SettlementState floodZone{ policy, 1200 };
		Request committedA = place;
		committedA.operationId = 40;
		const auto committed = floodZone.Apply(committedA);
		assert(committed.status == ApplyStatus::Committed);
		assert(committed.objectId == 1200);

		Request rejectedB = place;
		rejectedB.playerId = 14;
		rejectedB.profileId = "profile-b";
		rejectedB.kind = OperationKind::Move;
		rejectedB.objectId = 999;
		rejectedB.expectedRevision = 1;
		for (OperationId operation = 1000; operation <= 1000 + MaximumRememberedOperations; ++operation) {
			rejectedB.operationId = operation;
			assert(floodZone.Apply(rejectedB).status == ApplyStatus::ObjectNotFound);
		}
		rejectedB.operationId = 1000;
		const auto resent = floodZone.Apply(rejectedB);
		assert(resent.status == ApplyStatus::ObjectNotFound);
		assert(!resent.replayed);

		Request replayA = committedA;
		replayA.playerId = 13;
		const auto replay = floodZone.Apply(replayA);
		assert(replay.status == ApplyStatus::Committed);
		assert(replay.replayed);
		assert(replay.revision == 1);
		assert(replay.objectId == 1200);
		assert(floodZone.ObjectCount() == 1);
	}

	{
		SettlementState shared{ policy, 1300 };
		Request built;
		built.playerId = 30;
		built.profileId = "profile-c";
		built.buildZoneId = 7;
		built.operationId = 1;
		built.kind = OperationKind::Place;
		built.form = { "Fallout4.esm", 0x1234 };
		built.space = commonwealth;
		built.transform = { 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F };
		const auto placed = shared.Apply(built);
		assert(placed.status == ApplyStatus::Committed);
		assert(placed.objectId == 1300);

		Request otherMove = built;
		otherMove.playerId = 31;
		otherMove.profileId = "profile-d";
		otherMove.operationId = 2;
		otherMove.expectedRevision = 1;
		otherMove.kind = OperationKind::Move;
		otherMove.objectId = 1300;
		otherMove.transform.x = 15.0F;
		const auto moved = shared.Apply(otherMove);
		assert(moved.status == ApplyStatus::Committed);
		assert(moved.revision == 2);
		assert(shared.Find(1300)->transform.x == 15.0F);

		Request otherScrap = otherMove;
		otherScrap.operationId = 3;
		otherScrap.expectedRevision = 2;
		otherScrap.kind = OperationKind::Scrap;
		assert(shared.Apply(otherScrap).status == ApplyStatus::Committed);
		assert(shared.ObjectCount() == 0);
	}

	{
		SettlementState world{ policy, 1400 };
		Request tree;
		tree.playerId = 40;
		tree.profileId = "profile-e";
		tree.buildZoneId = 7;
		tree.operationId = 1;
		tree.kind = OperationKind::ScrapWorld;
		tree.form = { "Fallout4.esm", 0x00ABCD };
		tree.space = commonwealth;
		tree.transform = { 5.0F, 5.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		const auto scrapped = world.Apply(tree);
		assert(scrapped.status == ApplyStatus::Committed);
		assert(scrapped.revision == 1);
		assert(scrapped.objectId == 1400);
		assert(scrapped.object && scrapped.object->form == tree.form);
		assert(world.Tombstones().size() == 1);
		assert(world.ObjectCount() == 0);

		Request again = tree;
		again.operationId = 2;
		again.expectedRevision = 1;
		assert(world.Apply(again).status == ApplyStatus::AlreadyRemoved);
		assert(world.Tombstones().size() == 1);

		Request runtime = tree;
		runtime.operationId = 3;
		runtime.expectedRevision = 1;
		runtime.form = { "Fallout4.esm", 0xFF000001 };
		assert(world.Apply(runtime).status == ApplyStatus::InvalidRequest);

		Request outOfRange = tree;
		outOfRange.operationId = 7;
		outOfRange.expectedRevision = 1;
		outOfRange.form = { "Fallout4.esm", 0x01000000 };
		assert(world.Apply(outOfRange).status == ApplyStatus::InvalidRequest);
		outOfRange.operationId = 8;
		outOfRange.form = { "Fallout4.esm", 0x80000001 };
		assert(world.Apply(outOfRange).status == ApplyStatus::InvalidRequest);
		outOfRange.operationId = 9;
		outOfRange.form = { "Fallout4.esm", MaximumLocalFormId };
		assert(world.Apply(outOfRange).status == ApplyStatus::Committed);

		Request far = tree;
		far.operationId = 4;
		far.expectedRevision = 2;
		far.form = { "Fallout4.esm", 0x00ABCE };
		far.transform.x = 500.0F;
		assert(world.Apply(far).status == ApplyStatus::OutsideBuildZone);

		Request large = tree;
		large.operationId = 5;
		large.expectedRevision = 2;
		large.form = { "Fallout4.esm", 0x00ABCF };
		large.transform.scale = 5.0F;
		assert(world.Apply(large).status == ApplyStatus::Committed);
		assert(world.Tombstones().size() == 3);
		assert(world.CurrentRevision() == 3);

		Request denied = tree;
		denied.operationId = 6;
		denied.expectedRevision = 3;
		denied.form = { "Fallout4.esm", 0x00ABD0 };
		assert(world.Apply(denied, denyForeign).status == ApplyStatus::NotAuthorized);
		assert(world.CurrentRevision() == 3);
		assert(world.Tombstones().size() == 3);
	}

	{
		SettlementState capped{ policy, 5000 };
		Request scrap;
		scrap.playerId = 50;
		scrap.profileId = "profile-f";
		scrap.buildZoneId = 7;
		scrap.kind = OperationKind::ScrapWorld;
		scrap.space = commonwealth;
		scrap.transform = { 5.0F, 5.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		for (std::uint32_t index = 1; index <= MaximumWorldTombstones; ++index) {
			scrap.operationId = index;
			scrap.profileId = "profile-" + std::to_string(index / MaximumTombstonesPerProfile);
			scrap.expectedRevision = capped.CurrentRevision();
			scrap.form = { "Fallout4.esm", index };
			assert(capped.Apply(scrap).status == ApplyStatus::Committed);
		}
		assert(capped.Tombstones().size() == MaximumWorldTombstones);
		const auto revision = capped.CurrentRevision();

		Request full = scrap;
		full.operationId = MaximumWorldTombstones + 1;
		full.profileId = "profile-new";
		full.expectedRevision = revision;
		full.form = { "Fallout4.esm", 0x00FFFFFF };
		assert(capped.Apply(full).status == ApplyStatus::BudgetExceeded);
		assert(capped.CurrentRevision() == revision);

		Request duplicate = scrap;
		duplicate.operationId = MaximumWorldTombstones + 2;
		duplicate.profileId = "profile-new";
		duplicate.expectedRevision = revision;
		duplicate.form = { "Fallout4.esm", 1 };
		assert(capped.Apply(duplicate).status == ApplyStatus::AlreadyRemoved);
		assert(capped.Tombstones().size() == MaximumWorldTombstones);
	}

	{
		SettlementState quota{ policy, 1 };
		Request scrap;
		scrap.playerId = 60;
		scrap.profileId = "profile-hog";
		scrap.buildZoneId = 7;
		scrap.kind = OperationKind::ScrapWorld;
		scrap.space = commonwealth;
		scrap.transform = { 5.0F, 5.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		for (std::uint32_t index = 1; index <= MaximumTombstonesPerProfile; ++index) {
			scrap.operationId = index;
			scrap.expectedRevision = quota.CurrentRevision();
			scrap.form = { "Fallout4.esm", index };
			assert(quota.Apply(scrap).status == ApplyStatus::Committed);
		}
		scrap.operationId = MaximumTombstonesPerProfile + 1;
		scrap.expectedRevision = quota.CurrentRevision();
		scrap.form = { "Fallout4.esm", 0x5000 };
		assert(quota.Apply(scrap).status == ApplyStatus::BudgetExceeded);
		assert(quota.Tombstones().size() == MaximumTombstonesPerProfile);

		scrap.profileId = "profile-other";
		scrap.operationId = 1;
		assert(quota.Apply(scrap).status == ApplyStatus::Committed);
		assert(quota.Tombstones().size() == MaximumTombstonesPerProfile + 1);

		SettlementState restored{ policy, 1 };
		assert(restored.Restore(quota.CurrentRevision(), quota.NextObjectId(), quota.Objects(), quota.StoredTombstones()));
		assert(restored.CurrentRevision() == quota.CurrentRevision());
		assert(restored.NextObjectId() == quota.NextObjectId());
		assert(restored.Tombstones().size() == quota.Tombstones().size());
		scrap.profileId = "profile-hog";
		scrap.operationId = 99;
		scrap.expectedRevision = restored.CurrentRevision();
		scrap.form = { "Fallout4.esm", 0x5001 };
		assert(restored.Apply(scrap).status == ApplyStatus::BudgetExceeded);

		auto bad = quota.StoredTombstones();
		bad.front().profileId.clear();
		SettlementState rejected{ policy, 1 };
		assert(!rejected.Restore(1, quota.NextObjectId(), {}, bad));
		assert(rejected.Tombstones().empty() && rejected.CurrentRevision() == 0);
		assert(!rejected.Restore(1, 1, {}, quota.StoredTombstones()));
		bad = quota.StoredTombstones();
		bad.front().state.form.localFormId = 0x01000000;
		assert(!rejected.Restore(1, quota.NextObjectId(), {}, bad));
		assert(rejected.Tombstones().empty());
	}

	return 0;
}
