#include "F4TNpcAuthority.h"

#include "F4TAuthorityElection.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <map>
#include <set>
#include <stdexcept>

namespace F4T::NpcAuthority
{
	namespace
	{
		template <class Entry>
		auto FindByScope(Entry& a_entries, const Scope& a_scope)
		{
			return std::lower_bound(
				a_entries.begin(),
				a_entries.end(),
				a_scope,
				[](const auto& a_entry, const Scope& a_value) {
					return a_entry.scope < a_value;
				});
		}

		template <class Entries>
		auto FindEpoch(Entries& a_epochs, const Scope& a_scope)
		{
			return std::lower_bound(
				a_epochs.begin(),
				a_epochs.end(),
				a_scope,
				[](const auto& a_entry, const Scope& a_value) {
					return a_entry.scope < a_value;
				});
		}
	}

	void Manager::Clear() noexcept
	{
		_assignments.clear();
		_lastEpochs.clear();
	}

	std::optional<Assignment> Manager::Get(const Scope& a_scope) const
	{
		const auto found = std::lower_bound(
			_assignments.begin(),
			_assignments.end(),
			a_scope,
			[](const Assignment& a_entry, const Scope& a_value) {
				return a_entry.scope < a_value;
			});
		if (found == _assignments.end() || found->scope != a_scope) {
			return std::nullopt;
		}
		return *found;
	}

	std::vector<Assignment> Manager::Assignments() const
	{
		return _assignments;
	}

	bool Manager::Authorize(std::uint32_t a_playerId, const Scope& a_scope, std::uint32_t a_epoch) const
	{
		const auto assignment = Get(a_scope);
		return assignment && assignment->playerId == a_playerId && assignment->epoch == a_epoch;
	}

	std::vector<Change> Manager::Reconcile(std::span<const PlayerScope> a_players)
	{
		std::map<Scope, std::vector<std::uint32_t>> eligible;
		for (const auto& player : a_players) {
			if (player.playerId == 0) {
				continue;
			}
			eligible[player.scope].push_back(player.playerId);
		}

		std::set<Scope> scopes;
		for (const auto& assignment : _assignments) {
			scopes.insert(assignment.scope);
		}
		for (const auto& [scope, ids] : eligible) {
			(void)ids;
			scopes.insert(scope);
		}

		const std::vector<std::uint32_t> noCandidates;
		std::vector<Change> changes;
		for (const auto& scope : scopes) {
			const auto current = Get(scope);
			const auto previousPlayerId = current ? current->playerId : 0U;
			const auto eligibleIt = eligible.find(scope);
			const auto& candidates = eligibleIt == eligible.end() ? noCandidates : eligibleIt->second;
			const auto nextPlayerId = Authority::ElectSticky(
				previousPlayerId, std::span<const std::uint32_t>(candidates));
			if (previousPlayerId == nextPlayerId) {
				continue;
			}

			auto epochIt = FindEpoch(_lastEpochs, scope);
			std::uint32_t epoch = 1;
			++_touchCounter;
			if (epochIt != _lastEpochs.end() && epochIt->scope == scope) {
				if (epochIt->epoch == (std::numeric_limits<std::uint32_t>::max)()) {
					throw std::overflow_error("NPC authority epoch exhausted");
				}
				epoch = epochIt->epoch + 1U;
				epochIt->epoch = epoch;
				epochIt->lastTouched = _touchCounter;
			} else {
				_lastEpochs.insert(epochIt, EpochEntry{ scope, epoch, _touchCounter });
			}

			auto assignmentIt = FindByScope(_assignments, scope);
			if (nextPlayerId == 0) {
				if (assignmentIt != _assignments.end() && assignmentIt->scope == scope) {
					_assignments.erase(assignmentIt);
				}
			} else {
				const Assignment next{ scope, nextPlayerId, epoch };
				if (assignmentIt != _assignments.end() && assignmentIt->scope == scope) {
					*assignmentIt = next;
				} else {
					_assignments.insert(assignmentIt, next);
				}
			}

			changes.push_back(Change{ scope, previousPlayerId, nextPlayerId, epoch });
		}

		ForgetColdEpochs();
		return changes;
	}

	std::size_t Manager::TrackedEpochScopes() const noexcept
	{
		return _lastEpochs.size();
	}

	void Manager::ForgetColdEpochs()
	{
		if (_lastEpochs.size() <= MaximumTrackedEpochScopes) {
			return;
		}

		std::vector<const EpochEntry*> evictable;
		evictable.reserve(_lastEpochs.size());
		for (const auto& entry : _lastEpochs) {
			if (!Get(entry.scope)) {
				evictable.push_back(std::addressof(entry));
			}
		}
		const auto excess = _lastEpochs.size() - MaximumTrackedEpochScopes;
		if (evictable.size() <= excess) {
			std::set<Scope> doomed;
			for (const auto* entry : evictable) {
				doomed.insert(entry->scope);
			}
			std::erase_if(_lastEpochs, [&doomed](const EpochEntry& a_entry) {
				return doomed.contains(a_entry.scope);
			});
			return;
		}

		std::nth_element(
			evictable.begin(),
			evictable.begin() + static_cast<std::ptrdiff_t>(excess),
			evictable.end(),
			[](const EpochEntry* a_left, const EpochEntry* a_right) {
				return a_left->lastTouched < a_right->lastTouched;
			});
		std::set<Scope> doomed;
		for (std::size_t index = 0; index < excess; ++index) {
			doomed.insert(evictable[index]->scope);
		}
		std::erase_if(_lastEpochs, [&doomed](const EpochEntry& a_entry) {
			return doomed.contains(a_entry.scope);
		});
	}
}
