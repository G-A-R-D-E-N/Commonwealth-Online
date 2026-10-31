#include "F4TObjectState.h"

#include <algorithm>
#include <cmath>

namespace F4T::ObjectState
{
	namespace
	{
		using F4T::ObjectWire::Action;
		using F4T::ObjectWire::Kind;
		using F4T::ObjectWire::ObjectKey;
		using F4T::ObjectWire::ObjectPosition;
		using F4T::ObjectWire::ObjectState;

		bool SameObservableState(const ObjectState& a_left, const ObjectState& a_right)
		{
			return a_left.kind == a_right.kind &&
				a_left.open == a_right.open &&
				a_left.hasLock == a_right.hasLock &&
				a_left.locked == a_right.locked &&
				a_left.lockLevel == a_right.lockLevel;
		}

		bool PositionDrifted(const ObjectPosition& a_stored, const ObjectPosition& a_reported)
		{
			const auto dx = a_stored.x - a_reported.x;
			const auto dy = a_stored.y - a_reported.y;
			const auto dz = a_stored.z - a_reported.z;
			const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			return !std::isfinite(distance) || distance > kMaximumPositionDrift;
		}

		bool ActionApplies(Kind a_kind, Action a_action)
		{
			return a_kind == Kind::Door || a_action == Action::Activate;
		}

		bool OpenAfter(Kind a_kind, Action a_action, bool a_current)
		{
			if (a_kind == Kind::Activator) return a_current;
			if (a_action == Action::Open) return true;
			if (a_action == Action::Close) return false;
			return !a_current;
		}
	}

	ObjectStateTable::ObjectStateTable(std::size_t a_maximumObjectsPerPlayer) :
		_maximumObjectsPerPlayer(a_maximumObjectsPerPlayer)
	{}

	std::optional<IntentResult> ObjectStateTable::LookUpReplay(const ReplayRecord& a_probe) const
	{
		if (a_probe.operationId.empty()) {
			return std::nullopt;
		}
		const auto found = _replays.find(ReplayKey{ a_probe.playerId, a_probe.operationId });
		if (found == _replays.end()) {
			return std::nullopt;
		}
		const auto& previous = found->second;
		const bool identical =
			previous.request == a_probe.request &&
			previous.object == a_probe.object &&
			previous.action == a_probe.action &&
			previous.locked == a_probe.locked &&
			previous.lockLevel == a_probe.lockLevel;
		if (!identical) {
			IntentResult conflict;
			conflict.outcome = Outcome::RejectedOperationConflict;
			return conflict;
		}
		auto replayed = previous.result;
		replayed.replayed = true;
		return replayed;
	}

	void ObjectStateTable::Remember(ReplayRecord a_record)
	{
		const ReplayKey key{ a_record.playerId, a_record.operationId };
		a_record.sequence = _nextSequence++;
		const auto inserted = _replays.insert_or_assign(key, std::move(a_record));
		_replayOrder.push_back({ inserted.first->second.sequence, key });
		while (_replays.size() > kMaximumReplayEntries && !_replayOrder.empty()) {
			const auto oldest = _replayOrder.front();
			_replayOrder.pop_front();
			const auto found = _replays.find(oldest.second);
			if (found != _replays.end() && found->second.sequence == oldest.first) {
				_replays.erase(found);
			}
		}
	}

	IntentResult ObjectStateTable::ApplyActivate(
		std::uint32_t a_playerId,
		const std::string& a_operationId,
		const ObjectKey& a_object,
		Kind a_kind,
		Action a_action,
		const ObjectPosition& a_position)
	{
		ReplayRecord probe;
		probe.playerId = a_playerId;
		probe.operationId = a_operationId;
		probe.request = Request::Activate;
		probe.object = a_object;
		probe.action = a_action;
		if (const auto looked = LookUpReplay(probe)) {
			return *looked;
		}

		IntentResult result;
		result.state.object = a_object;
		result.state.kind = a_kind;
		result.state.position = a_position;
		const auto finish = [this, &probe](IntentResult a_result) {
			if (a_result.outcome != Outcome::Applied) {
				a_result.state = ObjectState{};
			}
			probe.result = a_result;
			Remember(probe);
			return a_result;
		};

		const auto found = _objects.find(a_object);
		if (found == _objects.end()) {
			if (_objects.size() >= kMaximumObjects) {
				result.outcome = Outcome::RejectedCapacity;
				return finish(result);
			}
			const auto createdByPlayer = _objectsCreatedByPlayer.find(a_playerId);
			const auto created = createdByPlayer == _objectsCreatedByPlayer.end() ? std::size_t{} : createdByPlayer->second;
			if (created >= _maximumObjectsPerPlayer) {
				result.outcome = Outcome::RejectedPlayerCapacity;
				return finish(result);
			}
			if (!ActionApplies(a_kind, a_action)) {
				result.outcome = Outcome::RejectedActionNotApplicable;
				return finish(result);
			}
			result.state.revision = 1;
			result.state.open = OpenAfter(a_kind, a_action, false);
			result.outcome = Outcome::Applied;
			_objects.emplace(a_object, result.state);
			_objectsCreatedByPlayer[a_playerId] = created + 1;
			return finish(result);
		}

		result.state = found->second;
		if (found->second.kind != a_kind) {
			result.outcome = Outcome::RejectedKindMismatch;
			return finish(result);
		}
		if (PositionDrifted(found->second.position, a_position)) {
			result.outcome = Outcome::RejectedPositionDrift;
			return finish(result);
		}
		if (!ActionApplies(a_kind, a_action)) {
			result.outcome = Outcome::RejectedActionNotApplicable;
			return finish(result);
		}
		if (a_kind == Kind::Activator) {
			++result.state.revision;
		} else {
			const auto open = OpenAfter(a_kind, a_action, found->second.open);
			if (open != found->second.open) {
				result.state.open = open;
				++result.state.revision;
			}
		}
		result.outcome = Outcome::Applied;
		_objects.insert_or_assign(a_object, result.state);
		return finish(result);
	}

	IntentResult ObjectStateTable::ApplyLock(
		std::uint32_t a_playerId,
		const std::string& a_operationId,
		const ObjectKey& a_object,
		bool a_locked,
		std::optional<std::uint32_t> a_lockLevel,
		const ObjectPosition& a_position)
	{
		ReplayRecord probe;
		probe.playerId = a_playerId;
		probe.operationId = a_operationId;
		probe.request = Request::Lock;
		probe.object = a_object;
		probe.locked = a_locked;
		probe.lockLevel = a_locked ? a_lockLevel : std::nullopt;
		if (const auto looked = LookUpReplay(probe)) {
			return *looked;
		}

		IntentResult result;
		result.state.object = a_object;
		result.state.position = a_position;
		const auto finish = [this, &probe](IntentResult a_result) {
			if (a_result.outcome != Outcome::Applied) {
				a_result.state = ObjectState{};
			}
			probe.result = a_result;
			Remember(probe);
			return a_result;
		};

		const auto found = _objects.find(a_object);
		if (found == _objects.end()) {
			result.outcome = Outcome::RejectedUnknownObject;
			return finish(result);
		}
		result.state = found->second;
		if (PositionDrifted(found->second.position, a_position)) {
			result.outcome = Outcome::RejectedPositionDrift;
			return finish(result);
		}
		const auto level = a_locked ? a_lockLevel.value_or(0) : 0;
		if (!found->second.hasLock || found->second.locked != a_locked || found->second.lockLevel != level) {
			result.state.hasLock = true;
			result.state.locked = a_locked;
			result.state.lockLevel = level;
			++result.state.revision;
		}
		result.outcome = Outcome::Applied;
		_objects.insert_or_assign(a_object, result.state);
		return finish(result);
	}

	const ObjectState* ObjectStateTable::Find(const ObjectKey& a_object) const noexcept
	{
		const auto found = _objects.find(a_object);
		return found == _objects.end() ? nullptr : &found->second;
	}

	bool ObjectStateTable::IsEmpty() const noexcept
	{
		return _objects.empty();
	}

	std::size_t ObjectStateTable::Size() const noexcept
	{
		return _objects.size();
	}

	std::vector<ObjectState> ObjectStateTable::Snapshot(std::size_t a_maximum) const
	{
		std::vector<ObjectState> states;
		states.reserve(std::min(a_maximum, _objects.size()));
		for (const auto& [key, state] : _objects) {
			(void)key;
			if (states.size() >= a_maximum) {
				break;
			}
			states.push_back(state);
		}
		return states;
	}

	bool ApplyGuard::Record(const ObjectState& a_expected, double a_nowSeconds)
	{
		if (!std::isfinite(a_nowSeconds)) {
			return false;
		}
		Expire(a_nowSeconds);
		const auto existing = std::find_if(_entries.begin(), _entries.end(), [&a_expected](const Entry& a_entry) {
			return a_entry.expected.object == a_expected.object;
		});
		if (existing != _entries.end()) {
			existing->expected = a_expected;
			existing->expiresAtSeconds = a_nowSeconds + kApplyGuardLifetimeSeconds;
			return true;
		}
		if (_entries.size() >= kMaximumApplyGuardEntries) {
			_entries.pop_front();
		}
		_entries.push_back(Entry{ a_expected, a_nowSeconds + kApplyGuardLifetimeSeconds });
		return true;
	}

	bool ApplyGuard::Consume(const ObjectState& a_observed, double a_nowSeconds)
	{
		if (!std::isfinite(a_nowSeconds)) {
			return false;
		}
		Expire(a_nowSeconds);
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&a_observed](const Entry& a_entry) {
			return a_entry.expected.object == a_observed.object && SameObservableState(a_entry.expected, a_observed);
		});
		if (found == _entries.end()) {
			return false;
		}
		_entries.erase(found);
		return true;
	}

	void ApplyGuard::Expire(double a_nowSeconds) noexcept
	{
		if (!std::isfinite(a_nowSeconds)) {
			return;
		}
		std::erase_if(_entries, [a_nowSeconds](const Entry& a_entry) {
			return !(a_entry.expiresAtSeconds > a_nowSeconds);
		});
	}

	std::size_t ApplyGuard::Size() const noexcept
	{
		return _entries.size();
	}
}
