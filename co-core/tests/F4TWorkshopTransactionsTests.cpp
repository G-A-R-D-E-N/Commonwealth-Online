#include "F4TWorkshopTransactions.h"

#include "F4TWorkshopNoBuild.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <limits>
#include <optional>
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

		const WorkshopAuthority::CanBuildAt denyOutsider = [](const std::string&, BuildZoneId) { return false; };
		Request outsiderMove = otherMove;
		outsiderMove.playerId = 32;
		outsiderMove.profileId = "profile-outsider";
		outsiderMove.operationId = 4;
		outsiderMove.expectedRevision = 2;
		outsiderMove.transform.x = 99.0F;
		assert(shared.Apply(outsiderMove, denyOutsider).status == ApplyStatus::NotAuthorized);
		assert(shared.CurrentRevision() == 2);
		assert(shared.Find(1300)->transform.x == 15.0F);
		Request outsiderScrap = outsiderMove;
		outsiderScrap.operationId = 5;
		outsiderScrap.kind = OperationKind::Scrap;
		assert(shared.Apply(outsiderScrap, denyOutsider).status == ApplyStatus::NotAuthorized);
		assert(shared.ObjectCount() == 1);

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
		SettlementState skipped{ policy, 1 };
		assert(skipped.Restore(1, quota.NextObjectId(), {}, bad));
		assert(skipped.Tombstones().size() == quota.Tombstones().size() - 1);
		SettlementState rejected{ policy, 1 };
		assert(!rejected.Restore(1, 1, {}, quota.StoredTombstones()));
		bad = quota.StoredTombstones();
		bad.front().state.form.localFormId = 0x01000000;
		assert(!rejected.Restore(1, quota.NextObjectId(), {}, bad));
		assert(rejected.Tombstones().empty());
	}

	{
		ObjectState damaged;
		damaged.objectId = 5;
		damaged.buildZoneId = 7;
		damaged.form = { "Fallout4.esm", 0x1234 };
		damaged.space = commonwealth;
		damaged.transform = { 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F };
		damaged.health = 20.0F;
		damaged.maxHealth = 100.0F;
		SettlementState repaired{ policy, 1 };
		assert(repaired.Restore(1, 6, { damaged }, {}));
		assert(repaired.RepairObject(5, 1000.0F, 100));
		assert(repaired.Objects().front().health == 45.0F);
		assert(repaired.RepairObject(5, 10.0F, 100));
		assert(repaired.Objects().front().health == 55.0F);
	}

	{
		const BuildZonePolicy wide{ 9, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 600 };
		std::vector<ObjectState> objects;
		for (std::uint32_t index = 1; index <= 300; ++index) {
			ObjectState object;
			object.objectId = index;
			object.buildZoneId = 9;
			object.form = { "Fallout4.esm", 0x1234 };
			object.space = commonwealth;
			object.transform = { static_cast<float>(index % 20), static_cast<float>(index / 20), 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
			object.placedBy = index % 2 == 0 ? "profile-even" : "profile-odd";
			object.health = 100.0F;
			object.maxHealth = 100.0F;
			objects.push_back(object);
		}
		SettlementState cull{ wide, 1 };
		const bool restoredObjects = cull.Restore(1, 301, objects, {});
		const bool relocated = cull.RelocateCenter(10000.0F, 0.0F, 0.0F);
		const auto culled = cull.CullOutsideRadius(1000);
		const auto culledTombstones = cull.Tombstones().size();
		SettlementState copy{ cull.Policy(), 1 };
		const bool copyRestored = copy.Restore(cull.CurrentRevision(), cull.NextObjectId(), cull.Objects(), cull.StoredTombstones());
		assert(restoredObjects && relocated && copyRestored);
		assert(culled.size() == 300 && cull.ObjectCount() == 0);
		assert(culledTombstones == 300 && copy.Tombstones().size() == 300);
		assert(copy.NextObjectId() == cull.NextObjectId() && copy.CurrentRevision() == cull.CurrentRevision());

		std::vector<StoredTombstone> trimmed;
		for (std::uint32_t index = 1; index <= 300; ++index) {
			ObjectState object;
			object.objectId = index;
			object.buildZoneId = 9;
			object.form = { "Fallout4.esm", index };
			object.space = commonwealth;
			object.transform = { 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
			object.destroyedUnixSeconds = index;
			trimmed.push_back({ object, "profile-trim" });
		}
		SettlementState trim{ wide, 1 };
		const bool trimRestored = trim.Restore(1, 301, {}, trimmed);
		std::uint64_t oldestKept = std::numeric_limits<std::uint64_t>::max();
		for (const auto& tombstone : trim.Tombstones()) {
			if (tombstone.destroyedUnixSeconds < oldestKept) {
				oldestKept = tombstone.destroyedUnixSeconds;
			}
		}
		assert(trimRestored);
		assert(trim.Tombstones().size() == MaximumTombstonesPerProfile);
		assert(oldestKept == 45);

		std::vector<StoredTombstone> legacy;
		for (std::uint32_t index = 1; index <= MaximumTombstonesPerProfile + 1; ++index) {
			ObjectState object;
			object.objectId = index;
			object.buildZoneId = 9;
			object.form = { "Fallout4.esm", index };
			object.space = commonwealth;
			object.transform = { 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
			object.destroyedUnixSeconds = index == MaximumTombstonesPerProfile + 1 ? 5 : 0;
			legacy.push_back({ object, "profile-legacy" });
		}
		SettlementState zeroTime{ wide, 1 };
		const bool legacyRestored = zeroTime.Restore(1, MaximumTombstonesPerProfile + 2, {}, legacy);
		assert(legacyRestored);
		assert(zeroTime.Tombstones().size() == MaximumTombstonesPerProfile);
	}

	{
		const BuildZonePolicy legacyZone{ 10, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 600 };
		ObjectState unowned;
		unowned.objectId = 1;
		unowned.buildZoneId = 10;
		unowned.form = { "Fallout4.esm", 0x1234 };
		unowned.space = commonwealth;
		unowned.transform = { 500.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		ObjectState tracked = unowned;
		tracked.objectId = 2;
		tracked.form = { "Fallout4.esm", 0x1235 };
		tracked.placedBy = "profile-a";
		SettlementState zone{ legacyZone, 3 };
		assert(zone.Restore(1, 3, { unowned, tracked }, {}));
		assert(zone.CullOutsideRadius(100).size() == 2);
		assert(zone.ObjectCount() == 0 && zone.Tombstones().size() == 1);
		SettlementState copy{ zone.Policy(), 1 };
		assert(copy.Restore(zone.CurrentRevision(), zone.NextObjectId(), zone.Objects(), zone.StoredTombstones()));
		assert(copy.Tombstones().size() == 1 && copy.StoredTombstones().front().profileId == "profile-a");
	}

	{
		const BuildZonePolicy cascade{ 11, commonwealth, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 600 };
		ObjectState anchor;
		anchor.objectId = 1;
		anchor.buildZoneId = 11;
		anchor.form = { "Fallout4.esm", 0x1234 };
		anchor.space = commonwealth;
		anchor.transform = { 500.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		anchor.foundation = true;
		ObjectState resting = anchor;
		resting.objectId = 2;
		resting.foundation = false;
		resting.transform = { 50.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		resting.supportIds = { 1 };
		SettlementState zone{ cascade, 3 };
		assert(zone.Restore(1, 3, { anchor, resting }, {}));
		const auto culled = zone.CullOutsideRadius(100);
		assert(culled.size() == 2 && zone.ObjectCount() == 0);
	}

	{
		const BuildZonePolicy level{ 12, commonwealth, 0.0F, 0.0F, 0.0F, 1000.0F, 2.0F, 600 };
		SettlementState zone{ level, 1 };
		Request place;
		place.playerId = 10;
		place.profileId = "profile-a";
		place.buildZoneId = 12;
		place.kind = OperationKind::Place;
		place.form = { "Fallout4.esm", 0x1234 };
		place.space = commonwealth;
		place.transform = { 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		place.operationId = 1;
		place.expectedRevision = zone.CurrentRevision();
		const auto base = zone.Apply(place);
		assert(base.status == ApplyStatus::Committed);
		place.operationId = 2;
		place.expectedRevision = zone.CurrentRevision();
		place.transform.z = 1200.0F;
		assert(zone.Apply(place).status == ApplyStatus::OutsideBuildZone);
		place.operationId = 3;
		place.expectedRevision = zone.CurrentRevision();
		place.transform.z = 0.0F;
		place.supportIds = { 99 };
		assert(zone.Apply(place).status == ApplyStatus::InvalidRequest);
		place.operationId = 4;
		place.expectedRevision = zone.CurrentRevision();
		place.supportIds = { base.objectId };
		const auto raised = zone.Apply(place);
		assert(raised.status == ApplyStatus::Committed);
		place.operationId = 5;
		place.expectedRevision = zone.CurrentRevision();
		place.supportIds.clear();
		place.transform.z = 600.0F;
		assert(zone.Apply(place).status == ApplyStatus::Committed);
		assert(zone.ObjectCount() == 3);
		place.operationId = 6;
		place.expectedRevision = zone.CurrentRevision();
		place.supportIds = { base.objectId };
		place.transform.z = 1200.0F;
		assert(zone.Apply(place).status == ApplyStatus::OutsideBuildZone);
		assert(zone.ObjectCount() == 3);

		Request move;
		move.playerId = 10;
		move.profileId = "profile-a";
		move.buildZoneId = 12;
		move.kind = OperationKind::Move;
		move.space = commonwealth;
		move.objectId = base.objectId;
		move.transform = { 10.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		move.operationId = 7;
		move.expectedRevision = zone.CurrentRevision();
		assert(zone.Apply(move).status == ApplyStatus::InvalidRequest);
		move.objectId = raised.objectId;
		move.operationId = 8;
		move.expectedRevision = zone.CurrentRevision();
		move.transform = { 10.0F, 0.0F, 600.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		assert(zone.Apply(move).status == ApplyStatus::Committed);
		move.operationId = 9;
		move.expectedRevision = zone.CurrentRevision();
		move.transform = { 10.0F, 0.0F, 1200.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		assert(zone.Apply(move).status == ApplyStatus::OutsideBuildZone);
	}

	{
		const BuildZonePolicy sample{ 1, { "Fallout4.esm", 60 }, -76967.41F, 67777.91F, 7529.0F, 6946.84F, 2.0F, 2048 };
		SettlementState zone{ sample, 1 };
		Request request;
		request.playerId = 3;
		request.profileId = "profile-sample";
		request.buildZoneId = 1;
		request.kind = OperationKind::Place;
		request.form = { "Fallout4.esm", 0x1234 };
		request.transform = { -76967.41F, 67777.91F, 7529.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		request.maxHealth = 300.0F;
		request.operationId = 1;
		request.space = { "Fallout4.esm", 60 };
		assert(zone.Apply(request).status == ApplyStatus::Committed);
		request.operationId = 2;
		request.expectedRevision = 1;
		request.space = { "fallout4.esm", 60 };
		assert(zone.Apply(request).status == ApplyStatus::Committed);
		request.operationId = 3;
		request.expectedRevision = 2;
		request.space = { "Other.esm", 60 };
		assert(zone.Apply(request).status == ApplyStatus::OutsideBuildZone);
		request.operationId = 4;
		request.space = { "fallout4.esm", 61 };
		assert(zone.Apply(request).status == ApplyStatus::OutsideBuildZone);
		assert(zone.CurrentRevision() == 2 && zone.ObjectCount() == 2);
	}

	{
		SettlementState zone{ BuildZonePolicy{ 7, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 4 }, 900 };
		Request first;
		first.playerId = 1;
		first.profileId = "profile-a";
		first.buildZoneId = 7;
		first.kind = OperationKind::Place;
		first.form = { "Fallout4.esm", 0x10 };
		first.space = { "Fallout4.esm", 1 };
		first.transform = { 1.0F, 2.0F, 3.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		first.operationId = 1;
		assert(zone.Apply(first).status == ApplyStatus::Committed);
		Request second = first;
		second.operationId = 2;
		second.expectedRevision = 0;
		const auto stale = zone.Apply(second);
		assert(stale.status == ApplyStatus::StaleRevision && stale.revision == 1);
		Request retry = second;
		retry.operationId = 3;
		retry.expectedRevision = stale.revision;
		const auto committed = zone.Apply(retry);
		assert(committed.status == ApplyStatus::Committed && committed.revision == 2);
		assert(zone.ObjectCount() == 2);
	}

	{
		const auto root = nlohmann::json::parse(R"({"regions":[{"space":{"plugin":"Fallout4.esm","localFormId":60},"minX":-28000,"minY":-18000,"maxX":-22000,"maxY":-12000}]})");
		F4T::WorkshopNoBuild::Table table;
		assert(table.LoadFromJson(root));
		const std::optional<FormKey> noCell;
		assert(table.Contains({ "Fallout4.esm", 60 }, -25000.0F, -15000.0F, 0.0F, noCell, noCell));
		assert(table.Contains({ "fallout4.esm", 60 }, -25000.0F, -15000.0F, 0.0F, noCell, noCell));
		assert(!table.Contains({ "fallout4.esm", 61 }, -25000.0F, -15000.0F, 0.0F, noCell, noCell));
		assert(!table.Contains({ "fallout4.esm", 60 }, 0.0F, 0.0F, 0.0F, noCell, noCell));
		for (const double hostile : { 1e300, -1e300, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			auto hostileRoot = root;
			hostileRoot["regions"][0]["minX"] = hostile;
			F4T::WorkshopNoBuild::Table rejected;
			assert(!rejected.LoadFromJson(hostileRoot));
		}
	}

	{
		const BuildZonePolicy campPolicy{ 9, { "fallout4.esm", 60 }, -25000.0F, -15000.0F, 0.0F, 100.0F, 1.0F, 4 };
		SettlementState camp{ campPolicy, 1 };
		Request build;
		build.playerId = 4;
		build.profileId = "profile-camp";
		build.buildZoneId = 9;
		build.kind = OperationKind::Place;
		build.form = { "Fallout4.esm", 0x20 };
		build.space = { "Fallout4.esm", 60 };
		build.transform = { -25000.0F, -15000.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		build.operationId = 1;
		assert(camp.Apply(build).status == ApplyStatus::Committed);
	}

	{
		const BuildZonePolicy scrapPolicy{ 10, { "Fallout4.esm", 60 }, -25000.0F, -15000.0F, 0.0F, 100.0F, 1.0F, 4 };
		SettlementState scrapWorld{ scrapPolicy, 1500 };
		Request tree;
		tree.playerId = 5;
		tree.profileId = "profile-scrap";
		tree.buildZoneId = 10;
		tree.operationId = 1;
		tree.kind = OperationKind::ScrapWorld;
		tree.form = { "Fallout4.esm", 0x00ABCD };
		tree.space = { "Fallout4.esm", 60 };
		tree.transform = { -25000.0F, -15000.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		assert(scrapWorld.Apply(tree).status == ApplyStatus::Committed);
		assert(scrapWorld.Tombstones().size() == 1);

		Request recased = tree;
		recased.operationId = 2;
		recased.expectedRevision = 1;
		recased.form = { "fallout4.esm", 0x00ABCD };
		assert(scrapWorld.Apply(recased).status == ApplyStatus::AlreadyRemoved);
		assert(scrapWorld.Tombstones().size() == 1);
	}

	return 0;
}
