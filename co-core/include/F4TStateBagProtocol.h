#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::StateBagProtocol
{
	inline constexpr std::string_view kCapability = "state-bag-v1";
	inline constexpr std::string_view kPacketType = "stateBag";
	inline constexpr std::size_t kMaximumKeyChars = 64;
	inline constexpr std::size_t kMaximumValueBytes = 4096;
	inline constexpr std::size_t kMaximumKeysPerBag = 256;
	inline constexpr std::size_t kMaximumBagBytes = 65536;
	inline constexpr std::string_view kGlobalBagName = "global";
	inline constexpr std::string_view kPlayerBagPrefix = "player:";
	inline constexpr std::string_view kResourceViewPacketType = "resourceView";
	inline constexpr std::string_view kResourceMessagePacketType = "resourceMessage";
	inline constexpr std::size_t kMaximumResourceNameChars = 64;
	inline constexpr std::size_t kMaximumViewPathChars = 128;
	inline constexpr std::size_t kMaximumResourceDataBytes = 1024;
	inline constexpr double kMaximumResourceMessagesPerSecond = 20.0;

	enum class BagKind : std::uint8_t
	{
		Global,
		Player
	};

	struct BagId
	{
		BagKind kind{ BagKind::Global };
		std::uint32_t playerId{};

		[[nodiscard]] bool operator==(const BagId&) const noexcept = default;
	};

	[[nodiscard]] BagId PlayerBag(std::uint32_t a_playerId) noexcept;
	[[nodiscard]] std::string FormatBag(const BagId& a_bag);
	[[nodiscard]] std::optional<BagId> ParseBag(std::string_view a_name);
	[[nodiscard]] bool IsValidKey(std::string_view a_key) noexcept;
	[[nodiscard]] bool IsValidValue(const nlohmann::json& a_value);
	[[nodiscard]] std::size_t EncodedSize(const nlohmann::json& a_value);
	[[nodiscard]] bool IsValidResourceName(std::string_view a_name) noexcept;
	[[nodiscard]] bool IsValidViewPathSegment(std::string_view a_segment) noexcept;
	[[nodiscard]] bool IsValidViewPath(std::string_view a_path) noexcept;
	[[nodiscard]] bool IsValidResourceData(const nlohmann::json& a_data);
}
