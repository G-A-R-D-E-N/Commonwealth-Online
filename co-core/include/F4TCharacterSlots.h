#pragma once

#include "F4TIdentityProtocol.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::CharacterSlots
{
	inline constexpr std::size_t DefaultCharacterSlots = 3;
	inline constexpr std::size_t MinimumCharacterSlots = 1;
	inline constexpr std::size_t MaximumCharacterSlots = 8;
	inline constexpr char SlotKeySeparator = '#';
	inline constexpr std::uint32_t FirstSlot = 0;

	[[nodiscard]] std::optional<std::string> AccountKey(std::string_view a_profileId);
	[[nodiscard]] bool IsValidSlotCount(std::size_t a_count) noexcept;
	[[nodiscard]] bool IsValidSlot(std::uint32_t a_slot, std::size_t a_slotCount) noexcept;
	[[nodiscard]] std::string SlotProfileKey(std::string_view a_account, std::uint32_t a_slot);
	[[nodiscard]] std::optional<std::pair<std::string, std::uint32_t>> ParseSlotKey(std::string_view a_key);

	struct MigrationResult
	{
		std::size_t moved{};
		std::size_t alreadySlotted{};
		std::size_t skipped{};
		bool conflict{};
		std::string conflictKey;
	};

	[[nodiscard]] MigrationResult MigrateLegacyProfiles(nlohmann::json& a_root, double a_updatedAt);
	[[nodiscard]] std::map<std::uint32_t, nlohmann::json> GetSlotRecords(
		const nlohmann::json& a_root,
		std::string_view a_account);
	[[nodiscard]] bool SetSlotRecord(
		nlohmann::json& a_root,
		std::string_view a_account,
		std::uint32_t a_slot,
		const nlohmann::json& a_record);
	[[nodiscard]] bool EraseSlotRecord(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot);
	[[nodiscard]] bool EraseProfileNode(nlohmann::json& a_root, std::string_view a_profileKey);
}
