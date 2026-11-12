#pragma once

#include <string>
#include <string_view>

namespace F4T::ProfanityFilter
{
	[[nodiscard]] bool LoadWords(std::string_view a_text);
	enum class Scope { Slurs, All };

	void SetProfanityEnabled(bool a_enabled);
	[[nodiscard]] bool ProfanityForced();

	[[nodiscard]] bool ContainsBlockedWord(std::string_view a_text);
	[[nodiscard]] bool ContainsBlockedWord(std::string_view a_text, Scope a_scope);
	[[nodiscard]] std::string MaskBlockedWords(std::string_view a_text);
	[[nodiscard]] std::string MaskBlockedWords(std::string_view a_text, Scope a_scope);
}
