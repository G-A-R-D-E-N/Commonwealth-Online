// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TSocialNameplate.h"

#include <cassert>
#include <optional>
#include <string>

int main()
{
	using F4T::SocialNameplate::DisplayNameNeedsApply;
	using F4T::SocialNameplate::NoteDisplayNameApplied;
	using F4T::SocialNameplate::ResetDisplayNameTracking;
	using F4T::SocialNameplate::ResolveDisplayName;
	using F4T::SocialNameplate::ResolveShownName;

	assert(ResolveDisplayName("Alice", "Alice") == "Alice");
	assert(ResolveDisplayName("Stranger", "") == "Stranger");
	assert(ResolveDisplayName("", "Bob") == "Bob");
	assert(ResolveDisplayName("", "").empty());

	assert(ResolveShownName(std::optional<std::string>{ "Stranger" }, std::nullopt) == "Stranger");
	assert(ResolveShownName(std::optional<std::string>{ "Alice" }, std::optional<std::string>{ "Alice" }) == "Alice");
	assert(ResolveShownName(std::nullopt, std::optional<std::string>{ "Bob" }) == "Bob");
	assert(ResolveShownName(std::optional<std::string>{ "" }, std::optional<std::string>{ "Bob" }) == "Bob");
	assert(!ResolveShownName(std::nullopt, std::nullopt).has_value());
	assert(!ResolveShownName(std::optional<std::string>{ "" }, std::nullopt).has_value());

	std::optional<std::string> lastApplied;
	assert(DisplayNameNeedsApply(lastApplied, "Stranger"));
	NoteDisplayNameApplied(lastApplied, "Stranger");
	assert(!DisplayNameNeedsApply(lastApplied, "Stranger"));
	assert(DisplayNameNeedsApply(lastApplied, "Alice"));
	NoteDisplayNameApplied(lastApplied, "Alice");
	assert(!DisplayNameNeedsApply(lastApplied, "Alice"));
	std::optional<std::string> none;
	assert(!DisplayNameNeedsApply(none, ""));

	ResetDisplayNameTracking(lastApplied);
	assert(!lastApplied.has_value());
	assert(DisplayNameNeedsApply(lastApplied, "Alice"));
	NoteDisplayNameApplied(lastApplied, "Alice");
	assert(!DisplayNameNeedsApply(lastApplied, "Alice"));

	return 0;
}
