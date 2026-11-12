#pragma once

#include "F4TQuestLogic.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestFormKey
{
	using LoadOrder = std::vector<QuestKey::LoadOrderFile>;

	[[nodiscard]] std::size_t GetLoadOrderStamp();
	[[nodiscard]] LoadOrder GetLoadOrder();
	[[nodiscard]] std::optional<std::string> ToKey(std::uint32_t a_formId, const LoadOrder& a_loadOrder);
	[[nodiscard]] std::optional<std::uint32_t> ToFormId(std::string_view a_key, const LoadOrder& a_loadOrder);
}
