#include "F4TWindowsCaseFold.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

namespace
{
	struct Range
	{
		std::uint32_t first;
		std::uint32_t last;
		std::uint32_t step;
		std::int32_t delta;
	};

	constexpr std::array<Range, 135> ranges{
	Range{ 0x41, 0x5A, 1, 32 },
	Range{ 0xC0, 0xD6, 1, 32 },
	Range{ 0xD8, 0xDE, 1, 32 },
	Range{ 0x100, 0x12E, 2, 1 },
	Range{ 0x132, 0x136, 2, 1 },
	Range{ 0x139, 0x147, 2, 1 },
	Range{ 0x14A, 0x176, 2, 1 },
	Range{ 0x178, 0x178, 1, -121 },
	Range{ 0x179, 0x17D, 2, 1 },
	Range{ 0x181, 0x181, 1, 210 },
	Range{ 0x182, 0x184, 2, 1 },
	Range{ 0x186, 0x186, 1, 206 },
	Range{ 0x187, 0x187, 1, 1 },
	Range{ 0x189, 0x18A, 1, 205 },
	Range{ 0x18B, 0x18B, 1, 1 },
	Range{ 0x18E, 0x18E, 1, 79 },
	Range{ 0x18F, 0x18F, 1, 202 },
	Range{ 0x190, 0x190, 1, 203 },
	Range{ 0x191, 0x191, 1, 1 },
	Range{ 0x193, 0x193, 1, 205 },
	Range{ 0x194, 0x194, 1, 207 },
	Range{ 0x196, 0x196, 1, 211 },
	Range{ 0x197, 0x197, 1, 209 },
	Range{ 0x198, 0x198, 1, 1 },
	Range{ 0x19C, 0x19C, 1, 211 },
	Range{ 0x19D, 0x19D, 1, 213 },
	Range{ 0x19F, 0x19F, 1, 214 },
	Range{ 0x1A0, 0x1A4, 2, 1 },
	Range{ 0x1A6, 0x1A6, 1, 218 },
	Range{ 0x1A7, 0x1A7, 1, 1 },
	Range{ 0x1A9, 0x1A9, 1, 218 },
	Range{ 0x1AC, 0x1AC, 1, 1 },
	Range{ 0x1AE, 0x1AE, 1, 218 },
	Range{ 0x1AF, 0x1AF, 1, 1 },
	Range{ 0x1B1, 0x1B2, 1, 217 },
	Range{ 0x1B3, 0x1B5, 2, 1 },
	Range{ 0x1B7, 0x1B7, 1, 219 },
	Range{ 0x1B8, 0x1B8, 1, 1 },
	Range{ 0x1BC, 0x1BC, 1, 1 },
	Range{ 0x1C4, 0x1C4, 1, 2 },
	Range{ 0x1C7, 0x1C7, 1, 2 },
	Range{ 0x1CA, 0x1CA, 1, 2 },
	Range{ 0x1CD, 0x1DB, 2, 1 },
	Range{ 0x1DE, 0x1EE, 2, 1 },
	Range{ 0x1F1, 0x1F1, 1, 2 },
	Range{ 0x1F4, 0x1F4, 1, 1 },
	Range{ 0x1F6, 0x1F6, 1, -97 },
	Range{ 0x1F7, 0x1F7, 1, -56 },
	Range{ 0x1F8, 0x21E, 2, 1 },
	Range{ 0x220, 0x220, 1, -130 },
	Range{ 0x222, 0x232, 2, 1 },
	Range{ 0x23A, 0x23A, 1, 10795 },
	Range{ 0x23B, 0x23B, 1, 1 },
	Range{ 0x23D, 0x23D, 1, -163 },
	Range{ 0x23E, 0x23E, 1, 10792 },
	Range{ 0x241, 0x241, 1, 1 },
	Range{ 0x243, 0x243, 1, -195 },
	Range{ 0x244, 0x244, 1, 69 },
	Range{ 0x245, 0x245, 1, 71 },
	Range{ 0x246, 0x24E, 2, 1 },
	Range{ 0x370, 0x372, 2, 1 },
	Range{ 0x376, 0x376, 1, 1 },
	Range{ 0x386, 0x386, 1, 38 },
	Range{ 0x388, 0x38A, 1, 37 },
	Range{ 0x38C, 0x38C, 1, 64 },
	Range{ 0x38E, 0x38F, 1, 63 },
	Range{ 0x391, 0x3A1, 1, 32 },
	Range{ 0x3A3, 0x3AB, 1, 32 },
	Range{ 0x3CF, 0x3CF, 1, 8 },
	Range{ 0x3D8, 0x3EE, 2, 1 },
	Range{ 0x3F7, 0x3F7, 1, 1 },
	Range{ 0x3F9, 0x3F9, 1, -7 },
	Range{ 0x3FA, 0x3FA, 1, 1 },
	Range{ 0x3FD, 0x3FF, 1, -130 },
	Range{ 0x400, 0x40F, 1, 80 },
	Range{ 0x410, 0x42F, 1, 32 },
	Range{ 0x460, 0x480, 2, 1 },
	Range{ 0x48A, 0x4BE, 2, 1 },
	Range{ 0x4C0, 0x4C0, 1, 15 },
	Range{ 0x4C1, 0x4CD, 2, 1 },
	Range{ 0x4D0, 0x522, 2, 1 },
	Range{ 0x531, 0x556, 1, 48 },
	Range{ 0x10A0, 0x10C5, 1, 7264 },
	Range{ 0x1E00, 0x1E94, 2, 1 },
	Range{ 0x1EA0, 0x1EFE, 2, 1 },
	Range{ 0x1F08, 0x1F0F, 1, -8 },
	Range{ 0x1F18, 0x1F1D, 1, -8 },
	Range{ 0x1F28, 0x1F2F, 1, -8 },
	Range{ 0x1F38, 0x1F3F, 1, -8 },
	Range{ 0x1F48, 0x1F4D, 1, -8 },
	Range{ 0x1F59, 0x1F5F, 2, -8 },
	Range{ 0x1F68, 0x1F6F, 1, -8 },
	Range{ 0x1F88, 0x1F8F, 1, -8 },
	Range{ 0x1F98, 0x1F9F, 1, -8 },
	Range{ 0x1FA8, 0x1FAF, 1, -8 },
	Range{ 0x1FB8, 0x1FB9, 1, -8 },
	Range{ 0x1FBA, 0x1FBB, 1, -74 },
	Range{ 0x1FBC, 0x1FBC, 1, -9 },
	Range{ 0x1FC8, 0x1FCB, 1, -86 },
	Range{ 0x1FCC, 0x1FCC, 1, -9 },
	Range{ 0x1FD8, 0x1FD9, 1, -8 },
	Range{ 0x1FDA, 0x1FDB, 1, -100 },
	Range{ 0x1FE8, 0x1FE9, 1, -8 },
	Range{ 0x1FEA, 0x1FEB, 1, -112 },
	Range{ 0x1FEC, 0x1FEC, 1, -7 },
	Range{ 0x1FF8, 0x1FF9, 1, -128 },
	Range{ 0x1FFA, 0x1FFB, 1, -126 },
	Range{ 0x1FFC, 0x1FFC, 1, -9 },
	Range{ 0x2132, 0x2132, 1, 28 },
	Range{ 0x2160, 0x216F, 1, 16 },
	Range{ 0x2183, 0x2183, 1, 1 },
	Range{ 0x24B6, 0x24CF, 1, 26 },
	Range{ 0x2C00, 0x2C2E, 1, 48 },
	Range{ 0x2C60, 0x2C60, 1, 1 },
	Range{ 0x2C62, 0x2C62, 1, -10743 },
	Range{ 0x2C63, 0x2C63, 1, -3814 },
	Range{ 0x2C64, 0x2C64, 1, -10727 },
	Range{ 0x2C67, 0x2C6B, 2, 1 },
	Range{ 0x2C6D, 0x2C6D, 1, -10780 },
	Range{ 0x2C6E, 0x2C6E, 1, -10749 },
	Range{ 0x2C6F, 0x2C6F, 1, -10783 },
	Range{ 0x2C72, 0x2C72, 1, 1 },
	Range{ 0x2C75, 0x2C75, 1, 1 },
	Range{ 0x2C80, 0x2CE2, 2, 1 },
	Range{ 0xA640, 0xA65E, 2, 1 },
	Range{ 0xA662, 0xA66C, 2, 1 },
	Range{ 0xA680, 0xA696, 2, 1 },
	Range{ 0xA722, 0xA72E, 2, 1 },
	Range{ 0xA732, 0xA76E, 2, 1 },
	Range{ 0xA779, 0xA77B, 2, 1 },
	Range{ 0xA77D, 0xA77D, 1, -35332 },
	Range{ 0xA77E, 0xA786, 2, 1 },
	Range{ 0xA78B, 0xA78B, 1, 1 },
	Range{ 0xFF21, 0xFF3A, 1, 32 },
	Range{ 0x10400, 0x10427, 1, 40 },
	};

	consteval bool ValidRanges()
	{
		for (std::size_t index = 0; index < ranges.size(); ++index) {
			const auto& range = ranges[index];
			if (range.first > range.last || range.step == 0 || (range.last - range.first) % range.step != 0 ||
				(index != 0 && ranges[index - 1].last >= range.first)) return false;
		}
		return true;
	}
	static_assert(ValidRanges());

	std::uint32_t Lower(std::uint32_t a_code)
	{
		const auto next = std::upper_bound(ranges.begin(), ranges.end(), a_code,
			[](std::uint32_t code, const Range& range) { return code < range.first; });
		if (next == ranges.begin()) return a_code;
		const auto& range = *std::prev(next);
		if (a_code > range.last || (a_code - range.first) % range.step != 0) return a_code;
		return static_cast<std::uint32_t>(static_cast<std::int64_t>(a_code) + range.delta);
	}

	void Append(std::string& a_output, std::uint32_t a_code)
	{
		if (a_code <= 0x7f) a_output += static_cast<char>(a_code);
		else if (a_code <= 0x7ff) {
			a_output += static_cast<char>(0xc0 | (a_code >> 6));
			a_output += static_cast<char>(0x80 | (a_code & 0x3f));
		} else if (a_code <= 0xffff) {
			a_output += static_cast<char>(0xe0 | (a_code >> 12));
			a_output += static_cast<char>(0x80 | ((a_code >> 6) & 0x3f));
			a_output += static_cast<char>(0x80 | (a_code & 0x3f));
		} else {
			a_output += static_cast<char>(0xf0 | (a_code >> 18));
			a_output += static_cast<char>(0x80 | ((a_code >> 12) & 0x3f));
			a_output += static_cast<char>(0x80 | ((a_code >> 6) & 0x3f));
			a_output += static_cast<char>(0x80 | (a_code & 0x3f));
		}
	}
}

namespace F4T::WindowsCaseFold
{
	std::optional<std::string> Key(std::string_view a_text)
	{
		std::string result;
		result.reserve(a_text.size());
		bool nonAscii = false;
		for (std::size_t i = 0; i < a_text.size();) {
			const auto first = static_cast<unsigned char>(a_text[i++]);
			std::uint32_t code = first;
			std::size_t count = 0;
			std::uint32_t minimum = 0;
			if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; count = 1; minimum = 0x80; }
			else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; count = 2; minimum = 0x800; }
			else if (first >= 0xf0 && first <= 0xf4) { code = first & 0x07; count = 3; minimum = 0x10000; }
			else if (first >= 0x80) return std::nullopt;
			if (i + count > a_text.size()) return std::nullopt;
			for (std::size_t n = 0; n < count; ++n) {
				const auto next = static_cast<unsigned char>(a_text[i++]);
				if ((next & 0xc0) != 0x80) return std::nullopt;
				code = (code << 6) | (next & 0x3f);
			}
			if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return std::nullopt;
			nonAscii = nonAscii || code >= 0x80;
			Append(result, Lower(code));
		}
#ifdef _WIN32
		if (nonAscii) {
			if (a_text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
			const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			if (length <= 0) return std::nullopt;
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), wide.data(), length) != length ||
				CharLowerBuffW(wide.data(), static_cast<DWORD>(length)) != static_cast<DWORD>(length)) return std::nullopt;
			const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, nullptr, 0, nullptr, nullptr);
			if (bytes <= 0) return std::nullopt;
			std::string actual(static_cast<std::size_t>(bytes), '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, actual.data(), bytes, nullptr, nullptr) != bytes || actual != result) return std::nullopt;
		}
#else
		(void)nonAscii;
#endif
		return result;
	}
}
