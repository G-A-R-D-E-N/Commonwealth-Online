#include "F4TInventoryOperations.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TFormIdentity.h"
#include "F4TInventoryContract.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::InventoryOps;
	using F4T::Drops::EntityId;
	using F4T::FormIdentity::Kind;
	using F4T::InventoryContract::OwnedReason;

	std::string ItemKey()
	{
		const auto key = F4T::InventoryContract::MakeItemKey("Test.esm", 0x123, Kind::Regular);
		assert(key);
		return *key;
	}

	std::string KeyFor(std::uint32_t a_localId)
	{
		const auto key = F4T::InventoryContract::MakeItemKey("Test.esm", a_localId, Kind::Regular);
		assert(key);
		return *key;
	}

	OwnedRequest Drop(std::uint32_t a_count)
	{
		OwnedRequest request;
		request.kind = Operation::Drop;
		request.itemKey = ItemKey();
		request.count = a_count;
		return request;
	}

	OwnedRequest Pickup(EntityId a_entityId)
	{
		OwnedRequest request;
		request.kind = Operation::Pickup;
		request.entityId = a_entityId;
		return request;
	}

	OwnedResult Committed(const std::string& a_operationId, std::uint32_t a_revision, EntityId a_entityId = F4T::Drops::kFirstDroppedEntityId)
	{
		OwnedResult result;
		result.operationId = a_operationId;
		result.committed = true;
		result.revision = a_revision;
		result.entityId = a_entityId;
		return result;
	}

	OwnedResult Rejected(const std::string& a_operationId, OwnedReason a_reason, std::optional<std::uint32_t> a_revision = std::nullopt)
	{
		OwnedResult result;
		result.operationId = a_operationId;
		result.reason = a_reason;
		result.revision = a_revision;
		return result;
	}

	DropContext Context(std::uint32_t a_count = 1)
	{
		DropContext context;
		context.itemKey = ItemKey();
		context.count = a_count;
		return context;
	}

	void TestPendingSendAndIdenticalResend()
	{
		PendingOwnedOps ops{ "session" };
		ops.SetExpectedRevision(4);
		const auto operationId = ops.Begin(Drop(5));
		assert(operationId && *operationId == "session:1");
		assert(ops.PendingCount() == 1 && !ops.InFlight());
		const auto sent = ops.Poll(0);
		assert(sent.action == PollAction::Send && sent.operationId == *operationId && sent.kind == Operation::Drop);
		assert(sent.request.at("type") == "inventoryDropRequest");
		assert(sent.request.at("expectedRevision") == 4U && sent.request.at("count") == 5U && sent.request.at("itemKey") == ItemKey());
		assert(ops.InFlight());
		assert(ops.Poll(0).action == PollAction::Idle);
		assert(ops.Poll(1999).action == PollAction::Idle);
		const auto first = ops.Poll(2000);
		assert(first.action == PollAction::Resend && first.operationId == *operationId);
		assert(sent.request.dump() == first.request.dump());
		assert(ops.Poll(4999).action == PollAction::Idle);
		const auto second = ops.Poll(5000);
		assert(second.action == PollAction::Resend);
		assert(sent.request.dump() == second.request.dump());
		assert(ops.Poll(9999).action == PollAction::Idle);
		const auto failed = ops.Poll(10000);
		assert(failed.action == PollAction::Fail && failed.operationId == *operationId);
		assert(ops.PendingCount() == 0 && !ops.InFlight());
		assert(ops.Poll(20000).action == PollAction::Idle);
	}

	void TestNoResendAfterResolve()
	{
		PendingOwnedOps ops{ "session" };
		const auto operationId = ops.Begin(Drop(1));
		assert(operationId);
		assert(ops.Poll(0).action == PollAction::Send);
		assert(ops.Resolve(Committed(*operationId, 3), 100) == ResolveOutcome::Commit);
		assert(ops.ExpectedRevision() == 3 && ops.PendingCount() == 0 && !ops.InFlight());
		assert(ops.Poll(2000).action == PollAction::Idle && ops.Poll(5000).action == PollAction::Idle);
	}

	void TestStaleResyncThenRetry()
	{
		PendingOwnedOps ops{ "session" };
		assert(ops.ExpectedRevision() == 0);
		const auto operationId = ops.Begin(Drop(2));
		assert(operationId);
		const auto sent = ops.Poll(0);
		assert(sent.action == PollAction::Send && sent.request.at("expectedRevision") == 0U);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 7), 10) == ResolveOutcome::StaleResync);
		assert(ops.ExpectedRevision() == 7 && ops.PendingCount() == 1 && !ops.InFlight());
		const auto retried = ops.Poll(11);
		assert(retried.action == PollAction::Send && retried.operationId == *operationId && retried.request.at("expectedRevision") == 7U);
		assert(retried.request.dump() != sent.request.dump());
		assert(ops.Resolve(Committed(*operationId, 8), 20) == ResolveOutcome::Commit);
		assert(ops.ExpectedRevision() == 8 && ops.PendingCount() == 0);
	}

	void TestNinthOperationRefused()
	{
		PendingOwnedOps ops{ "session" };
		assert(!ops.Begin(Drop(0)) && !ops.Begin(Drop(65536)));
		assert(!ops.Begin(Pickup(F4T::Drops::kFirstDroppedEntityId - 1U)));
		OwnedRequest unknown;
		unknown.kind = static_cast<Operation>(9);
		unknown.itemKey = ItemKey();
		unknown.count = 1;
		unknown.entityId = F4T::Drops::kFirstDroppedEntityId;
		assert(!ops.Begin(unknown) && ops.PendingCount() == 0);
		PendingOwnedOps noNonce{ "" };
		assert(!noNonce.Begin(Drop(1)) && noNonce.PendingCount() == 0);
		std::vector<std::string> ids;
		for (std::uint32_t index = 0; index < kMaximumPendingOperations; ++index) {
			const auto id = ops.Begin(Drop(index + 1));
			assert(id && id->size() <= F4T::Drops::kMaximumOperationIdChars);
			assert(std::find(ids.begin(), ids.end(), *id) == ids.end());
			ids.push_back(*id);
		}
		assert(ops.PendingCount() == kMaximumPendingOperations);
		assert(!ops.Begin(Drop(1)));
		assert(ops.Poll(0).action == PollAction::Send);
		assert(ops.Resolve(Committed(ids.front(), 1), 1) == ResolveOutcome::Commit);
		const auto next = ops.Begin(Drop(1));
		assert(next && *next != ids.front() && ops.PendingCount() == kMaximumPendingOperations);
	}

	void TestResetFailsClosed()
	{
		PendingOwnedOps ops{ "session" };
		std::vector<std::string> ids;
		for (std::uint32_t index = 0; index < 3; ++index) {
			const auto id = ops.Begin(Drop(1));
			assert(id);
			ids.push_back(*id);
		}
		ops.SetExpectedRevision(5);
		assert(ops.Poll(0).action == PollAction::Send && ops.Poll(2000).action == PollAction::Resend);
		assert(ops.Reset() == ids);
		assert(ops.PendingCount() == 0 && !ops.InFlight() && ops.ExpectedRevision() == 5);
		assert(ops.Poll(20000).action == PollAction::Idle && ops.Poll(50000).action == PollAction::Idle);
		LateCommitAction late;
		assert(ops.Resolve(Committed(ids.front(), 6), 1, &late) == ResolveOutcome::LateCommit);
		assert(late.kind == Operation::Drop && late.itemKey == ItemKey() && late.count == 1U);
		assert(ops.ExpectedRevision() == 6);
	}

	void TestDuplicateResultIgnored()
	{
		PendingOwnedOps ops{ "session" };
		const auto first = ops.Begin(Drop(1));
		const auto second = ops.Begin(Drop(1));
		assert(first && second);
		assert(ops.Poll(0).action == PollAction::Send);
		const auto committed = Committed(*first, 1);
		assert(ops.Resolve(committed, 1) == ResolveOutcome::Commit);
		assert(ops.ExpectedRevision() == 1);
		assert(ops.Resolve(committed, 2) == ResolveOutcome::Duplicate);
		assert(ops.ExpectedRevision() == 1 && ops.PendingCount() == 1);
		assert(ops.Resolve(Committed("other:9", 4), 3) == ResolveOutcome::Unknown);
		assert(ops.Resolve(Rejected(*second, OwnedReason::InsufficientItems), 4) == ResolveOutcome::Unknown);
		assert(ops.Resolve(Rejected(*second, OwnedReason::StaleRevision, 9), 5) == ResolveOutcome::Unknown);
		assert(ops.ExpectedRevision() == 1 && ops.PendingCount() == 1 && !ops.InFlight());
	}

	void TestRateLimitedRequeueKeepsOperationQueued()
	{
		PendingOwnedOps ops{ "session" };
		ops.SetExpectedRevision(2);
		const auto operationId = ops.Begin(Drop(1));
		assert(operationId);
		assert(ops.Poll(0).action == PollAction::Send);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::RateLimited), 100) == ResolveOutcome::Requeued);
		assert(ops.ExpectedRevision() == 2 && ops.PendingCount() == 1 && !ops.InFlight());
		assert(ops.Poll(349).action == PollAction::Idle);
		const auto retried = ops.Poll(350);
		assert(retried.action == PollAction::Send && retried.operationId == *operationId && retried.request.at("expectedRevision") == 2U);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::RateLimited), 400) == ResolveOutcome::Requeued);
		assert(ops.ExpectedRevision() == 2 && ops.PendingCount() == 1);
		assert(ops.Poll(649).action == PollAction::Idle);
		assert(ops.Poll(650).action == PollAction::Send);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::InsufficientItems), 651) == ResolveOutcome::Reject);
		assert(ops.ExpectedRevision() == 2 && ops.PendingCount() == 0);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::RateLimited), 652) == ResolveOutcome::Duplicate);
	}

	void TestPickupOperation()
	{
		PendingOwnedOps ops{ "session" };
		ops.SetExpectedRevision(6);
		const auto operationId = ops.Begin(Pickup(F4T::Drops::kFirstDroppedEntityId));
		assert(operationId);
		const auto sent = ops.Poll(0);
		assert(sent.action == PollAction::Send && sent.kind == Operation::Pickup);
		assert(sent.request.at("type") == "inventoryPickupRequest" && sent.request.at("entityId") == F4T::Drops::kFirstDroppedEntityId);
		assert(sent.request.at("expectedRevision") == 6U && !sent.request.contains("itemKey"));
		assert(ops.Resolve(Committed(*operationId, 7, F4T::Drops::kFirstDroppedEntityId), 1) == ResolveOutcome::Commit);
	}

	void TestReserveBlocksMaterialization()
	{
		DropEntityBindings bindings;
		const EntityId entity = F4T::Drops::kFirstDroppedEntityId;
		assert(bindings.ReserveForLocalOp(entity));
		assert(!bindings.ShouldMaterialize(entity));
		assert(bindings.IsReserved(entity) && !bindings.IsBound(entity));
		assert(bindings.Bind(entity, "ref:0001"));
		assert(bindings.IsBound(entity) && !bindings.IsReserved(entity));
		assert(!bindings.ShouldMaterialize(entity));
		assert(bindings.FindEntity("ref:0001") == std::optional<EntityId>{ entity });
		assert(bindings.TrackedCount() == 1);
		assert(!bindings.Bind(entity, "ref:0002"));
		assert(bindings.FindEntity("ref:0002") == std::nullopt);
		assert(!bindings.Bind(entity + 1U, ""));
		assert(bindings.Bind(entity + 1U, "ref:0002"));
		assert(!bindings.Bind(entity + 2U, "ref:0002"));
		assert(bindings.Unbind(entity));
		assert(bindings.ShouldMaterialize(entity) && !bindings.IsBound(entity) && !bindings.IsReserved(entity));
		assert(bindings.FindEntity("ref:0001") == std::nullopt);
		assert(!bindings.Unbind(entity));
		assert(bindings.ShouldMaterialize(entity + 2U));
	}

	void TestBindingCap()
	{
		DropEntityBindings bindings;
		for (std::uint32_t index = 0; index < kMaximumBoundEntities; ++index) {
			assert(bindings.ReserveForLocalOp(F4T::Drops::kFirstDroppedEntityId + index));
		}
		assert(bindings.TrackedCount() == kMaximumBoundEntities);
		const EntityId overflow = F4T::Drops::kFirstDroppedEntityId + static_cast<EntityId>(kMaximumBoundEntities);
		assert(!bindings.ReserveForLocalOp(overflow));
		assert(bindings.ShouldMaterialize(overflow));
		assert(!bindings.Bind(overflow, "ref:cap"));
		assert(bindings.TrackedCount() == kMaximumBoundEntities);
		assert(bindings.ReserveForLocalOp(F4T::Drops::kFirstDroppedEntityId));
	}

	void TestClassifierTruthTable()
	{
		const auto check = [](const DropContext& a_context, const DropPolicy& a_policy, DropVerdict a_verdict, const char* a_reason) {
			const auto decision = ClassifyDrop(a_context, a_policy);
			assert(decision.verdict == a_verdict && decision.reason == a_reason);
		};
		DropContext runtime = Context();
		runtime.itemKey = std::nullopt;
		check(runtime, {}, DropVerdict::PassThrough, "runtimeForm");
		DropContext empty = Context();
		empty.itemKey = "";
		check(empty, {}, DropVerdict::PassThrough, "runtimeForm");
		DropContext invalid = Context();
		invalid.itemKey = "not-a-key";
		check(invalid, {}, DropVerdict::Block, "invalidItemKey");
		DropContext book = Context();
		book.formKind = FormKind::Book;
		check(book, {}, DropVerdict::PassThrough, "book");
		DropContext note = Context();
		note.formKind = FormKind::Note;
		check(note, {}, DropVerdict::PassThrough, "note");
		DropContext instance = Context();
		instance.hasInstanceData = true;
		check(instance, {}, DropVerdict::Block, "instanceData");
		DropContext bookInstance = book;
		bookInstance.hasInstanceData = true;
		check(bookInstance, {}, DropVerdict::PassThrough, "book");
		DropPolicy allowInstance;
		allowInstance.blockInstanceData = false;
		check(instance, allowInstance, DropVerdict::Gate, "gate");
		DropPolicy allowBooks;
		allowBooks.excludeBooksAndNotes = false;
		check(book, allowBooks, DropVerdict::Gate, "gate");
		DropContext queued = Context();
		queued.origin = DropOrigin::CompanionApp;
		check(queued, {}, DropVerdict::Gate, "gate");
		DropPolicy noQueue;
		noQueue.gateCompanionApp = false;
		check(queued, noQueue, DropVerdict::PassThrough, "companionAppDisabled");
		DropContext none = Context(0);
		check(none, {}, DropVerdict::Block, "countOutOfRange");
		DropContext oversized = Context(65536);
		check(oversized, {}, DropVerdict::Block, "countOutOfRange");
		DropContext scripted = Context();
		scripted.origin = DropOrigin::Scripted;
		check(scripted, {}, DropVerdict::Gate, "gate");
		DropContext exact = Context(5);
		exact.viewCount = 5;
		check(exact, {}, DropVerdict::Gate, "gate");
		DropContext shortView = Context(5);
		shortView.viewCount = 4;
		check(shortView, {}, DropVerdict::Block, "countExceedsView");
		DropContext unknownView = Context(5);
		check(unknownView, {}, DropVerdict::Gate, "gate");
		DropContext zeroView = Context(5);
		zeroView.viewCount = 0;
		check(zeroView, {}, DropVerdict::Block, "countExceedsView");
		DropContext unknownOne = Context(1);
		check(unknownOne, {}, DropVerdict::Gate, "gate");
	}

	void TestPickupClassifier()
	{
		DropEntityBindings bindings;
		const EntityId entity = F4T::Drops::kFirstDroppedEntityId;
		const auto notBound = [](const PickupDecision& a_decision) {
			assert(a_decision.verdict == PickupVerdict::PassThrough && a_decision.reason == "notBound");
		};
		notBound(ClassifyPickup(std::nullopt, bindings));
		notBound(ClassifyPickup(std::string{}, bindings));
		notBound(ClassifyPickup(std::string{ "ref:0001" }, bindings));
		assert(bindings.Bind(entity, "ref:0001"));
		const auto bound = ClassifyPickup(std::string{ "ref:0001" }, bindings);
		assert(bound.verdict == PickupVerdict::Gate && bound.reason == "boundMirror");
		assert(bindings.Unbind(entity));
		notBound(ClassifyPickup(std::string{ "ref:0001" }, bindings));
	}

	void TestSnapshotChunks()
	{
		const auto empty = BuildInventorySnapshot({});
		assert(empty.size() == 1);
		assert(empty.front().at("type") == "inventorySnapshot" && empty.front().at("chunkIndex") == 0U);
		assert(empty.front().at("chunkCount") == 1U && empty.front().at("lastChunk") == true);
		assert(empty.front().at("items").is_object() && empty.front().at("items").empty());

		const std::vector<InventoryCount> single{ { ItemKey(), 3 } };
		const auto one = BuildInventorySnapshot(single);
		assert(one.size() == 1 && one.front().at("items").size() == 1 && one.front().at("items").at(ItemKey()).at("plugin") == "test.esm");
		assert(one.front().at("items").at(ItemKey()).at("localFormId") == "00000123" && one.front().at("items").at(ItemKey()).at("count") == 3U);

		std::vector<InventoryCount> maximum;
		for (std::uint32_t index = 1; index <= kMaximumSnapshotEntriesPerChunk; ++index) maximum.push_back({ KeyFor(index), 1 });
		assert(BuildInventorySnapshot(maximum).size() == 1);
		auto over = maximum;
		over.push_back({ KeyFor(1000), 2 });
		const auto chunked = BuildInventorySnapshot(over);
		assert(chunked.size() == 2);
		assert(chunked[0].at("chunkIndex") == 0U && chunked[0].at("chunkCount") == 2U && chunked[0].at("lastChunk") == false);
		assert(chunked[0].at("items").size() == kMaximumSnapshotEntriesPerChunk);
		assert(chunked[1].at("chunkIndex") == 1U && chunked[1].at("chunkCount") == 2U && chunked[1].at("lastChunk") == true);
		assert(chunked[1].at("items").size() == 1 && chunked[1].at("items").contains(KeyFor(1000)));
		for (const auto& packet : chunked) {
			std::vector<Json> items;
			for (const auto& entry : packet.at("items").items()) {
				assert(F4T::InventoryContract::ParseItemKey(entry.key()));
				items.push_back(entry.value());
			}
			const auto catalog = F4T::DurableState::NormalizeStartingInventory(Json(items));
			assert(catalog.size() == packet.at("items").size());
		}

		const std::vector<InventoryCount> filtered{ { KeyFor(1), 0 }, { "not-a-key", 4 }, { KeyFor(2), 4 }, { KeyFor(2), 7 }, { KeyFor(3), 70000 } };
		const auto filteredPackets = BuildInventorySnapshot(filtered);
		assert(filteredPackets.size() == 1);
		const auto& items = filteredPackets.front().at("items");
		assert(items.size() == 2 && !items.contains("not-a-key"));
		assert(items.at(KeyFor(2)).at("count") == 11U && items.at(KeyFor(3)).at("count") == 65535U);
	}

	void TestDeltaChunksAndBounds()
	{
		assert(BuildInventoryDelta(4, {}).empty());

		std::vector<InventoryChange> changes;
		for (std::uint32_t index = 1; index <= kMaximumDeltaChangesPerChunk + 1; ++index) changes.push_back({ KeyFor(index), 1 });
		const auto chunked = BuildInventoryDelta(4, changes);
		assert(chunked.size() == 2);
		assert(chunked[0].at("type") == "inventoryDelta" && chunked[0].at("seq") == 4U && chunked[0].at("chunkIndex") == 0U);
		assert(chunked[0].at("chunkCount") == 2U && chunked[0].at("lastChunk") == false);
		assert(chunked[0].at("changes").size() == kMaximumDeltaChangesPerChunk);
		assert(chunked[1].at("seq") == 5U && chunked[1].at("chunkIndex") == 1U && chunked[1].at("lastChunk") == true);
		assert(chunked[1].at("changes").size() == 1 && chunked[1].at("changes")[0].at("itemKey") == KeyFor(kMaximumDeltaChangesPerChunk + 1));
		assert(chunked[0].at("changes")[0].at("itemKey") == KeyFor(1) && chunked[0].at("changes")[0].at("count") == 1U);

		const std::vector<InventoryChange> bounded{ { KeyFor(1), 0 }, { "not-a-key", 5 }, { KeyFor(2), 70000 } };
		const auto packet = BuildInventoryDelta(9, bounded);
		assert(packet.size() == 1 && packet.front().at("changes").size() == 2);
		assert(packet.front().at("changes")[0].at("itemKey") == KeyFor(1) && packet.front().at("changes")[0].at("count") == 0U);
		assert(packet.front().at("changes")[1].at("count") == 65535U);
		assert(BuildInventoryDelta(9, std::vector<InventoryChange>{ { "not-a-key", 5 } }).empty());
	}

	void TestDiffSnapshot()
	{
		const CountMap mirror{ { KeyFor(1), 5 }, { KeyFor(2), 3 }, { KeyFor(3), 1 } };
		const std::vector<InventoryCount> engine{ { KeyFor(1), 5 }, { KeyFor(2), 7 }, { KeyFor(4), 2 } };
		const std::vector<InventoryChange> expected{ { KeyFor(2), 7 }, { KeyFor(3), 0 }, { KeyFor(4), 2 } };
		assert(DiffSnapshot(mirror, engine) == expected);
		const std::vector<InventoryChange> removals{ { KeyFor(1), 0 }, { KeyFor(2), 0 }, { KeyFor(3), 0 } };
		assert(DiffSnapshot(mirror, {}) == removals);
		const std::vector<InventoryChange> additions{ { KeyFor(1), 5 }, { KeyFor(2), 7 }, { KeyFor(4), 2 } };
		assert(DiffSnapshot({}, engine) == additions);
		const std::vector<InventoryChange> single{ { KeyFor(3), 0 } };
		assert(DiffSnapshot(mirror, { { KeyFor(1), 5 }, { KeyFor(2), 3 }, { KeyFor(3), 0 } }) == single);
		const std::vector<InventoryChange> unchangedStack{ { KeyFor(2), 0 }, { KeyFor(3), 0 } };
		assert(DiffSnapshot(mirror, { { KeyFor(1), 5 }, { KeyFor(1), 0 } }) == unchangedStack);
		assert(DiffSnapshot({}, {}).empty());
	}

	void TestAbsoluteDeadlineAcrossRearms()
	{
		PendingOwnedOps requeued{ "session" };
		requeued.SetExpectedRevision(3);
		const auto requeuedId = requeued.Begin(Drop(1));
		assert(requeuedId);
		assert(requeued.Poll(0).action == PollAction::Send);
		assert(requeued.Resolve(Rejected(*requeuedId, OwnedReason::RateLimited), 9400) == ResolveOutcome::Requeued);
		assert(requeued.Poll(9649).action == PollAction::Idle);
		const auto resent = requeued.Poll(9650);
		assert(resent.action == PollAction::Send && resent.operationId == *requeuedId);
		assert(requeued.Poll(9999).action == PollAction::Idle);
		const auto expired = requeued.Poll(kOperationTimeoutMs);
		assert(expired.action == PollAction::Fail && expired.operationId == *requeuedId);
		assert(requeued.PendingCount() == 0 && !requeued.InFlight());

		PendingOwnedOps resynced{ "session" };
		const auto resyncedId = resynced.Begin(Drop(1));
		assert(resyncedId);
		assert(resynced.Poll(0).action == PollAction::Send);
		assert(resynced.Resolve(Rejected(*resyncedId, OwnedReason::StaleRevision, 4), 9000) == ResolveOutcome::StaleResync);
		const auto retried = resynced.Poll(9001);
		assert(retried.action == PollAction::Send && retried.request.at("expectedRevision") == 4U);
		const auto resyncedExpired = resynced.Poll(kOperationTimeoutMs);
		assert(resyncedExpired.action == PollAction::Fail && resyncedExpired.operationId == *resyncedId);
		assert(resynced.PendingCount() == 0);
	}

	void TestStaleResyncCapIsTerminal()
	{
		PendingOwnedOps ops{ "session" };
		const auto operationId = ops.Begin(Drop(1));
		assert(operationId);
		assert(ops.Poll(0).action == PollAction::Send);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 2), 10) == ResolveOutcome::StaleResync);
		assert(ops.ExpectedRevision() == 2 && ops.PendingCount() == 1 && !ops.InFlight());
		assert(ops.Poll(11).action == PollAction::Send);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 3), 20) == ResolveOutcome::Reject);
		assert(ops.PendingCount() == 0 && ops.ExpectedRevision() == 3);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 4), 21) == ResolveOutcome::Duplicate);
		assert(ops.ExpectedRevision() == 3);
	}

	void TestLateCommitHistory()
	{
		PendingOwnedOps drops{ "session" };
		drops.SetExpectedRevision(5);
		const auto dropId = drops.Begin(Drop(4));
		assert(dropId);
		assert(drops.Poll(0).action == PollAction::Send);
		assert(drops.Poll(kOperationTimeoutMs).action == PollAction::Fail);
		const EntityId dropped = F4T::Drops::kFirstDroppedEntityId + 3U;
		LateCommitAction late;
		assert(drops.Resolve(Committed(*dropId, 6, dropped), kOperationTimeoutMs + 1, &late) == ResolveOutcome::LateCommit);
		assert(late.kind == Operation::Drop && late.entityId == dropped && late.itemKey == ItemKey() && late.count == 4U);
		assert(drops.ExpectedRevision() == 6);
		assert(drops.Resolve(Committed(*dropId, 6, dropped), kOperationTimeoutMs + 2) == ResolveOutcome::Duplicate);
		assert(drops.ExpectedRevision() == 6);
		assert(drops.Resolve(Rejected(*dropId, OwnedReason::InsufficientItems, 3), kOperationTimeoutMs + 3) == ResolveOutcome::Duplicate);
		assert(drops.ExpectedRevision() == 6);

		PendingOwnedOps pickups{ "session" };
		const auto pickupId = pickups.Begin(Pickup(F4T::Drops::kFirstDroppedEntityId + 5U));
		assert(pickupId);
		assert(pickups.Poll(0).action == PollAction::Send);
		assert(pickups.Poll(kOperationTimeoutMs).action == PollAction::Fail);
		LateCommitAction latePickup;
		const EntityId pickupTarget = F4T::Drops::kFirstDroppedEntityId + 5U;
		assert(pickups.Resolve(Committed(*pickupId, 2, pickupTarget), kOperationTimeoutMs + 1, &latePickup) == ResolveOutcome::LateCommit);
		assert(latePickup.kind == Operation::Pickup && latePickup.entityId == pickupTarget);
		assert(latePickup.itemKey.empty() && pickups.ExpectedRevision() == 2);

		PendingOwnedOps fallback{ "session" };
		const EntityId fallbackTarget = F4T::Drops::kFirstDroppedEntityId + 7U;
		const auto fallbackId = fallback.Begin(Pickup(fallbackTarget));
		assert(fallbackId);
		assert(fallback.Poll(0).action == PollAction::Send);
		assert(fallback.Poll(kOperationTimeoutMs).action == PollAction::Fail);
		OwnedResult noEntity;
		noEntity.operationId = *fallbackId;
		noEntity.committed = true;
		noEntity.revision = 3;
		LateCommitAction fallbackLate;
		assert(fallback.Resolve(noEntity, kOperationTimeoutMs + 1, &fallbackLate) == ResolveOutcome::LateCommit);
		assert(fallbackLate.kind == Operation::Pickup && fallbackLate.entityId == fallbackTarget);
		assert(fallback.ExpectedRevision() == 3);

		PendingOwnedOps lower{ "session" };
		lower.SetExpectedRevision(9);
		const auto lowerId = lower.Begin(Drop(1));
		assert(lowerId);
		assert(lower.Poll(0).action == PollAction::Send);
		assert(lower.Poll(kOperationTimeoutMs).action == PollAction::Fail);
		LateCommitAction staleLate;
		assert(lower.Resolve(Committed(*lowerId, 4), kOperationTimeoutMs + 1, &staleLate) == ResolveOutcome::LateCommit);
		assert(lower.ExpectedRevision() == 9);

		PendingOwnedOps reset{ "session" };
		const auto sentId = reset.Begin(Drop(2));
		const auto unsentId = reset.Begin(Drop(3));
		assert(sentId && unsentId);
		assert(reset.Poll(0).action == PollAction::Send);
		const std::vector<std::string> cleared{ *sentId, *unsentId };
		assert(reset.Reset() == cleared);
		LateCommitAction resetLate;
		assert(reset.Resolve(Committed(*sentId, 2), 1, &resetLate) == ResolveOutcome::LateCommit);
		assert(resetLate.kind == Operation::Drop && resetLate.count == 2U);
		assert(reset.Resolve(Committed(*unsentId, 3), 2) == ResolveOutcome::Duplicate);
		assert(reset.ExpectedRevision() == 2);
	}

	void TestStaleReplyAtOrBelowSentRevisionIgnored()
	{
		PendingOwnedOps ops{ "session" };
		ops.SetExpectedRevision(5);
		const auto operationId = ops.Begin(Drop(1));
		assert(operationId);
		assert(ops.Poll(0).action == PollAction::Send);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 5), 10) == ResolveOutcome::Unknown);
		assert(ops.ExpectedRevision() == 5 && ops.PendingCount() == 1 && ops.InFlight());
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 2), 11) == ResolveOutcome::Unknown);
		assert(ops.ExpectedRevision() == 5 && ops.InFlight());
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 8), 12) == ResolveOutcome::StaleResync);
		assert(ops.ExpectedRevision() == 8 && !ops.InFlight());
		assert(ops.Poll(13).request.at("expectedRevision") == 8U);
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 8), 14) == ResolveOutcome::Unknown);
		assert(ops.ExpectedRevision() == 8 && ops.InFlight());
		assert(ops.Resolve(Rejected(*operationId, OwnedReason::StaleRevision, 6), 15) == ResolveOutcome::Unknown);
		assert(ops.ExpectedRevision() == 8 && ops.PendingCount() == 1);
	}

	void TestViewCountUnknownIsOptional()
	{
		DropContext unknown = Context(5);
		assert(!unknown.viewCount.has_value());
		assert(ClassifyDrop(unknown, {}).verdict == DropVerdict::Gate);
		DropContext zero = Context(5);
		zero.viewCount = 0U;
		const auto zeroDecision = ClassifyDrop(zero, {});
		assert(zeroDecision.verdict == DropVerdict::Block && zeroDecision.reason == "countExceedsView");
		DropContext exact = Context(5);
		exact.viewCount = 5U;
		assert(ClassifyDrop(exact, {}).verdict == DropVerdict::Gate);
		DropContext shortView = Context(5);
		shortView.viewCount = 4U;
		assert(ClassifyDrop(shortView, {}).reason == "countExceedsView");
		DropContext cleared = Context(5);
		cleared.viewCount = 4U;
		cleared.viewCount.reset();
		assert(ClassifyDrop(cleared, {}).verdict == DropVerdict::Gate);
	}

	void TestDiffSnapshotClampsDuplicateStacks()
	{
		const std::vector<InventoryCount> stacked{ { KeyFor(5), 40000 }, { KeyFor(5), 40000 }, { "not-a-key", 9 } };
		const std::vector<InventoryChange> expected{ { KeyFor(5), 65535U } };
		assert(DiffSnapshot({}, stacked) == expected);
		const CountMap converged{ { KeyFor(5), 65535 } };
		assert(DiffSnapshot(converged, stacked).empty());
		assert((DiffSnapshot(CountMap{ { KeyFor(5), 70000 } }, { { KeyFor(5), 65535 } }).empty()));
		assert((DiffSnapshot(CountMap{ { "not-a-key", 4 } }, { { "not-a-key", 4 } }).empty()));
		assert((DiffSnapshot(CountMap{ { "not-a-key", 4 } }, {}).empty()));
		const std::vector<InventoryChange> capped{ { KeyFor(6), 65535U } };
		assert((DiffSnapshot({}, { { KeyFor(6), 65535 }, { KeyFor(6), 1 } }) == capped));
	}
}

int main()
{
	TestPendingSendAndIdenticalResend();
	TestNoResendAfterResolve();
	TestStaleResyncThenRetry();
	TestNinthOperationRefused();
	TestResetFailsClosed();
	TestDuplicateResultIgnored();
	TestRateLimitedRequeueKeepsOperationQueued();
	TestPickupOperation();
	TestReserveBlocksMaterialization();
	TestBindingCap();
	TestClassifierTruthTable();
	TestPickupClassifier();
	TestSnapshotChunks();
	TestDeltaChunksAndBounds();
	TestDiffSnapshot();
	TestAbsoluteDeadlineAcrossRearms();
	TestStaleResyncCapIsTerminal();
	TestLateCommitHistory();
	TestStaleReplyAtOrBelowSentRevisionIgnored();
	TestViewCountUnknownIsOptional();
	TestDiffSnapshotClampsDuplicateStacks();
}
