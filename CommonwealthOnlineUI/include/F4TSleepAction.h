#pragma once

#include "F4TSleepView.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::SleepView
{
	struct ParsedAction
	{
		Action action;
		std::uint32_t hours;
	};

	inline std::optional<ParsedAction> ParseAction(std::string_view a_payload, Phase a_phase, std::uint32_t a_token, std::uint32_t a_maxHours)
	{
		const auto value = nlohmann::json::parse(a_payload, nullptr, false);
		if (value.is_discarded() || !value.is_object() || a_phase == Phase::Closed ||
			!value.contains("op") || !value["op"].is_string() ||
			!value.contains("token") || !value["token"].is_number_unsigned() ||
			!value.contains("hours") || !value["hours"].is_number_unsigned()) return std::nullopt;
		const auto token = value["token"].get<std::uint64_t>();
		const auto hours = value["hours"].get<std::uint64_t>();
		if (token != a_token || hours < 1 || hours > a_maxHours) return std::nullopt;
		const auto& op = value["op"].get_ref<const std::string&>();
		if (op == "sleep" && a_phase == Phase::Choose) return ParsedAction{ Action::Sleep, static_cast<std::uint32_t>(hours) };
		if (op == "cancel") return ParsedAction{ Action::Cancel, static_cast<std::uint32_t>(hours) };
		return std::nullopt;
	}
}
