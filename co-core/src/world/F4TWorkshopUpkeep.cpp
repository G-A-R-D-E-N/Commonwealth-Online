#include "F4TWorkshopUpkeep.h"

#include <algorithm>
#include <vector>

namespace F4T::Workshop
{
	bool SettlementState::TickUnpaidDecay(double a_dtHours, std::uint64_t a_unixSeconds)
	{
		if (a_dtHours <= 0.0) {
			return false;
		}
		bool healthChanged = false;
		std::vector<ObjectId> toDestroy;
		for (auto& entry : m_objects) {
			auto& object = entry.second;
			if (object.maxHealth <= 0.0F) {
				continue;
			}
			const float before = object.health;
			WorkshopUpkeep::TickDecay(object, true, a_dtHours);
			if (object.health == before) {
				continue;
			}
			healthChanged = true;
			if (object.health <= 0.0F) {
				toDestroy.push_back(entry.first);
			}
		}
		bool destroyed = false;
		for (const auto objectId : toDestroy) {
			if (DestroyObject(objectId, a_unixSeconds)) {
				destroyed = true;
			}
		}
		if (healthChanged && !destroyed) {
			++m_revision;
		}
		return healthChanged || destroyed;
	}

	bool SettlementState::TickFractionalDecay(double a_fractionPerHour, double a_dtHours, std::uint64_t a_unixSeconds)
	{
		if (!(a_fractionPerHour > 0.0) || a_dtHours <= 0.0) {
			return false;
		}
		bool healthChanged = false;
		std::vector<ObjectId> toDestroy;
		for (auto& entry : m_objects) {
			auto& object = entry.second;
			if (object.maxHealth <= 0.0F) {
				continue;
			}
			const float damage = static_cast<float>(object.maxHealth * a_fractionPerHour * a_dtHours);
			if (!(damage > 0.0F)) {
				continue;
			}
			object.health -= damage;
			object.lastHitUnix = a_unixSeconds;
			healthChanged = true;
			if (object.health <= 0.0F) {
				toDestroy.push_back(entry.first);
			}
		}
		bool destroyed = false;
		for (const auto objectId : toDestroy) {
			if (DestroyObject(objectId, a_unixSeconds)) {
				destroyed = true;
			}
		}
		if (healthChanged && !destroyed) {
			++m_revision;
		}
		return healthChanged || destroyed;
	}

	bool SettlementState::Restore(
		Revision a_revision,
		ObjectId a_nextObjectId,
		const std::vector<ObjectState>& a_objects,
		const std::vector<StoredTombstone>& a_tombstones)
	{
		if (!m_policyValid || a_nextObjectId == 0 || a_objects.size() > m_policy.maxObjects) {
			return false;
		}
		std::map<ObjectId, ObjectState> objects;
		std::map<ObjectId, ObjectState> tombstones;
		const auto usable = [&](const ObjectState& a_state) {
			return a_state.objectId != 0 && a_state.objectId < a_nextObjectId &&
			       a_state.buildZoneId == m_policy.buildZoneId &&
			       !objects.contains(a_state.objectId) && !tombstones.contains(a_state.objectId) &&
			       Valid(a_state.form) && Valid(a_state.space) && Valid(a_state.transform);
		};
		for (const auto& object : a_objects) {
			if (!usable(object)) {
				return false;
			}
			objects.emplace(object.objectId, object);
		}
		for (const auto& tombstone : a_tombstones) {
			if (tombstone.profileId.empty()) {
				continue;
			}
			if (!usable(tombstone.state) || LocalFormIdOutOfRange(tombstone.state.form.localFormId)) {
				return false;
			}
			tombstones.emplace(tombstone.state.objectId, tombstone.state);
		}
		auto ordered = a_tombstones;
		std::sort(ordered.begin(), ordered.end(), [](const StoredTombstone& a_left, const StoredTombstone& a_right) {
			if (a_left.state.destroyedUnixSeconds != a_right.state.destroyedUnixSeconds) {
				return a_left.state.destroyedUnixSeconds < a_right.state.destroyedUnixSeconds;
			}
			return a_left.state.objectId < a_right.state.objectId;
		});
		m_revision = a_revision;
		m_nextObjectId = a_nextObjectId;
		m_objects = std::move(objects);
		m_tombstones.clear();
		m_tombstoneProfiles.clear();
		m_results.clear();
		m_resultOrder.clear();
		for (const auto& tombstone : ordered) {
			AddTombstone(tombstone.state, tombstone.profileId);
		}
		return true;
	}

	bool SettlementState::DestroyObject(ObjectId a_objectId, std::uint64_t a_unixSeconds)
	{
		const auto it = m_objects.find(a_objectId);
		if (it == m_objects.end()) {
			return false;
		}
		ObjectState object = it->second;
		object.destroyedUnixSeconds = a_unixSeconds;
		object.health = 0.0F;
		m_objects.erase(it);
		AddTombstone(object, object.placedBy);
		++m_revision;
		(void)CollapseUnsupported(a_unixSeconds);
		return true;
	}

	std::vector<ObjectId> SettlementState::CollapseUnsupported(std::uint64_t a_unixSeconds)
	{
		std::vector<ObjectId> collapsed;
		for (;;) {
			std::vector<ObjectId> falling;
			for (const auto& [objectId, object] : m_objects) {
				if (!HasSupportPath(objectId, object)) {
					falling.push_back(objectId);
				}
			}
			if (falling.empty()) {
				break;
			}
			for (const auto objectId : falling) {
				const auto it = m_objects.find(objectId);
				if (it == m_objects.end()) {
					continue;
				}
				ObjectState object = it->second;
				object.destroyedUnixSeconds = a_unixSeconds;
				object.health = 0.0F;
				m_objects.erase(it);
				AddTombstone(object, object.placedBy);
				collapsed.push_back(objectId);
			}
			++m_revision;
		}
		return collapsed;
	}

	void SettlementState::AddTombstone(const ObjectState& a_object, const std::string& a_profileId)
	{
		if (a_profileId.empty()) {
			return;
		}
		std::size_t owned = 0;
		for (const auto& entry : m_tombstoneProfiles) {
			if (entry.second == a_profileId) {
				++owned;
			}
		}
		if (owned >= MaximumTombstonesPerProfile) {
			EvictOldestTombstone(a_profileId);
		}
		if (m_tombstones.size() >= MaximumWorldTombstones) {
			EvictOldestTombstone(std::string{});
		}
		m_tombstones[a_object.objectId] = a_object;
		m_tombstoneProfiles[a_object.objectId] = a_profileId;
	}

	void SettlementState::EvictOldestTombstone(const std::string& a_profileId)
	{
		auto oldest = m_tombstones.end();
		for (auto it = m_tombstones.begin(); it != m_tombstones.end(); ++it) {
			if (!a_profileId.empty()) {
				const auto owner = m_tombstoneProfiles.find(it->first);
				if (owner == m_tombstoneProfiles.end() || owner->second != a_profileId) {
					continue;
				}
			}
			if (oldest == m_tombstones.end() || it->second.destroyedUnixSeconds < oldest->second.destroyedUnixSeconds) {
				oldest = it;
			}
		}
		if (oldest == m_tombstones.end()) {
			return;
		}
		m_tombstoneProfiles.erase(oldest->first);
		m_tombstones.erase(oldest);
	}
}

namespace F4T::WorkshopUpkeep
{
	namespace
	{
		[[nodiscard]] std::uint64_t CeilDiv(std::uint64_t a_numerator, std::uint64_t a_denominator) noexcept
		{
			if (a_denominator == 0) {
				return 0;
			}
			return (a_numerator + a_denominator - 1) / a_denominator;
		}
	}

	UpkeepState Evaluate(std::uint64_t a_nowUnix, std::uint64_t a_paidUntilUnix) noexcept
	{
		if (a_paidUntilUnix == 0) {
			return { 0, false };
		}
		const bool decaying = a_nowUnix > a_paidUntilUnix;
		return { a_paidUntilUnix, decaying };
	}

	std::uint64_t HourlyCostFromCounts(
		std::uint64_t a_woodPieces,
		std::uint64_t a_scrapPieces,
		std::uint64_t a_concretePieces,
		std::uint64_t a_reinforcedPieces) noexcept
	{
		return CeilDiv(a_woodPieces, 10) +
		       CeilDiv(a_scrapPieces, 10) * 2 +
		       CeilDiv(a_concretePieces, 5) * 3 +
		       CeilDiv(a_reinforcedPieces, 5) * 5;
	}

	void TickDecay(Workshop::ObjectState& a_object, bool a_unpaid, double a_dtHours) noexcept
	{
		if (!a_unpaid || a_object.maxHealth <= 0.0F || a_dtHours <= 0.0) {
			return;
		}
		const auto loss = static_cast<float>(kDecayFractionPerUnpaidHour * a_dtHours) * a_object.maxHealth;
		a_object.health -= loss;
		if (a_object.health < 0.0F) {
			a_object.health = 0.0F;
		}
	}

}
