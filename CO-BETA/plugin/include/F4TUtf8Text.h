#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace F4T::Utf8
{
	namespace Detail
	{
		struct Sequence
		{
			std::size_t bytes{ 0 };
			bool valid{ false };
		};

		constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

		constexpr Sequence SequenceAt(std::string_view a_text, std::size_t a_offset) noexcept
		{
			if (a_offset >= a_text.size()) {
				return {};
			}

			const auto lead = static_cast<unsigned char>(a_text[a_offset]);
			if (lead < 0x80) {
				return { 1, true };
			}

			std::size_t length = 0;
			unsigned char secondMinimum = 0x80;
			unsigned char secondMaximum = 0xBF;
			if (lead >= 0xC2 && lead <= 0xDF) {
				length = 2;
			} else if (lead >= 0xE0 && lead <= 0xEF) {
				length = 3;
				if (lead == 0xE0) {
					secondMinimum = 0xA0;
				} else if (lead == 0xED) {
					secondMaximum = 0x9F;
				}
			} else if (lead >= 0xF0 && lead <= 0xF4) {
				length = 4;
				if (lead == 0xF0) {
					secondMinimum = 0x90;
				} else if (lead == 0xF4) {
					secondMaximum = 0x8F;
				}
			} else {
				return {};
			}

			if (a_text.size() - a_offset < length) {
				return {};
			}

			const auto second = static_cast<unsigned char>(a_text[a_offset + 1]);
			if (second < secondMinimum || second > secondMaximum) {
				return {};
			}

			for (std::size_t index = 2; index < length; ++index) {
				const auto continuation = static_cast<unsigned char>(a_text[a_offset + index]);
				if (continuation < 0x80 || continuation > 0xBF) {
					return {};
				}
			}

			return { length, true };
		}
	}

	[[nodiscard]] constexpr bool IsValid(std::string_view a_text) noexcept
	{
		for (std::size_t offset = 0; offset < a_text.size();) {
			const auto sequence = Detail::SequenceAt(a_text, offset);
			if (!sequence.valid) {
				return false;
			}
			offset += sequence.bytes;
		}
		return true;
	}

	[[nodiscard]] inline std::string TruncateToBytes(std::string_view a_text, std::size_t a_maximumBytes)
	{
		std::string text;
		text.reserve((std::min)(a_text.size(), a_maximumBytes));
		for (std::size_t offset = 0; offset < a_text.size();) {
			const auto sequence = Detail::SequenceAt(a_text, offset);
			const std::string_view encoded = sequence.valid ?
				a_text.substr(offset, sequence.bytes) :
				Detail::kReplacementCharacter;
			if (text.size() + encoded.size() > a_maximumBytes) {
				break;
			}
			text.append(encoded);
			offset += sequence.valid ? sequence.bytes : std::size_t{ 1 };
		}
		return text;
	}
}
