#include "ClaimedText.h"

#include <cstddef>
#include <cstdint>

namespace CommonwealthOnline::Server
{
	namespace
	{
		bool IsBlockedCodePoint(std::uint32_t a_codePoint) noexcept
		{
			return (a_codePoint >= 0x80U && a_codePoint <= 0x9FU) || a_codePoint == 0xADU || a_codePoint == 0x61CU ||
				a_codePoint == 0x180EU || (a_codePoint >= 0x200BU && a_codePoint <= 0x200FU) ||
				(a_codePoint >= 0x2028U && a_codePoint <= 0x202EU) || (a_codePoint >= 0x2060U && a_codePoint <= 0x206FU) ||
				a_codePoint == 0xFEFFU || (a_codePoint >= 0xE0000U && a_codePoint <= 0xE007FU);
		}
	}

	bool IsSafeClaimedText(std::string_view a_text) noexcept
	{
		std::size_t index = 0;
		while (index < a_text.size()) {
			const auto lead = static_cast<unsigned char>(a_text[index]);
			std::size_t length = 1;
			std::uint32_t minimum = 0;
			std::uint32_t codePoint = lead;
			if (lead >= 0xC2U && lead <= 0xDFU) {
				length = 2;
				minimum = 0x80U;
				codePoint = lead & 0x1FU;
			} else if (lead >= 0xE0U && lead <= 0xEFU) {
				length = 3;
				minimum = 0x800U;
				codePoint = lead & 0x0FU;
			} else if (lead >= 0xF0U && lead <= 0xF4U) {
				length = 4;
				minimum = 0x10000U;
				codePoint = lead & 0x07U;
			} else if (lead >= 0x80U) {
				return false;
			}
			if (index + length > a_text.size()) {
				return false;
			}
			for (std::size_t offset = 1; offset < length; ++offset) {
				const auto continuation = static_cast<unsigned char>(a_text[index + offset]);
				if ((continuation & 0xC0U) != 0x80U) {
					return false;
				}
				codePoint = (codePoint << 6U) | (continuation & 0x3FU);
			}
			if (codePoint < minimum || codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
				IsBlockedCodePoint(codePoint)) {
				return false;
			}
			index += length;
		}
		return true;
	}
}
