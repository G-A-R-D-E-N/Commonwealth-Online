#include "F4TObjectState.h"

#include "F4TObjectWire.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::ObjectWire;
	using F4T::ObjectState::ApplyGuard;
	using F4T::ObjectState::IntentResult;
	using F4T::ObjectState::ObjectStateTable;
	using F4T::ObjectState::Outcome;

	Json Object(const char* a_plugin = "Test.esm", const char* a_formId = "00001234")
	{
		return Json{ { "plugin", a_plugin }, { "localFormId", a_formId } };
	}

	Json Position(double a_x = 10.0, double a_y = 20.0, double a_z = 30.0, const char* a_cell = "0000CAFE", const char* a_world = "")
	{
		return Json{ { "cellId", a_cell }, { "worldspaceId", a_world }, { "x", a_x }, { "y", a_y }, { "z", a_z } };
	}

	Json ActivateRequest()
	{
		return Json{ { "type", "objectActivateRequest" }, { "operationId", "op-1" }, { "object", Object() },
			{ "kind", "door" }, { "action", "open" }, { "position", Position() } };
	}

	Json LockRequest()
	{
		return Json{ { "type", "objectLockRequest" }, { "operationId", "lock-1" }, { "object", Object() },
			{ "locked", true }, { "position", Position() } };
	}

	Json StatePacket()
	{
		return Json{ { "type", "objectState" }, { "object", Object() }, { "kind", "door" },
			{ "open", true }, { "locked", true }, { "lockLevel", 3u }, { "revision", 2u } };
	}

	ObjectKey Key(const char* a_plugin = "Test.esm", const char* a_formId = "00001234")
	{
		const auto key = ParseObjectKey(Object(a_plugin, a_formId));
		assert(key);
		return *key;
	}

	ObjectPosition At(double a_x = 10.0, double a_y = 20.0, double a_z = 30.0)
	{
		const auto position = ParsePosition(Position(a_x, a_y, a_z));
		assert(position);
		return *position;
	}

	std::string Hex8(std::uint32_t a_value)
	{
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
		return buffer;
	}

	void TestObjectKey()
	{
		const auto key = ParseObjectKey(Object());
		assert(key && key->plugin == "Test.esm" && key->localId == 0x1234u);
		const auto light = ParseObjectKey(Object("Light.esl", "FE000123"));
		assert(light && light->localId == 0x123u);
		assert(ParseObjectKey(Object("Test.esm", "FF001234")) == std::nullopt);
		assert(ParseObjectKey(Object("Test.esm", "00000000")) == std::nullopt);
		assert(ParseObjectKey(Object("Test.esm", "001234")) == std::nullopt);
		assert(ParseObjectKey(Object("Test.esm", "ZZ001234")) == std::nullopt);
		assert(ParseObjectKey(Object("", "00001234")) == std::nullopt);
		assert(ParseObjectKey(Json{ { "plugin", "Test.esm" } }) == std::nullopt);
		assert(ParseObjectKey(Json{ { "plugin", "Test.esm" }, { "localFormId", "00001234" }, { "extra", 1u } }) == std::nullopt);
		assert(ParseObjectKey(Json::array()) == std::nullopt);
	}

	void TestNormalizeActivateRequest()
	{
		const auto request = ActivateRequest();
		assert(NormalizeActivateRequest(request) == request);
		auto extra = request;
		extra["unexpected"] = 1u;
		assert(!NormalizeActivateRequest(extra));
		auto missing = request;
		missing.erase("position");
		assert(!NormalizeActivateRequest(missing));
		auto kind = request;
		kind["kind"] = "container";
		assert(!NormalizeActivateRequest(kind));
		auto action = request;
		action["action"] = "ping";
		assert(!NormalizeActivateRequest(action));
		auto runtime = request;
		runtime["object"] = Object("Test.esm", "FF001234");
		assert(!NormalizeActivateRequest(runtime));
		auto text = request;
		text["position"] = Position();
		text["position"]["x"] = "10";
		assert(!NormalizeActivateRequest(text));
		auto operation = request;
		operation["operationId"] = std::string(65, 'a');
		assert(!NormalizeActivateRequest(operation));
		auto type = request;
		type["type"] = "objectLockRequest";
		assert(!NormalizeActivateRequest(type));
	}

	void TestNormalizeLockRequest()
	{
		const auto request = LockRequest();
		assert(NormalizeLockRequest(request) == request);
		auto level = request;
		level["lockLevel"] = 4u;
		assert(NormalizeLockRequest(level) == level);
		auto unlocked = request;
		unlocked["locked"] = false;
		assert(NormalizeLockRequest(unlocked) == unlocked);
		auto zeroLevel = request;
		zeroLevel["locked"] = false;
		zeroLevel["lockLevel"] = 0u;
		assert(!NormalizeLockRequest(zeroLevel));
		auto tooHigh = request;
		tooHigh["lockLevel"] = 9u;
		assert(!NormalizeLockRequest(tooHigh));
		auto numeric = request;
		numeric["locked"] = 1;
		assert(!NormalizeLockRequest(numeric));
		auto extra = request;
		extra["kind"] = "door";
		assert(!NormalizeLockRequest(extra));
		auto missing = request;
		missing.erase("position");
		assert(!NormalizeLockRequest(missing));
	}

	void TestNormalizeObjectState()
	{
		const auto packet = StatePacket();
		assert(NormalizeObjectState(packet) == packet);
		auto noOpen = packet;
		noOpen.erase("open");
		assert(!NormalizeObjectState(noOpen));
		auto activator = packet;
		activator["kind"] = "activator";
		assert(!NormalizeObjectState(activator));
		activator.erase("open");
		assert(NormalizeObjectState(activator) == activator);
		auto zeroRevision = packet;
		zeroRevision["revision"] = 0u;
		assert(!NormalizeObjectState(zeroRevision));
		auto orphanLevel = packet;
		orphanLevel.erase("locked");
		assert(!NormalizeObjectState(orphanLevel));
		auto kind = packet;
		kind["kind"] = "switch";
		assert(!NormalizeObjectState(kind));
		auto extra = packet;
		extra["cellId"] = "0000CAFE";
		assert(!NormalizeObjectState(extra));
		auto runtime = packet;
		runtime["object"] = Object("Test.esm", "FF000001");
		assert(!NormalizeObjectState(runtime));
	}

	void TestSnapshotAndRejected()
	{
		std::vector<ObjectState> states;
		states.push_back(ObjectState{ Key(), Kind::Door, true, true, true, 3u, 2u, At() });
		states.push_back(ObjectState{ Key("Other.esm", "00000005"), Kind::Activator, false, false, false, 0u, 1u, At(1.0, 2.0, 3.0) });
		const auto snapshot = BuildStateSnapshotPacket(states);
		assert(NormalizeObjectStateSnapshot(snapshot) == snapshot);
		assert(snapshot.at("objects").size() == 2);
		for (const auto& item : snapshot.at("objects")) {
			assert(!item.contains("type"));
			auto typed = item;
			typed["type"] = "objectState";
			assert(NormalizeObjectState(typed));
		}
		assert(NormalizeObjectState(BuildObjectState(states.front())) == BuildObjectState(states.front()));

		auto duplicate = snapshot;
		duplicate["objects"].push_back(duplicate["objects"].at(0));
		assert(!NormalizeObjectStateSnapshot(duplicate));
		auto extra = snapshot;
		extra["unexpected"] = 1u;
		assert(!NormalizeObjectStateSnapshot(extra));
		auto missing = snapshot;
		missing.erase("objects");
		assert(!NormalizeObjectStateSnapshot(missing));
		auto oversized = snapshot;
		oversized["objects"] = Json::array();
		for (std::size_t index = 0; index < kMaximumSnapshotObjects + 1; ++index) {
			auto item = duplicate["objects"].at(0);
			item["object"] = Json{ { "plugin", "Bulk.esm" }, { "localFormId", Hex8(static_cast<std::uint32_t>(index) + 1u) } };
			oversized["objects"].push_back(std::move(item));
		}
		assert(!NormalizeObjectStateSnapshot(oversized));
		assert(NormalizeObjectStateSnapshot(Json{ { "type", "objectStateSnapshot" }, { "objects", Json::array() } }));

		const auto rejected = Json{ { "type", "objectStateRejected" }, { "operationId", "op-1" }, { "reason", "outOfScope" } };
		assert(NormalizeObjectStateRejected(rejected) == rejected);
		auto emptyReason = rejected;
		emptyReason["reason"] = "";
		assert(!NormalizeObjectStateRejected(emptyReason));
		auto extraReason = rejected;
		extraReason["revision"] = 1u;
		assert(!NormalizeObjectStateRejected(extraReason));
	}

	void TestSnapshotChunkBound()
	{
		const auto plugin = std::string(kMaximumPluginChars - 4, 'P') + ".esm";
		std::vector<ObjectState> states;
		states.reserve(kMaximumSnapshotObjects);
		for (std::uint32_t index = 1; index <= kMaximumSnapshotObjects; ++index) {
			states.push_back(ObjectState{ ObjectKey{ plugin, index }, Kind::Door, true, true, true,
				kMaximumLockLevel, (std::numeric_limits<std::uint32_t>::max)(), At() });
		}
		const auto snapshot = BuildStateSnapshotPacket(states);
		assert(snapshot.at("objects").size() == kMaximumSnapshotObjects);
		assert(snapshot.dump().size() < 60000);
		assert(NormalizeObjectStateSnapshot(snapshot) == snapshot);
	}

	void TestTableActivation()
	{
		ObjectStateTable table;
		assert(table.IsEmpty());
		auto result = table.ApplyActivate(1, "open-1", Key(), Kind::Door, Action::Open, At());
		assert(result.outcome == Outcome::Applied && !result.replayed);
		assert(result.state.revision == 1 && result.state.open && !result.state.hasLock);
		assert(table.Size() == 1 && table.Find(Key()) != nullptr);
		result = table.ApplyActivate(1, "toggle-1", Key(), Kind::Door, Action::Toggle, At());
		assert(result.outcome == Outcome::Applied && !result.state.open && result.state.revision == 2);
		result = table.ApplyActivate(1, "close-1", Key(), Kind::Door, Action::Close, At());
		assert(result.outcome == Outcome::Applied && !result.state.open && result.state.revision == 2);
		result = table.ApplyActivate(1, "activate-1", Key(), Kind::Door, Action::Activate, At());
		assert(result.outcome == Outcome::Applied && result.state.open && result.state.revision == 3);
		result = table.ApplyActivate(1, "wrong-kind", Key(), Kind::Activator, Action::Activate, At());
		assert(result.outcome == Outcome::RejectedKindMismatch && result.state.revision == 0);
		assert(table.Find(Key())->kind == Kind::Door);

		const auto switchKey = Key("Test.esm", "00002000");
		result = table.ApplyActivate(1, "switch-open", switchKey, Kind::Activator, Action::Open, At());
		assert(result.outcome == Outcome::RejectedActionNotApplicable);
		result = table.ApplyActivate(1, "switch-1", switchKey, Kind::Activator, Action::Activate, At());
		assert(result.outcome == Outcome::Applied && result.state.revision == 1 && !result.state.open);
		result = table.ApplyActivate(1, "switch-2", switchKey, Kind::Activator, Action::Activate, At());
		assert(result.outcome == Outcome::Applied && result.state.revision == 2);
		assert(table.Size() == 2);
	}

	void TestTablePositionPinning()
	{
		ObjectStateTable table;
		auto result = table.ApplyActivate(1, "first", Key(), Kind::Door, Action::Open, At(10.0, 20.0, 30.0));
		assert(result.outcome == Outcome::Applied && result.state.revision == 1);
		result = table.ApplyActivate(1, "reach", Key(), Kind::Door, Action::Close, At(74.0, 20.0, 30.0));
		assert(result.outcome == Outcome::Applied && !result.state.open && result.state.revision == 2);
		result = table.ApplyActivate(1, "spoof", Key(), Kind::Door, Action::Open, At(75.01, 20.0, 30.0));
		assert(result.outcome == Outcome::RejectedPositionDrift && result.state.revision == 0);
		assert(!table.Find(Key())->open && table.Find(Key())->revision == 2);
		result = table.ApplyActivate(1, "spoof-z", Key(), Kind::Door, Action::Open, At(10.0, 20.0, 95.0));
		assert(result.outcome == Outcome::RejectedPositionDrift);
	}

	void TestTableReplay()
	{
		ObjectStateTable table;
		const auto first = table.ApplyActivate(1, "replay-1", Key(), Kind::Door, Action::Open, At());
		assert(first.outcome == Outcome::Applied && !first.replayed);
		const auto replay = table.ApplyActivate(1, "replay-1", Key(), Kind::Door, Action::Open, At());
		assert(replay.outcome == Outcome::Applied && replay.replayed);
		assert(replay.state.revision == first.state.revision && replay.state.open == first.state.open);
		assert(table.Find(Key())->revision == 1);
		const auto conflict = table.ApplyActivate(1, "replay-1", Key(), Kind::Door, Action::Close, At());
		assert(conflict.outcome == Outcome::RejectedOperationConflict);
		assert(table.Find(Key())->open);
		const auto otherPlayer = table.ApplyActivate(2, "replay-1", Key(), Kind::Door, Action::Open, At());
		assert(otherPlayer.outcome == Outcome::Applied && !otherPlayer.replayed);
		const auto drifted = table.ApplyActivate(1, "replay-2", Key(), Kind::Door, Action::Open, At(200.0, 0.0, 0.0));
		assert(drifted.outcome == Outcome::RejectedPositionDrift && !drifted.replayed);
		const auto driftedAgain = table.ApplyActivate(1, "replay-2", Key(), Kind::Door, Action::Open, At(200.0, 0.0, 0.0));
		assert(driftedAgain.outcome == Outcome::RejectedPositionDrift && driftedAgain.replayed);
	}

	void TestTableLock()
	{
		ObjectStateTable table;
		auto result = table.ApplyLock(1, "lock-unknown", Key(), true, 2u, At());
		assert(result.outcome == Outcome::RejectedUnknownObject);
		assert(table.IsEmpty());
		result = table.ApplyActivate(1, "activate", Key(), Kind::Door, Action::Close, At());
		assert(result.outcome == Outcome::Applied && result.state.revision == 1);
		result = table.ApplyLock(1, "lock-1", Key(), true, 3u, At());
		assert(result.outcome == Outcome::Applied && result.state.hasLock && result.state.locked);
		assert(result.state.lockLevel == 3u && result.state.revision == 2);
		result = table.ApplyLock(1, "lock-2", Key(), true, 3u, At());
		assert(result.outcome == Outcome::Applied && result.state.revision == 2);
		result = table.ApplyLock(1, "lock-drift", Key(), false, std::nullopt, At(500.0, 0.0, 0.0));
		assert(result.outcome == Outcome::RejectedPositionDrift);
		result = table.ApplyLock(1, "unlock", Key(), false, std::nullopt, At());
		assert(result.outcome == Outcome::Applied && !result.state.locked);
		assert(result.state.lockLevel == 0u && result.state.revision == 3 && result.state.hasLock);
		const auto state = BuildObjectState(*table.Find(Key()));
		assert(state.at("locked") == false && !state.contains("lockLevel"));
		const auto unknownLock = BuildObjectState(ObjectState{ Key(), Kind::Door, true, false, false, 0u, 1u, At() });
		assert(!unknownLock.contains("locked") && !unknownLock.contains("lockLevel"));
		const auto lockedState = BuildObjectState(ObjectState{ Key(), Kind::Door, true, true, true, 3u, 4u, At() });
		assert(lockedState.at("locked") == true && lockedState.at("lockLevel") == 3u);
		assert(NormalizeObjectState(lockedState));
	}

	void TestTableBounds()
	{
		ObjectStateTable table;
		for (std::uint32_t index = 1; index <= 4096; ++index) {
			const auto key = ParseObjectKey(Object("Bulk.esm", Hex8(index).c_str()));
			assert(key);
			const auto result = table.ApplyActivate(1, "bulk-" + std::to_string(index), *key, Kind::Door, Action::Open, At());
			assert(result.outcome == Outcome::Applied);
		}
		assert(table.Size() == 4096);
		const auto overflowKey = ParseObjectKey(Object("Bulk.esm", "00002FFF"));
		assert(overflowKey && table.Find(*overflowKey) == nullptr);
		const auto overflow = table.ApplyActivate(1, "bulk-overflow", *overflowKey, Kind::Door, Action::Open, At());
		assert(overflow.outcome == Outcome::RejectedCapacity);
		assert(table.Size() == 4096);
		assert(table.Snapshot(8).size() == 8);
	}

	void TestTablePerPlayerBound()
	{
		ObjectStateTable table{ 3 };
		for (std::uint32_t index = 1; index <= 3; ++index) {
			const auto result = table.ApplyActivate(1, "cap-" + std::to_string(index), Key("Bulk.esm", Hex8(index).c_str()),
				Kind::Door, Action::Open, At());
			assert(result.outcome == Outcome::Applied && result.state.revision == 1);
		}
		const auto blocked = Key("Bulk.esm", Hex8(4).c_str());
		const auto overflow = table.ApplyActivate(1, "cap-4", blocked, Kind::Door, Action::Open, At());
		assert(overflow.outcome == Outcome::RejectedPlayerCapacity && overflow.state.revision == 0);
		assert(table.Size() == 3 && table.Find(blocked) == nullptr);
		const auto otherPlayer = table.ApplyActivate(2, "cap-4", blocked, Kind::Door, Action::Open, At());
		assert(otherPlayer.outcome == Outcome::Applied && table.Size() == 4);
		const auto replayed = table.ApplyActivate(1, "cap-1", Key("Bulk.esm", Hex8(1).c_str()), Kind::Door, Action::Open, At());
		assert(replayed.outcome == Outcome::Applied && replayed.replayed && replayed.state.revision == 1);
		const auto toggled = table.ApplyActivate(1, "cap-1-toggle", Key("Bulk.esm", Hex8(1).c_str()), Kind::Door, Action::Toggle, At());
		assert(toggled.outcome == Outcome::Applied && toggled.state.revision == 2);
		assert(table.Snapshot(F4T::ObjectState::kMaximumObjects).size() == 4);
	}

	void TestReplayBounds()
	{
		ObjectStateTable table;
		for (std::uint32_t index = 1; index <= 4096; ++index) {
			assert(table.ApplyActivate(1, "evict-" + std::to_string(index), Key(), Kind::Door, Action::Toggle, At()).outcome == Outcome::Applied);
		}
		const auto pushed = table.ApplyActivate(1, "evict-4097", Key(), Kind::Door, Action::Toggle, At());
		assert(pushed.outcome == Outcome::Applied && !pushed.replayed);
		const auto evicted = table.ApplyActivate(1, "evict-1", Key(), Kind::Door, Action::Toggle, At());
		assert(evicted.outcome == Outcome::Applied && !evicted.replayed);
		const auto retained = table.ApplyActivate(1, "evict-4097", Key(), Kind::Door, Action::Toggle, At());
		assert(retained.replayed);
	}

	void TestApplyGuard()
	{
		ApplyGuard guard;
		const ObjectState expected{ Key(), Kind::Door, true, false, false, 0u, 1u, At() };
		assert(guard.Record(expected, 0.0) && guard.Size() == 1);
		auto observed = expected;
		observed.revision = 9;
		observed.position = At(500.0, 0.0, 0.0);
		assert(guard.Consume(observed, 1.0) && guard.Size() == 0);
		assert(!guard.Consume(observed, 1.0));
		assert(guard.Record(expected, 1.0));
		observed.open = false;
		assert(!guard.Consume(observed, 1.1) && guard.Size() == 1);
		assert(guard.Record(ObjectState{ Key("Other.esm", "00000009"), Kind::Activator, false, false, false, 0u, 1u, At() }, 1.2));
		assert(guard.Size() == 2);
		assert(guard.Record(expected, 1.3) && guard.Size() == 2);
		auto replaced = expected;
		assert(guard.Consume(replaced, 1.4) && guard.Size() == 1);
		guard.Expire(6.0);
		assert(guard.Size() == 1);
		guard.Expire(7.0);
		assert(guard.Size() == 0);
		assert(!guard.Record(expected, std::numeric_limits<double>::quiet_NaN()));
		for (std::size_t index = 0; index < F4T::ObjectState::kMaximumApplyGuardEntries + 8; ++index) {
			const auto key = ParseObjectKey(Object("Bulk.esm", Hex8(static_cast<std::uint32_t>(index) + 1u).c_str()));
			assert(key);
			assert(guard.Record(ObjectState{ *key, Kind::Door, false, false, false, 0u, 1u, At() }, 10.0));
		}
		assert(guard.Size() == F4T::ObjectState::kMaximumApplyGuardEntries);
		guard.Expire(20.0);
		assert(guard.Size() == 0);
	}
}

int main()
{
	TestObjectKey();
	TestNormalizeActivateRequest();
	TestNormalizeLockRequest();
	TestNormalizeObjectState();
	TestSnapshotAndRejected();
	TestSnapshotChunkBound();
	TestTableActivation();
	TestTablePositionPinning();
	TestTableReplay();
	TestTableLock();
	TestTableBounds();
	TestTablePerPlayerBound();
	TestReplayBounds();
	TestApplyGuard();
	return 0;
}
