#include "F4TFactionValidation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::FactionValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMinimumNameLength = 3;
		constexpr std::size_t kMaximumNameLength = 24;
		constexpr std::size_t kMinimumTagLength = 2;
		constexpr std::size_t kMaximumTagLength = 4;
		constexpr std::size_t kMaximumIdentifierLength = 64;
		constexpr std::size_t kMaximumProfileIdLength = 128;
		constexpr std::size_t kMaximumDescriptionLength = 200;
		constexpr std::size_t kMaximumMotdLength = 1000;

		std::optional<std::string> ReadIdentifier(const Json& a_packet, std::string_view a_name, std::size_t a_maximumLength = kMaximumIdentifierLength)
		{
			const auto found = a_packet.find(a_name);
			if (found == a_packet.end() || !found->is_string()) {
				return std::nullopt;
			}
			auto value = found->get<std::string>();
			if (value.empty() || value.size() > a_maximumLength) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<std::uint32_t> ReadTargetPlayerId(const Json& a_packet)
		{
			const auto found = a_packet.find("targetPlayerId");
			if (found == a_packet.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			std::uint64_t value = 0;
			if (found->is_number_unsigned()) {
				value = found->get<std::uint64_t>();
			} else {
				const auto signedValue = found->get<std::int64_t>();
				if (signedValue < 0) {
					return std::nullopt;
				}
				value = static_cast<std::uint64_t>(signedValue);
			}
			if (value == 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::string_view TrimSpaces(std::string_view a_value)
		{
			const auto first = a_value.find_first_not_of(' ');
			if (first == std::string_view::npos) {
				return {};
			}
			return a_value.substr(first, a_value.find_last_not_of(' ') - first + 1);
		}

		constexpr std::array<std::string_view, 9> kBlockedTerms{
			"fuck", "shit", "nigger", "nigga", "faggot", "whore", "slut", "nazi", "hitler"
		};

		char FoldCharacter(char a_ch)
		{
			switch (a_ch) {
			case '0': return 'o';
			case '1': return 'i';
			case '3': return 'e';
			case '4': return 'a';
			case '5': return 's';
			case '7': return 't';
			case '$': return 's';
			case '@': return 'a';
			case '!': return 'i';
			case '+': return 't';
			case '6': return 'g';
			case '9': return 'g';
			case '|': return 'i';
			case '(': return 'c';
			default: return a_ch >= 'A' && a_ch <= 'Z' ? static_cast<char>(a_ch - 'A' + 'a') : a_ch;
			}
		}

		bool IsValidTag(std::string_view a_value)
		{
			return a_value.size() >= kMinimumTagLength && a_value.size() <= kMaximumTagLength &&
				std::all_of(a_value.begin(), a_value.end(), [](char a_ch) {
					return (a_ch >= 'A' && a_ch <= 'Z') || (a_ch >= '0' && a_ch <= '9');
				});
		}
	}

	std::optional<nlohmann::json> NormalizeFactionCreate(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto name = a_packet.find("name");
		if (name == a_packet.end() || !name->is_string()) {
			return std::nullopt;
		}
		Json normalized{ { "type", "factionCreate" }, { "name", name->get<std::string>() } };
		if (const auto tag = a_packet.find("tag"); tag != a_packet.end()) {
			if (!tag->is_string()) {
				return std::nullopt;
			}
			normalized["tag"] = tag->get<std::string>();
		}
		return normalized;
	}

	std::optional<std::string> NormalizeFactionName(std::string_view a_value)
	{
		const auto name = TrimSpaces(a_value);
		if (name.size() < kMinimumNameLength || name.size() > kMaximumNameLength || name.find("  ") != std::string_view::npos) {
			return std::nullopt;
		}
		for (const char ch : name) {
			const bool letterOrDigit = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
			if (!letterOrDigit && ch != ' ' && ch != '\'' && ch != '-') {
				return std::nullopt;
			}
		}
		return std::string{ name };
	}

	std::optional<std::string> NormalizeFactionTag(std::string_view a_value)
	{
		std::string tag{ TrimSpaces(a_value) };
		for (auto& ch : tag) {
			if (ch >= 'a' && ch <= 'z') {
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		if (tag.empty()) {
			return std::string{};
		}
		if (!IsValidTag(tag)) {
			return std::nullopt;
		}
		return tag;
	}

	bool IsFactionTextAllowed(std::string_view a_value)
	{
		std::string folded;
		for (const char ch : a_value) {
			const char mapped = FoldCharacter(ch);
			if (mapped >= 'a' && mapped <= 'z') {
				folded.push_back(mapped);
			}
		}
		return std::none_of(kBlockedTerms.begin(), kBlockedTerms.end(), [&](std::string_view a_term) {
			return folded.find(a_term) != std::string::npos;
		});
	}

	std::optional<nlohmann::json> NormalizeFactionInvite(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetPlayerId = ReadTargetPlayerId(a_packet);
		if (!targetPlayerId) {
			return std::nullopt;
		}
		return Json{ { "type", "factionInvite" }, { "targetPlayerId", *targetPlayerId } };
	}

	std::optional<nlohmann::json> NormalizeFactionInviteResponse(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto inviteId = ReadIdentifier(a_packet, "inviteId");
		const auto accept = a_packet.find("accept");
		if (!inviteId || accept == a_packet.end() || !accept->is_boolean()) {
			return std::nullopt;
		}
		return Json{ { "type", "factionInviteResponse" }, { "inviteId", *inviteId }, { "accept", accept->get<bool>() } };
	}

	std::optional<nlohmann::json> NormalizeFactionLeave(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		return Json{ { "type", "factionLeave" } };
	}

	std::optional<nlohmann::json> NormalizeFactionKick(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetProfileId = ReadIdentifier(a_packet, "targetProfileId", kMaximumProfileIdLength);
		if (!targetProfileId) {
			return std::nullopt;
		}
		return Json{ { "type", "factionKick" }, { "targetProfileId", *targetProfileId } };
	}

	std::optional<nlohmann::json> NormalizeFactionDisband(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		return Json{ { "type", "factionDisband" } };
	}

	std::optional<nlohmann::json> NormalizeFactionPromote(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetProfileId = ReadIdentifier(a_packet, "targetProfileId", kMaximumProfileIdLength);
		if (!targetProfileId) {
			return std::nullopt;
		}
		return Json{ { "type", "factionPromote" }, { "targetProfileId", *targetProfileId } };
	}

	std::optional<nlohmann::json> NormalizeFactionSetRank(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetProfileId = ReadIdentifier(a_packet, "targetProfileId", kMaximumProfileIdLength);
		const auto rank = ReadIdentifier(a_packet, "rank");
		if (!targetProfileId || !rank || (*rank != "officer" && *rank != "member")) {
			return std::nullopt;
		}
		return Json{ { "type", "factionSetRank" }, { "targetProfileId", *targetProfileId }, { "rank", *rank } };
	}

	namespace
	{
		std::optional<std::string> NormalizeProfileText(std::string_view a_value, std::size_t a_maximum)
		{
			const auto text = TrimSpaces(a_value);
			if (text.size() > a_maximum) {
				return std::nullopt;
			}
			for (const char ch : text) {
				if (ch < ' ' || ch > '~' || ch == '"' || ch == '\\') {
					return std::nullopt;
				}
			}
			return std::string{ text };
		}
	}

	std::optional<std::string> NormalizeFactionDescription(std::string_view a_value)
	{
		return NormalizeProfileText(a_value, kMaximumDescriptionLength);
	}

	std::optional<std::string> NormalizeFactionMotd(std::string_view a_value)
	{
		return NormalizeProfileText(a_value, kMaximumMotdLength);
	}

	std::optional<std::string> NormalizeFactionColor(std::string_view a_value)
	{
		if (a_value.empty()) {
			return std::string{};
		}
		if (a_value.size() != 7 || a_value.front() != '#') {
			return std::nullopt;
		}
		std::string color{ a_value };
		for (std::size_t index = 1; index < color.size(); ++index) {
			auto& ch = color[index];
			const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
			if (!hex) {
				return std::nullopt;
			}
			if (ch >= 'a' && ch <= 'f') {
				ch = static_cast<char>(ch - 'a' + 'A');
			}
		}
		return color;
	}

	std::optional<nlohmann::json> NormalizeFactionSetProfile(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto description = a_packet.find("description");
		const auto color = a_packet.find("color");
		const auto motd = a_packet.find("motd");
		if (description == a_packet.end() || !description->is_string() || color == a_packet.end() || !color->is_string()) {
			return std::nullopt;
		}
		const auto normalizedDescription = NormalizeFactionDescription(description->get<std::string>());
		const auto normalizedColor = NormalizeFactionColor(color->get<std::string>());
		if (!normalizedDescription || !normalizedColor) {
			return std::nullopt;
		}
		Json normalized{ { "type", "factionSetProfile" }, { "description", *normalizedDescription }, { "color", *normalizedColor } };
		if (motd != a_packet.end()) {
			if (!motd->is_string()) {
				return std::nullopt;
			}
			const auto normalizedMotd = NormalizeFactionMotd(motd->get<std::string>());
			if (!normalizedMotd) {
				return std::nullopt;
			}
			normalized["motd"] = *normalizedMotd;
		}
		return normalized;
	}

	std::optional<nlohmann::json> NormalizePlayerRosterRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		return Json{ { "type", "playerRosterRequest" } };
	}

	std::optional<nlohmann::json> NormalizeFactionListRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		return Json{ { "type", "factionList" } };
	}
}
