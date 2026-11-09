#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace F4T::Utf8Path
{
	[[nodiscard]] inline std::string Encode(const std::filesystem::path& a_path)
	{
		const auto text = a_path.u8string();
		return std::string{ text.begin(), text.end() };
	}

	[[nodiscard]] inline std::filesystem::path Decode(std::string_view a_text)
	{
		const auto* begin = reinterpret_cast<const char8_t*>(a_text.data());
		return std::filesystem::path{ std::u8string{ begin, begin + a_text.size() } };
	}
}
