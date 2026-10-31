// source:CO-BETA/docs/protocol/chat.md
#include "F4TChatValidation.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ChatValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumChatCodePoints = 256;
		constexpr std::size_t kMaximumDisplayNameCodePoints = 32;
		constexpr std::size_t kMaximumConsecutiveCombiningMarks = 4;
		constexpr std::string_view kSpoofTagWords[] = {
			"discord", "system", "web", "server", "admin", "administrator", "staff", "support", "moderator", "mod",
			"owner", "official", "team", "helper", "developer", "dev", "bot", "gm", "cm", "root", "sysadmin"
		};

		std::optional<std::uint32_t> DecodeCodePoint(std::string_view a_text, std::size_t& a_index)
		{
			const auto lead = static_cast<std::uint8_t>(a_text[a_index]);
			if (lead < 0x80U) {
				a_index++;
				return lead;
			}
			std::size_t length = 0;
			std::uint32_t value = 0;
			std::uint32_t minimum = 0;
			if (lead >= 0xC2U && lead <= 0xDFU) {
				length = 2;
				value = lead & 0x1FU;
				minimum = 0x80U;
			} else if (lead >= 0xE0U && lead <= 0xEFU) {
				length = 3;
				value = lead & 0x0FU;
				minimum = 0x800U;
			} else if (lead >= 0xF0U && lead <= 0xF4U) {
				length = 4;
				value = lead & 0x07U;
				minimum = 0x10000U;
			} else {
				return std::nullopt;
			}
			if (a_index + length > a_text.size()) {
				return std::nullopt;
			}
			for (std::size_t offset = 1; offset < length; ++offset) {
				const auto continuation = static_cast<std::uint8_t>(a_text[a_index + offset]);
				if ((continuation & 0xC0U) != 0x80U) {
					return std::nullopt;
				}
				value = (value << 6U) | (continuation & 0x3FU);
			}
			if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
				return std::nullopt;
			}
			a_index += length;
			return value;
		}

		bool IsCollapsibleSpace(std::uint32_t a_codePoint)
		{
			return a_codePoint == 0x20U || a_codePoint == 0xA0U || a_codePoint == 0x1680U ||
				(a_codePoint >= 0x2000U && a_codePoint <= 0x200AU) || a_codePoint == 0x202FU ||
				a_codePoint == 0x205FU || a_codePoint == 0x3000U;
		}

		bool IsStrippedCodePoint(std::uint32_t a_codePoint)
		{
			return a_codePoint < 0x20U || (a_codePoint >= 0x7FU && a_codePoint <= 0x9FU) ||
				(a_codePoint >= 0x200BU && a_codePoint <= 0x200FU) || (a_codePoint >= 0x202AU && a_codePoint <= 0x202EU) ||
				(a_codePoint >= 0x2060U && a_codePoint <= 0x2064U) || (a_codePoint >= 0x2066U && a_codePoint <= 0x2069U) ||
				(a_codePoint >= 0x180BU && a_codePoint <= 0x180FU) || (a_codePoint >= 0xFFF9U && a_codePoint <= 0xFFFBU) ||
				(a_codePoint >= 0xE0000U && a_codePoint <= 0xE0FFFU) || a_codePoint == 0x061CU || a_codePoint == 0x00ADU ||
				a_codePoint == 0x034FU || a_codePoint == 0x115FU || a_codePoint == 0x1160U || a_codePoint == 0x17B4U ||
				a_codePoint == 0x17B5U || a_codePoint == 0x2028U || a_codePoint == 0x2029U || a_codePoint == 0x2800U ||
				a_codePoint == 0x3164U || a_codePoint == 0xFFA0U || a_codePoint == 0xFEFFU ||
				(a_codePoint >= 0x206AU && a_codePoint <= 0x206FU) || (a_codePoint >= 0x1D173U && a_codePoint <= 0x1D17AU) ||
				(a_codePoint >= 0x13430U && a_codePoint <= 0x1343FU) || (a_codePoint >= 0x1BCA0U && a_codePoint <= 0x1BCA3U) ||
				(a_codePoint >= 0x0600U && a_codePoint <= 0x0605U) || a_codePoint == 0x06DDU || a_codePoint == 0x070FU ||
				(a_codePoint >= 0x0890U && a_codePoint <= 0x0891U) || a_codePoint == 0x08E2U || a_codePoint == 0x110BDU ||
				a_codePoint == 0x110CDU ||
				a_codePoint == 0x2065U || (a_codePoint >= 0xFFF0U && a_codePoint <= 0xFFF8U) || a_codePoint == 0x16FE4U;
		}

		bool IsVariationSelector(std::uint32_t a_codePoint)
		{
			return (a_codePoint >= 0xFE00U && a_codePoint <= 0xFE0FU) || (a_codePoint >= 0xE0100U && a_codePoint <= 0xE01EFU);
		}

		bool IsCombiningMark(std::uint32_t a_codePoint)
		{
			return (a_codePoint >= 0x0300U && a_codePoint <= 0x036FU) || (a_codePoint >= 0x1AB0U && a_codePoint <= 0x1AFFU) ||
				(a_codePoint >= 0x1DC0U && a_codePoint <= 0x1DFFU) || (a_codePoint >= 0x20D0U && a_codePoint <= 0x20FFU) ||
				(a_codePoint >= 0xFE20U && a_codePoint <= 0xFE2FU) || IsVariationSelector(a_codePoint) ||
				(a_codePoint >= 0x0483U && a_codePoint <= 0x0489U) || (a_codePoint >= 0x0591U && a_codePoint <= 0x05BDU) ||
				a_codePoint == 0x05BFU || (a_codePoint >= 0x05C1U && a_codePoint <= 0x05C2U) || (a_codePoint >= 0x05C4U && a_codePoint <= 0x05C5U) ||
				a_codePoint == 0x05C7U || (a_codePoint >= 0x0610U && a_codePoint <= 0x061AU) || (a_codePoint >= 0x064BU && a_codePoint <= 0x065FU) ||
				a_codePoint == 0x0E31U || (a_codePoint >= 0x0E34U && a_codePoint <= 0x0E3AU) || (a_codePoint >= 0x0E47U && a_codePoint <= 0x0E4EU) ||
				a_codePoint == 0x0EB1U || (a_codePoint >= 0x0EB4U && a_codePoint <= 0x0EBCU) || (a_codePoint >= 0x0EC8U && a_codePoint <= 0x0ECDU);
		}

		std::optional<std::string> NormalizeText(std::string_view a_text, bool a_stripVariationSelectors)
		{
			std::string output;
			bool pendingSpace = false;
			bool hasBase = false;
			std::size_t combiningMarks = 0;
			for (std::size_t index = 0; index < a_text.size();) {
				const auto start = index;
				const auto codePoint = DecodeCodePoint(a_text, index);
				if (!codePoint) {
					return std::nullopt;
				}
				if (IsCollapsibleSpace(*codePoint)) {
					pendingSpace = !output.empty();
					combiningMarks = 0;
					continue;
				}
				if (IsStrippedCodePoint(*codePoint) || (a_stripVariationSelectors && IsVariationSelector(*codePoint))) {
					continue;
				}
				if (IsCombiningMark(*codePoint)) {
					if (combiningMarks >= kMaximumConsecutiveCombiningMarks) {
						continue;
					}
					combiningMarks++;
				} else {
					combiningMarks = 0;
					hasBase = true;
				}
				if (pendingSpace) {
					output.push_back(' ');
					pendingSpace = false;
				}
				output.append(a_text.substr(start, index - start));
			}
			if (!hasBase) {
				return std::string{};
			}
			return output;
		}

		std::string DropCombiningMarks(std::string_view a_text)
		{
			std::string output;
			for (std::size_t index = 0; index < a_text.size();) {
				const auto start = index;
				const auto codePoint = DecodeCodePoint(a_text, index);
				if (!codePoint) {
					break;
				}
				if (!IsCombiningMark(*codePoint)) {
					output.append(a_text.substr(start, index - start));
				}
			}
			return output;
		}

		std::size_t CodePointCount(std::string_view a_text)
		{
			return static_cast<std::size_t>(std::count_if(a_text.begin(), a_text.end(), [](char a_byte) {
				return (static_cast<std::uint8_t>(a_byte) & 0xC0U) != 0x80U;
			}));
		}

		std::size_t SkipSpaces(std::string_view a_text, std::size_t a_index)
		{
			while (a_index < a_text.size() && a_text[a_index] == ' ') {
				a_index++;
			}
			return a_index;
		}

		bool IsOpenBracket(char a_byte)
		{
			return a_byte == '[' || a_byte == '(' || a_byte == '{' || a_byte == '<';
		}

		bool IsCloseBracket(char a_byte)
		{
			return a_byte == ']' || a_byte == ')' || a_byte == '}' || a_byte == '>';
		}

		std::size_t SkipBracketRun(std::string_view a_text, std::size_t a_index, bool a_open)
		{
			a_index = SkipSpaces(a_text, a_index);
			while (a_index < a_text.size() && (a_open ? IsOpenBracket(a_text[a_index]) : IsCloseBracket(a_text[a_index]))) {
				a_index = SkipSpaces(a_text, a_index + 1);
			}
			return a_index;
		}

		bool EqualsIgnoreCaseAscii(std::string_view a_text, std::string_view a_word)
		{
			return std::equal(a_text.begin(), a_text.end(), a_word.begin(), a_word.end(), [](char a_lhs, char a_rhs) {
				return std::tolower(static_cast<unsigned char>(a_lhs)) == a_rhs;
			});
		}

		std::optional<std::size_t> MatchSpoofTag(std::string_view a_name)
		{
			const auto start = SkipSpaces(a_name, 0);
			if (start >= a_name.size() || !IsOpenBracket(a_name[start])) {
				return std::nullopt;
			}
			const auto index = SkipBracketRun(a_name, start, true);
			for (const auto word : kSpoofTagWords) {
				if (a_name.size() - index < word.size() || !EqualsIgnoreCaseAscii(a_name.substr(index, word.size()), word)) {
					continue;
				}
				const auto end = SkipSpaces(a_name, index + word.size());
				if (end < a_name.size() && IsCloseBracket(a_name[end])) {
					return SkipBracketRun(a_name, end, false);
				}
			}
			return std::nullopt;
		}
	}

	ChatSendResult NormalizeChatSend(const Json& a_packet)
	{
		const auto channel = a_packet.find("channel");
		const auto text = a_packet.find("text");
		if (channel == a_packet.end() || text == a_packet.end() || !channel->is_string() || !text->is_string()) {
			return { ChatSendError::Malformed, {}, {} };
		}
		const auto channelName = channel->get<std::string>();
		if (channelName != "global" && channelName != "faction") {
			return { ChatSendError::ChannelUnavailable, {}, {} };
		}
		const auto normalized = NormalizeText(text->get<std::string>(), false);
		if (!normalized || normalized->empty()) {
			return { ChatSendError::InvalidText, {}, {} };
		}
		if (CodePointCount(*normalized) > kMaximumChatCodePoints) {
			return { ChatSendError::TooLong, {}, {} };
		}
		return { std::nullopt, channelName, *normalized };
	}

	std::string SanitizeChatDisplayName(std::string_view a_name)
	{
		auto name = NormalizeText(a_name, true).value_or(std::string{});
		while (const auto consumed = MatchSpoofTag(name)) {
			name.erase(0, *consumed);
		}
		std::size_t index = 0;
		for (std::size_t count = 0; count < kMaximumDisplayNameCodePoints && index < name.size(); ++count) {
			if (!DecodeCodePoint(name, index)) {
				break;
			}
		}
		name.resize(index);
		name = NormalizeText(name, true).value_or(std::string{});
		const auto bare = NormalizeText(DropCombiningMarks(name), true).value_or(std::string{});
		for (const auto word : kSpoofTagWords) {
			if (EqualsIgnoreCaseAscii(bare, word)) {
				return std::string{ "Player" };
			}
		}
		return name.empty() ? std::string{ "Player" } : name;
	}
}
