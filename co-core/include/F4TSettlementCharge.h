#pragma once

#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementCharge
{
	inline constexpr std::uint32_t kFullStructureComponents{ 4 };
	inline constexpr std::uint64_t kUpkeepBlockHours{ 24 };
	inline constexpr std::uint32_t kSettlerRecruitSteelComponents{ 4 }; // TODO owner confirm
	inline constexpr std::uint32_t kSettlerRecruitConcreteComponents{ 2 }; // TODO owner confirm

	enum class Purpose : std::uint8_t
	{
		StructureRepair,
		StructureRebuild,
		WorkbenchRepair,
		UpkeepPay,
		SettlerRecruit
	};

	struct Line
	{
		std::string itemKey;
		std::uint32_t count{};

		bool operator==(const Line&) const = default;
	};

	using Charge = std::vector<Line>;

	struct Result
	{
		std::uint64_t operationId{};
		bool ok{};
		std::string reason;
	};

	[[nodiscard]] WorkshopMaterials::MaterialTier TierForMaxHealth(float a_maxHealth) noexcept;
	[[nodiscard]] std::string_view ComponentItemKey(WorkshopMaterials::MaterialTier a_tier) noexcept;
	[[nodiscard]] std::string_view TierName(WorkshopMaterials::MaterialTier a_tier) noexcept;
	[[nodiscard]] Charge StructureRepairCharge(WorkshopMaterials::MaterialTier a_tier, float a_missingHealth, float a_maxHealth);
	[[nodiscard]] Charge StructureRebuildCharge(WorkshopMaterials::MaterialTier a_tier);
	[[nodiscard]] Charge WorkbenchRepairCharge(float a_missingHealth, float a_maxHealth);
	[[nodiscard]] Charge SettlerRecruitCharge();
	[[nodiscard]] Charge UpkeepBlockCharge(
		std::uint64_t a_woodPieces,
		std::uint64_t a_scrapPieces,
		std::uint64_t a_concretePieces,
		std::uint64_t a_reinforcedPieces);
	[[nodiscard]] std::string_view PurposeName(Purpose a_purpose) noexcept;
	[[nodiscard]] nlohmann::json MakeCharge(
		std::uint64_t a_operationId,
		Workshop::BuildZoneId a_buildZoneId,
		Purpose a_purpose,
		const Charge& a_charge);
	[[nodiscard]] std::optional<Result> NormalizeResult(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Workshop::BuildZoneId> NormalizeZoneRequest(const nlohmann::json& a_packet, std::string_view a_type);
}
