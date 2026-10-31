#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace F4T::FactionValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionCreate(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeFactionName(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> NormalizeFactionTag(std::string_view a_value);
	[[nodiscard]] bool IsFactionTextAllowed(std::string_view a_value);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionInvite(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionInviteResponse(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionLeave(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionKick(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionDisband(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionPromote(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionSetRank(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionSetProfile(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeFactionDescription(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> NormalizeFactionMotd(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> NormalizeFactionColor(std::string_view a_value);
	[[nodiscard]] std::optional<nlohmann::json> NormalizePlayerRosterRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeFactionListRequest(const nlohmann::json& a_packet);
}
