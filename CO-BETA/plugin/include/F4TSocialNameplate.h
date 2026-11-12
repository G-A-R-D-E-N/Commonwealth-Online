// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace F4T::SocialNameplate
{
// Constraint: empty displayName falls back to characterName; empty resolved text keeps the current plate.
[[nodiscard]] inline std::string ResolveDisplayName(
	std::string_view a_displayName,
	std::string_view a_characterName)
{
	if (!a_displayName.empty()) {
		return std::string{ a_displayName };
	}
	return std::string{ a_characterName };
}

[[nodiscard]] inline std::optional<std::string> ResolveShownName(
	const std::optional<std::string>& a_displayName,
	const std::optional<std::string>& a_disclosedName)
{
	if (a_displayName && !a_displayName->empty()) {
		return *a_displayName;
	}
	if (a_disclosedName && !a_disclosedName->empty()) {
		return *a_disclosedName;
	}
	return std::nullopt;
}

[[nodiscard]] inline bool ResolveNameFlag(
	std::string_view a_characterName,
	std::string_view a_displayName,
	bool a_carriedResolved)
{
	if (!a_characterName.empty()) {
		return true;
	}
	if (!a_displayName.empty()) {
		return false;
	}
	return a_carriedResolved;
}

[[nodiscard]] inline bool DisplayNameNeedsApply(
	const std::optional<std::string>& a_lastApplied,
	std::string_view a_resolved)
{
	return !a_resolved.empty() &&
		(!a_lastApplied || *a_lastApplied != a_resolved);
}

// Constraint: must stay non-empty, because DisplayNameNeedsApply ignores empty text.
inline constexpr std::string_view kHiddenNameplateText{ "Player" };

[[nodiscard]] inline std::string NameplateText(std::string_view a_real, bool a_show)
{
	if (a_show) {
		return std::string{ a_real };
	}
	return std::string{ kHiddenNameplateText };
}

inline void NoteDisplayNameApplied(
	std::optional<std::string>& a_lastApplied,
	std::string_view a_resolved)
{
	a_lastApplied = std::string{ a_resolved };
}

inline void ResetDisplayNameTracking(std::optional<std::string>& a_lastApplied)
{
	a_lastApplied.reset();
}
}
