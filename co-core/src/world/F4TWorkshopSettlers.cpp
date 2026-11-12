#include "F4TWorkshopSettlers.h"

#include <algorithm>
#include <cmath>

namespace F4T::WorkshopSettlers
{
	DetailLevel ChooseDetail(bool a_zoneUnderAttack, float a_nearestPlayerMeters) noexcept
	{
		if (a_zoneUnderAttack || a_nearestPlayerMeters <= 250.0F) {
			return DetailLevel::Full;
		}
		if (a_nearestPlayerMeters <= 600.0F) {
			return DetailLevel::Coarse;
		}
		return DetailLevel::Abstract;
	}

	bool TickSettlerRecovery(SettlerState& a_settler, double a_dtHours, std::uint64_t a_unixSeconds) noexcept
	{
		if (!std::isfinite(a_dtHours) || a_dtHours <= 0.0) {
			return false;
		}
		bool changed = false;
		if (a_settler.dead) {
			if (a_settler.deadUntilUnix != 0 && a_unixSeconds >= a_settler.deadUntilUnix) {
				a_settler.dead = false;
				a_settler.deadUntilUnix = 0;
				a_settler.health = a_settler.maxHealth > 0.0F ? a_settler.maxHealth : 100.0F;
				changed = true;
			}
			return changed;
		}
		if (!(a_settler.health < a_settler.maxHealth) || !(a_settler.maxHealth > 0.0F)) {
			return false;
		}
		const float before = a_settler.health;
		a_settler.health = (std::min)(a_settler.maxHealth, a_settler.health + RecoveryHpPerMinute * static_cast<float>(a_dtHours * 60.0));
		return a_settler.health != before;
	}

	bool CanRecoverSettler(const SettlerState& a_settler) noexcept
	{
		return a_settler.dead;
	}

	std::vector<SettlerState> Table::ForZone(Workshop::BuildZoneId a_buildZoneId) const
	{
		std::vector<SettlerState> result;
		for (const auto& [id, settler] : m_settlers) {
			(void)id;
			if (settler.buildZoneId == a_buildZoneId) {
				result.push_back(settler);
			}
		}
		return result;
	}

	bool Table::Restore(const std::vector<SettlerState>& a_settlers)
	{
		std::map<Workshop::ObjectId, SettlerState> restored;
		std::map<Workshop::BuildZoneId, std::size_t> perZone;
		for (const auto& settler : a_settlers) {
			if (settler.settlerId == 0 || settler.buildZoneId == 0 || restored.contains(settler.settlerId) ||
				!std::isfinite(settler.health) || !std::isfinite(settler.maxHealth) || !(settler.maxHealth > 0.0F) ||
				settler.health < 0.0F || settler.health > settler.maxHealth) {
				return false;
			}
			if (restored.size() >= MaximumSettlersPerServer) {
				return false;
			}
			auto& count = perZone[settler.buildZoneId];
			if (count >= MaximumSettlersPerSettlement) {
				return false;
			}
			++count;
			restored.emplace(settler.settlerId, settler);
		}
		m_nextSettlerId = restored.empty() ? 1 : restored.rbegin()->first + 1;
		m_settlers = std::move(restored);
		m_perZone = std::move(perZone);
		return true;
	}

	std::vector<SettlerState> Table::Snapshot() const
	{
		std::vector<SettlerState> result;
		result.reserve(m_settlers.size());
		for (const auto& [id, settler] : m_settlers) {
			(void)id;
			result.push_back(settler);
		}
		return result;
	}

	const SettlerState* Table::Find(Workshop::ObjectId a_settlerId) const noexcept
	{
		const auto found = m_settlers.find(a_settlerId);
		return found == m_settlers.end() ? nullptr : &found->second;
	}

	Workshop::ObjectId Table::MintSettlerId() noexcept
	{
		return m_nextSettlerId++;
	}

	bool Table::Upsert(SettlerState a_settler)
	{
		if (a_settler.settlerId == 0 || a_settler.buildZoneId == 0 ||
			!std::isfinite(a_settler.health) || !std::isfinite(a_settler.maxHealth) || !(a_settler.maxHealth > 0.0F)) {
			return false;
		}
		const auto found = m_settlers.find(a_settler.settlerId);
		if (found == m_settlers.end()) {
			if (m_settlers.size() >= MaximumSettlersPerServer) {
				return false;
			}
			auto& count = m_perZone[a_settler.buildZoneId];
			if (count >= MaximumSettlersPerSettlement) {
				return false;
			}
			++count;
			m_settlers.emplace(a_settler.settlerId, std::move(a_settler));
			return true;
		}
		if (found->second.buildZoneId != a_settler.buildZoneId) {
			return false;
		}
		found->second = std::move(a_settler);
		return true;
	}

	bool Table::Erase(Workshop::ObjectId a_settlerId)
	{
		const auto found = m_settlers.find(a_settlerId);
		if (found == m_settlers.end()) {
			return false;
		}
		auto& count = m_perZone[found->second.buildZoneId];
		if (count > 0) {
			count -= 1;
		}
		m_settlers.erase(found);
		return true;
	}

	std::size_t Table::TickAll(double a_dtHours, std::uint64_t a_unixSeconds)
	{
		std::size_t changed{};
		for (auto& [id, settler] : m_settlers) {
			(void)id;
			if (TickSettlerRecovery(settler, a_dtHours, a_unixSeconds)) {
				++changed;
			}
		}
		return changed;
	}
}
