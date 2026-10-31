#pragma once

#include "F4TSettlementAccess.h"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementAccessProtocol
{
	struct Update
	{
		Workshop::BuildZoneId buildZoneId{};
		bool grant{};
		std::string targetProfileId;
	};

	[[nodiscard]] std::optional<Workshop::BuildZoneId> NormalizeGet(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Update> NormalizeUpdate(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeState(const SettlementAccess::Settlement& a_settlement);
	[[nodiscard]] nlohmann::json MakeReject(Workshop::BuildZoneId a_buildZoneId, SettlementAccess::ApplyStatus a_status);
	[[nodiscard]] std::string_view StatusName(SettlementAccess::ApplyStatus a_status) noexcept;
	[[nodiscard]] nlohmann::json MakeStoredJson(const std::vector<SettlementAccess::Settlement>& a_settlements);
	[[nodiscard]] std::optional<std::vector<SettlementAccess::Settlement>> NormalizeStored(const nlohmann::json& a_stored);
}
