#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::Factions::Payload
{
	inline constexpr std::uint32_t kMaximumInviteSeconds = 3600;
	inline constexpr std::size_t kMaximumDescriptionBytes = 200;
	inline constexpr std::size_t kMaximumMotdBytes = 1000;
	inline constexpr std::size_t kMaximumColorBytes = 7;

	inline std::optional<std::uint32_t> ToWholeSeconds(double a_seconds)
	{
		if (!std::isfinite(a_seconds) || a_seconds < 0.0) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>((std::min)(std::ceil(a_seconds), static_cast<double>(kMaximumInviteSeconds)));
	}

	inline bool IsSafeByte(char a_byte)
	{
		const auto value = static_cast<unsigned char>(a_byte);
		return value >= 0x20 && value != 0x7F && a_byte != '"' && a_byte != '\\';
	}

	inline bool IsSafeToken(std::string_view a_text, std::size_t a_maximumBytes)
	{
		return !a_text.empty() && a_text.size() <= a_maximumBytes &&
			std::all_of(a_text.begin(), a_text.end(), IsSafeByte);
	}

	inline bool IsBoundedText(const std::optional<std::string>& a_text, std::size_t a_maximumBytes)
	{
		return a_text && !a_text->empty() && a_text->size() <= a_maximumBytes;
	}

	inline std::string SanitizeText(std::string_view a_text, std::size_t a_maximumBytes)
	{
		std::string clean;
		for (const char byte : a_text) {
			if (IsSafeByte(byte)) {
				clean.push_back(byte);
			}
		}
		if (clean.size() > a_maximumBytes) {
			auto cut = a_maximumBytes;
			while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0U) == 0x80U) {
				--cut;
			}
			clean.resize(cut);
		}
		return clean;
	}

	inline std::optional<std::string> ReadTextField(const nlohmann::json& a_json, const char* a_field)
	{
		const auto field = a_json.find(a_field);
		if (field == a_json.end() || !field->is_string()) {
			return std::nullopt;
		}
		return field->get<std::string>();
	}

	inline bool IsProfileDescriptionChar(char a_char)
	{
		return a_char >= ' ' && a_char <= '~' && a_char != '"' && a_char != '\\';
	}

	inline bool IsHexDigit(char a_char)
	{
		return (a_char >= '0' && a_char <= '9') || (a_char >= 'A' && a_char <= 'F') || (a_char >= 'a' && a_char <= 'f');
	}

	inline std::optional<std::string> ReadProfileDescription(const nlohmann::json& a_json)
	{
		const auto text = ReadTextField(a_json, "description");
		if (!text) {
			return std::nullopt;
		}
		const auto first = text->find_first_not_of(' ');
		const auto trimmed = first == std::string::npos ? std::string{} : text->substr(first, text->find_last_not_of(' ') - first + 1);
		if (trimmed.size() > kMaximumDescriptionBytes || !std::all_of(trimmed.begin(), trimmed.end(), IsProfileDescriptionChar)) {
			return std::nullopt;
		}
		return trimmed;
	}

	inline std::optional<std::optional<std::string>> ReadProfileMotd(const nlohmann::json& a_json)
	{
		if (a_json.find("motd") == a_json.end()) {
			return std::optional<std::optional<std::string>>{ std::in_place };
		}
		const auto text = ReadTextField(a_json, "motd");
		if (!text) {
			return std::nullopt;
		}
		const auto first = text->find_first_not_of(' ');
		const auto trimmed = first == std::string::npos ? std::string{} : text->substr(first, text->find_last_not_of(' ') - first + 1);
		if (trimmed.size() > kMaximumMotdBytes || !std::all_of(trimmed.begin(), trimmed.end(), IsProfileDescriptionChar)) {
			return std::nullopt;
		}
		return std::optional<std::optional<std::string>>{ std::in_place, trimmed };
	}

	inline std::optional<std::string> ReadProfileColor(const nlohmann::json& a_json)
	{
		auto color = ReadTextField(a_json, "color");
		if (!color || color->empty()) {
			return color;
		}
		if (color->size() != kMaximumColorBytes || color->front() != '#' || !std::all_of(color->begin() + 1, color->end(), IsHexDigit)) {
			return std::nullopt;
		}
		std::transform(color->begin() + 1, color->end(), color->begin() + 1, [](char a_char) {
			return static_cast<char>(std::toupper(static_cast<unsigned char>(a_char)));
		});
		return color;
	}
}
