#include "F4TStateBagProtocol.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::StateBagProtocol
{
	namespace
	{
		bool IsKeyCharacter(char a_character) noexcept
		{
			return a_character >= 0x21 && a_character <= 0x7E;
		}

		std::optional<std::uint32_t> ParsePlayerId(std::string_view a_digits)
		{
			if (a_digits.empty() || (a_digits.size() > 1 && a_digits.front() == '0') || a_digits.front() < '0' || a_digits.front() > '9') {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			const auto result = std::from_chars(a_digits.data(), a_digits.data() + a_digits.size(), value);
			if (result.ec != std::errc{} || result.ptr != a_digits.data() + a_digits.size()) {
				return std::nullopt;
			}
			return value;
		}

		bool IsResourceNameCharacter(char a_character) noexcept
		{
			return (a_character >= 'a' && a_character <= 'z') || (a_character >= '0' && a_character <= '9') ||
				a_character == '_' || a_character == '-';
		}

		bool IsViewSegmentCharacter(char a_character) noexcept
		{
			return (a_character >= 'a' && a_character <= 'z') || (a_character >= '0' && a_character <= '9') ||
				a_character == '_' || a_character == '-' || a_character == '.';
		}

		constexpr std::array<std::string_view, 7> kReservedDeviceNames{ "con", "prn", "aux", "nul", "conin$", "conout$", "clock$" };

		bool IsReservedDeviceStem(std::string_view a_stem) noexcept
		{
			for (const auto reserved : kReservedDeviceNames) {
				if (a_stem == reserved) {
					return true;
				}
			}
			if (a_stem.size() == 4 && a_stem[3] >= '0' && a_stem[3] <= '9') {
				const auto prefix = a_stem.substr(0, 3);
				return prefix == "com" || prefix == "lpt";
			}
			return false;
		}
	}

	BagId PlayerBag(std::uint32_t a_playerId) noexcept
	{
		return BagId{ BagKind::Player, a_playerId };
	}

	std::string FormatBag(const BagId& a_bag)
	{
		if (a_bag.kind == BagKind::Global) {
			return std::string{ kGlobalBagName };
		}
		return std::string{ kPlayerBagPrefix } + std::to_string(a_bag.playerId);
	}

	std::optional<BagId> ParseBag(std::string_view a_name)
	{
		if (a_name == kGlobalBagName) {
			return BagId{};
		}
		if (!a_name.starts_with(kPlayerBagPrefix)) {
			return std::nullopt;
		}
		const auto playerId = ParsePlayerId(a_name.substr(kPlayerBagPrefix.size()));
		if (!playerId) {
			return std::nullopt;
		}
		return PlayerBag(*playerId);
	}

	bool IsValidKey(std::string_view a_key) noexcept
	{
		if (a_key.empty() || a_key.size() > kMaximumKeyChars) {
			return false;
		}
		for (const char character : a_key) {
			if (!IsKeyCharacter(character)) {
				return false;
			}
		}
		return true;
	}

	bool IsValidValue(const nlohmann::json& a_value)
	{
		try {
			(void)a_value.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
			return true;
		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}

	std::size_t EncodedSize(const nlohmann::json& a_value)
	{
		return a_value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size();
	}

	bool IsValidResourceName(std::string_view a_name) noexcept
	{
		if (a_name.empty() || a_name.size() > kMaximumResourceNameChars) {
			return false;
		}
		for (const char character : a_name) {
			if (!IsResourceNameCharacter(character)) {
				return false;
			}
		}
		return true;
	}

	bool IsValidViewPathSegment(std::string_view a_segment) noexcept
	{
		if (a_segment.empty() || a_segment.front() == '.' || a_segment.back() == '.') {
			return false;
		}
		for (const char character : a_segment) {
			if (!IsViewSegmentCharacter(character)) {
				return false;
			}
		}
		return !IsReservedDeviceStem(a_segment.substr(0, a_segment.find('.')));
	}

	bool IsValidViewPath(std::string_view a_path) noexcept
	{
		if (a_path.empty() || a_path.size() > kMaximumViewPathChars || a_path.front() == '/' || !a_path.ends_with(".html")) {
			return false;
		}
		std::size_t start = 0;
		for (;;) {
			const auto next = a_path.find('/', start);
			const auto segment = a_path.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start);
			if (!IsValidViewPathSegment(segment)) {
				return false;
			}
			if (next == std::string_view::npos) {
				return true;
			}
			start = next + 1;
		}
	}

	bool IsValidResourceData(const nlohmann::json& a_data)
	{
		return EncodedSize(a_data) <= kMaximumResourceDataBytes;
	}
}
