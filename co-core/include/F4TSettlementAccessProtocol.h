#pragma once

#include "F4TSettlementAccess.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementAccessProtocol
{
	inline constexpr std::size_t kMaximumDestroyedStructures{ 64 };

	struct Update
	{
		Workshop::BuildZoneId buildZoneId{};
		bool grant{};
		std::string targetProfileId;
	};

	struct DestroyedStructure
	{
		Workshop::ObjectId objectId{};
		std::string pieceType;
		std::uint32_t rebuildComponents{};
	};

	[[nodiscard]] std::optional<Workshop::BuildZoneId> NormalizeGet(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Update> NormalizeUpdate(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeState(
		const SettlementAccess::Settlement& a_settlement,
		const std::vector<DestroyedStructure>& a_destroyedStructures = {});
	[[nodiscard]] nlohmann::json MakeReject(Workshop::BuildZoneId a_buildZoneId, SettlementAccess::ApplyStatus a_status);
	[[nodiscard]] std::string_view StatusName(SettlementAccess::ApplyStatus a_status) noexcept;
	[[nodiscard]] nlohmann::json MakeStoredJson(const std::vector<SettlementAccess::Settlement>& a_settlements);
	[[nodiscard]] std::optional<std::vector<SettlementAccess::Settlement>> NormalizeStored(const nlohmann::json& a_stored);
}
