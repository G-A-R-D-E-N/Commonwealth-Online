#include "F4TSettlementCharge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace F4T::SettlementCharge
{
	namespace
	{
		using Json = nlohmann::json;
		using MaterialTier = WorkshopMaterials::MaterialTier;

		constexpr std::string_view kWoodItemKey{ "fallout4.esm:000731A3" };
		constexpr std::string_view kSteelItemKey{ "fallout4.esm:000731A4" };
		constexpr std::string_view kConcreteItemKey{ "fallout4.esm:00106D99" };

		void AddLine(Charge& a_charge, std::string_view a_itemKey, std::uint64_t a_count)
		{
			if (a_count == 0) {
				return;
			}
			for (auto& line : a_charge) {
				if (line.itemKey == a_itemKey) {
					line.count += static_cast<std::uint32_t>(a_count);
					return;
				}
			}
			a_charge.push_back(Line{ std::string{ a_itemKey }, static_cast<std::uint32_t>(a_count) });
		}

		[[nodiscard]] std::uint64_t CeilDiv(std::uint64_t a_numerator, std::uint64_t a_denominator) noexcept
		{
			return (a_numerator + a_denominator - 1) / a_denominator;
		}
	}

	WorkshopMaterials::MaterialTier TierForMaxHealth(float a_maxHealth) noexcept
	{
		if (a_maxHealth <= 300.0F) {
			return MaterialTier::Wood;
		}
		if (a_maxHealth <= 600.0F) {
			return MaterialTier::ScrapMetal;
		}
		if (a_maxHealth <= 1000.0F) {
			return MaterialTier::Concrete;
		}
		return MaterialTier::Reinforced;
	}

	std::string_view ComponentItemKey(WorkshopMaterials::MaterialTier a_tier) noexcept
	{
		switch (a_tier) {
		case MaterialTier::Wood:
			return kWoodItemKey;
		case MaterialTier::Concrete:
			return kConcreteItemKey;
		case MaterialTier::ScrapMetal:
		case MaterialTier::Reinforced:
			return kSteelItemKey;
		}
		return kSteelItemKey;
	}

	std::string_view TierName(WorkshopMaterials::MaterialTier a_tier) noexcept
	{
		switch (a_tier) {
		case MaterialTier::Wood:
			return "wood";
		case MaterialTier::ScrapMetal:
			return "scrapMetal";
		case MaterialTier::Concrete:
			return "concrete";
		case MaterialTier::Reinforced:
			return "reinforced";
		}
		return "reinforced";
	}

	Charge StructureRepairCharge(WorkshopMaterials::MaterialTier a_tier, float a_missingHealth, float a_maxHealth)
	{
		Charge charge;
		if (!(a_missingHealth > 0.0F) || !(a_maxHealth > 0.0F)) {
			return charge;
		}
		const auto fraction = (std::min)(1.0F, a_missingHealth / a_maxHealth);
		const auto units = (std::max<std::uint64_t>)(1, static_cast<std::uint64_t>(std::ceil(fraction * static_cast<float>(kFullStructureComponents))));
		AddLine(charge, ComponentItemKey(a_tier), units);
		return charge;
	}

	Charge StructureRebuildCharge(WorkshopMaterials::MaterialTier a_tier)
	{
		Charge charge;
		AddLine(charge, ComponentItemKey(a_tier), kFullStructureComponents);
		return charge;
	}

	Charge WorkbenchRepairCharge(float a_missingHealth, float a_maxHealth)
	{
		return StructureRepairCharge(MaterialTier::Reinforced, a_missingHealth, a_maxHealth);
	}

	Charge SettlerRecruitCharge()
	{
		Charge charge;
		AddLine(charge, kSteelItemKey, kSettlerRecruitSteelComponents);
		AddLine(charge, kConcreteItemKey, kSettlerRecruitConcreteComponents);
		return charge;
	}

	Charge UpkeepBlockCharge(
		std::uint64_t a_woodPieces,
		std::uint64_t a_scrapPieces,
		std::uint64_t a_concretePieces,
		std::uint64_t a_reinforcedPieces)
	{
		Charge charge;
		AddLine(charge, kWoodItemKey, CeilDiv(a_woodPieces, 10) * 1 * kUpkeepBlockHours);
		AddLine(charge, kSteelItemKey, CeilDiv(a_scrapPieces, 10) * 2 * kUpkeepBlockHours);
		AddLine(charge, kConcreteItemKey, CeilDiv(a_concretePieces, 5) * 3 * kUpkeepBlockHours);
		AddLine(charge, kSteelItemKey, CeilDiv(a_reinforcedPieces, 5) * 5 * kUpkeepBlockHours);
		return charge;
	}

	std::string_view PurposeName(Purpose a_purpose) noexcept
	{
		switch (a_purpose) {
		case Purpose::StructureRepair:
			return "structureRepair";
		case Purpose::StructureRebuild:
			return "structureRebuild";
		case Purpose::WorkbenchRepair:
			return "workbenchRepair";
		case Purpose::UpkeepPay:
			return "upkeepPay";
		case Purpose::SettlerRecruit:
			return "settlerRecruit";
		}
		return "structureRepair";
	}

	nlohmann::json MakeCharge(
		std::uint64_t a_operationId,
		Workshop::BuildZoneId a_buildZoneId,
		Purpose a_purpose,
		const Charge& a_charge)
	{
		Json items = Json::array();
		for (const auto& line : a_charge) {
			items.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count } });
		}
		return Json{
			{ "type", "settlementCharge" },
			{ "operationId", a_operationId },
			{ "buildZoneId", a_buildZoneId },
			{ "purpose", std::string{ PurposeName(a_purpose) } },
			{ "items", std::move(items) }
		};
	}

	std::optional<Result> NormalizeResult(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != "settlementChargeResult") {
			return std::nullopt;
		}
		const auto operationId = a_packet.find("operationId");
		const auto ok = a_packet.find("ok");
		if (operationId == a_packet.end() || !operationId->is_number_unsigned() || ok == a_packet.end() || !ok->is_boolean()) {
			return std::nullopt;
		}
		Result result{ operationId->get<std::uint64_t>(), ok->get<bool>(), {} };
		const auto reason = a_packet.find("reason");
		if (reason != a_packet.end() && reason->is_string()) {
			result.reason = reason->get<std::string>();
		}
		return result;
	}

	std::optional<Workshop::BuildZoneId> NormalizeZoneRequest(const nlohmann::json& a_packet, std::string_view a_type)
	{
		const auto type = a_packet.find("type");
		if (type == a_packet.end() || !type->is_string() || type->get<std::string>() != a_type) {
			return std::nullopt;
		}
		const auto buildZoneId = a_packet.find("buildZoneId");
		if (buildZoneId == a_packet.end() || !buildZoneId->is_number_unsigned() || buildZoneId->get<std::uint64_t>() == 0) {
			return std::nullopt;
		}
		return buildZoneId->get<std::uint64_t>();
	}
}
