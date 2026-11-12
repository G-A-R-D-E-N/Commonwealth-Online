#pragma once

#include "F4TWorkshopTransactions.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace F4T::WorkshopSettlers
{
	inline constexpr std::size_t MaximumSettlersPerSettlement = 10;
	inline constexpr std::size_t MaximumSettlersPerPlayer = 30;
	inline constexpr std::size_t MaximumSettlersPerServer = 400;
	inline constexpr std::size_t MaximumFullDetailSettlers = 120;
	inline constexpr float RecoveryHpPerMinute = 1.0F;
	inline constexpr std::uint64_t DeadRecoverySeconds = 86400;

	enum class DetailLevel : std::uint8_t
	{
		Abstract,
		Coarse,
		Full
	};

	enum class Assignment : std::uint8_t
	{
		Idle,
		Guard,
		Farm,
		Scavenge,
		Purifier
	};

	struct SettlerState
	{
		Workshop::ObjectId settlerId{};
		Workshop::BuildZoneId buildZoneId{};
		float health{ 100.0F };
		float maxHealth{ 100.0F };
		Assignment assignment{ Assignment::Idle };
		bool dead{};
		std::uint64_t deadUntilUnix{};
	};

	[[nodiscard]] DetailLevel ChooseDetail(
		bool a_zoneUnderAttack,
		float a_nearestPlayerMeters) noexcept;

	[[nodiscard]] bool TickSettlerRecovery(SettlerState& a_settler, double a_dtHours, std::uint64_t a_unixSeconds) noexcept;
	[[nodiscard]] bool CanRecoverSettler(const SettlerState& a_settler) noexcept;

	class Table
	{
	public:
		[[nodiscard]] std::vector<SettlerState> ForZone(Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::size_t TotalCount() const noexcept { return m_settlers.size(); }
		[[nodiscard]] bool Restore(const std::vector<SettlerState>& a_settlers);
		[[nodiscard]] std::vector<SettlerState> Snapshot() const;
		[[nodiscard]] const SettlerState* Find(Workshop::ObjectId a_settlerId) const noexcept;
		[[nodiscard]] Workshop::ObjectId MintSettlerId() noexcept;
		[[nodiscard]] bool Upsert(SettlerState a_settler);
		bool Erase(Workshop::ObjectId a_settlerId);
		std::size_t TickAll(double a_dtHours, std::uint64_t a_unixSeconds);

	private:
		std::map<Workshop::ObjectId, SettlerState> m_settlers;
		std::map<Workshop::BuildZoneId, std::size_t> m_perZone;
		Workshop::ObjectId m_nextSettlerId{ 1 };
	};
}
