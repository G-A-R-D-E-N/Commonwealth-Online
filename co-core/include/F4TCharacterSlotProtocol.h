#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::CharacterSlotProtocol
{
	inline constexpr std::size_t kMaximumSlotNameLength = 48;
	inline constexpr std::size_t kMinimumSlotNameLength = 1;

	struct Create
	{
		std::uint32_t slot{};
		std::string name;
	};

	struct SlotDelete
	{
		std::uint32_t slot{};
		std::string confirmToken;
		bool hasConfirmToken{};
	};

	[[nodiscard]] std::optional<std::uint32_t> NormalizeSlot(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Create> NormalizeCreate(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<SlotDelete> NormalizeDelete(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeName(std::string_view a_name);
	[[nodiscard]] bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept;
	[[nodiscard]] nlohmann::json MakeResult(std::string_view a_op, bool a_ok, std::string_view a_reason, std::uint32_t a_slot, bool a_hasSlot);
	[[nodiscard]] nlohmann::json MakeDeleteChallenge(std::uint32_t a_slot, std::string_view a_confirmToken, std::uint32_t a_expiresInSeconds);
}
