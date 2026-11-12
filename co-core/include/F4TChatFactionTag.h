#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace F4T::ChatFactionTag
{
	inline constexpr std::size_t kMaximumCodePoints = 8;

	[[nodiscard]] inline std::string Sanitize(std::string_view a_tag)
	{
		std::string output;
		output.reserve(a_tag.size());
		for (const char ch : a_tag) {
			const auto byte = static_cast<unsigned char>(ch);
			if (byte >= 0x20 && byte != 0x7f) {
				output.push_back(ch);
			}
		}
		const auto first = output.find_first_not_of(' ');
		if (first == std::string::npos) {
			return {};
		}
		const auto last = output.find_last_not_of(' ');
		output = output.substr(first, last - first + 1);
		std::size_t codePoints = 0;
		for (const char ch : output) {
			if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) {
				++codePoints;
			}
		}
		if (codePoints > kMaximumCodePoints) {
			return {};
		}
		return output;
	}

	[[nodiscard]] inline std::string FromMessage(const nlohmann::json& a_message)
	{
		const auto found = a_message.find("factionTag");
		if (found == a_message.end() || !found->is_string()) {
			return {};
		}
		return Sanitize(found->get_ref<const std::string&>());
	}
}
