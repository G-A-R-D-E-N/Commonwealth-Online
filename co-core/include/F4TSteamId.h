#pragma once

#include <charconv>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace F4T::SteamId
{
	inline constexpr std::uint64_t kIndividualBase = 76561197960265728ull;
	inline constexpr std::uint64_t kIndividualMaximum = kIndividualBase + 0xFFFFFFFFull;
	inline constexpr std::size_t kDecimalLength = 17;
	inline constexpr std::size_t kMaximumTicketBytes = 1024;

	[[nodiscard]] constexpr bool IsIndividual(std::uint64_t a_steamId) noexcept
	{
		return a_steamId > kIndividualBase && a_steamId <= kIndividualMaximum;
	}

	[[nodiscard]] inline std::optional<std::uint64_t> ParseIndividual(std::string_view a_text) noexcept
	{
		if (a_text.size() != kDecimalLength) {
			return std::nullopt;
		}
		std::uint64_t value{};
		const auto* const end = a_text.data() + a_text.size();
		const auto [parsed, error] = std::from_chars(a_text.data(), end, value);
		if (error != std::errc{} || parsed != end || !IsIndividual(value)) {
			return std::nullopt;
		}
		return value;
	}

	// A session ticket travels as uppercase or lowercase hex: non-empty, even length, at most kMaximumTicketBytes.
	[[nodiscard]] constexpr bool IsTicketHex(std::string_view a_text) noexcept
	{
		if (a_text.empty() || a_text.size() % 2 != 0 || a_text.size() > kMaximumTicketBytes * 2) {
			return false;
		}
		for (const char character : a_text) {
			const bool digit = character >= '0' && character <= '9';
			const bool letter = (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
			if (!digit && !letter) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] inline std::string TicketHex(const std::uint8_t* a_bytes, std::size_t a_size)
	{
		constexpr char kDigits[] = "0123456789ABCDEF";
		std::string text;
		text.reserve(a_size * 2);
		for (std::size_t index = 0; index < a_size; ++index) {
			text.push_back(kDigits[a_bytes[index] >> 4]);
			text.push_back(kDigits[a_bytes[index] & 0x0F]);
		}
		return text;
	}
}
