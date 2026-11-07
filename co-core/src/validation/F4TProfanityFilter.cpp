#include "F4TProfanityFilter.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::ProfanityFilter
{
	namespace
	{
		struct Entry
		{
			std::string text;
			bool substring{};
			bool stem{};
			bool collapsible{};
		};

		struct List
		{
			std::vector<Entry> words;
			std::vector<std::string> phrases;
		};

		List g_slurs;
		List g_profanity;
		std::atomic<bool> g_profanityEnabled{};

		constexpr std::array kStemSuffixes = std::to_array<std::string_view>({ "ed", "er", "ers", "ing", "in" });
		constexpr std::size_t kMinimumEsWordLength = 5;
		constexpr std::size_t kMinimumCollapseWordLength = 4;
		constexpr std::size_t kMinimumSpacedWordLength = 4;

		std::vector<const List*> ActiveLists(bool a_profanity)
		{
			std::vector<const List*> lists{ &g_slurs };
			if (a_profanity) {
				lists.push_back(&g_profanity);
			}
			return lists;
		}

		bool IsWordChar(char a_ch)
		{
			return (a_ch >= 'a' && a_ch <= 'z') || (a_ch >= 'A' && a_ch <= 'Z') || (a_ch >= '0' && a_ch <= '9');
		}

		bool IsLeetSymbol(char a_ch)
		{
			return a_ch == '@' || a_ch == '$' || a_ch == '!';
		}

		char Fold(char a_ch)
		{
			switch (a_ch) {
			case '0': return 'o';
			case '1': case '!': return 'i';
			case '3': return 'e';
			case '4': case '@': return 'a';
			case '5': case '$': return 's';
			case '7': return 't';
			case '9': return 'g';
			default: return (a_ch >= 'A' && a_ch <= 'Z') ? static_cast<char>(a_ch - 'A' + 'a') : a_ch;
			}
		}

		std::string Collapse(std::string_view a_text)
		{
			std::string out;
			for (const char ch : a_text) {
				if (out.empty() || out.back() != ch) {
					out.push_back(ch);
				}
			}
			return out;
		}

		std::string SqueezeRuns(std::string_view a_text)
		{
			std::string out;
			for (std::size_t i = 0; i < a_text.size();) {
				std::size_t run = 1;
				while (i + run < a_text.size() && a_text[i + run] == a_text[i]) {
					++run;
				}
				out.append(run >= 3 ? 1 : run, a_text[i]);
				i += run;
			}
			return out;
		}

		bool MatchesWord(std::string_view a_token, const Entry& a_entry)
		{
			const std::string_view word = a_entry.text;
			if (a_token.size() < word.size() || a_token.substr(0, word.size()) != word) {
				return false;
			}
			const auto rest = a_token.substr(word.size());
			if (rest.empty() || rest == "s") {
				return true;
			}
			if (rest == "es" && (a_entry.stem || word.size() >= kMinimumEsWordLength)) {
				return true;
			}
			return a_entry.stem && std::find(kStemSuffixes.begin(), kStemSuffixes.end(), rest) != kStemSuffixes.end();
		}

		bool TokenBlocked(std::string_view a_token, bool a_profanity)
		{
			const auto squeezed = SqueezeRuns(a_token);
			const auto collapsed = Collapse(a_token);
			for (const auto* list : ActiveLists(a_profanity)) {
				for (const auto& entry : list->words) {
					if (entry.substring) {
						if (a_token.find(entry.text) != std::string_view::npos || squeezed.find(entry.text) != std::string::npos) {
							return true;
						}
					} else if (MatchesWord(a_token, entry) || MatchesWord(squeezed, entry) || (entry.collapsible && MatchesWord(collapsed, entry))) {
						return true;
					}
				}
			}
			return false;
		}

		bool ContainsPhrase(std::string_view a_text, bool a_profanity)
		{
			std::string folded{ " " };
			for (const char ch : a_text) {
				if (IsWordChar(ch)) {
					folded.push_back(Fold(ch));
				} else if (folded.back() != ' ') {
					folded.push_back(' ');
				}
			}
			folded.push_back(' ');
			for (const auto* list : ActiveLists(a_profanity)) {
				for (const auto& phrase : list->phrases) {
					if (folded.find(" " + phrase + " ") != std::string::npos) {
						return true;
					}
				}
			}
			return false;
		}

		struct Span
		{
			std::size_t begin;
			std::size_t end;
		};

		std::string FoldRange(std::string_view a_text, std::size_t a_begin, std::size_t a_end)
		{
			std::string token;
			for (auto at = a_begin; at < a_end; ++at) {
				token.push_back(Fold(a_text[at]));
			}
			return token;
		}

		std::vector<Span> BlockedSpans(std::string_view a_text, bool a_profanity)
		{
			std::vector<Span> spans;
			std::size_t index = 0;
			while (index < a_text.size()) {
				if (!IsWordChar(a_text[index]) && !IsLeetSymbol(a_text[index])) {
					++index;
					continue;
				}
				const auto start = index;
				while (index < a_text.size() && (IsWordChar(a_text[index]) || IsLeetSymbol(a_text[index]))) {
					++index;
				}
				auto trailing = index;
				while (trailing > start && IsLeetSymbol(a_text[trailing - 1])) {
					--trailing;
				}
				auto leading = start;
				while (leading < trailing && IsLeetSymbol(a_text[leading])) {
					++leading;
				}
				bool found = false;
				for (const auto candidate : { Span{ start, index }, Span{ start, trailing }, Span{ leading, trailing } }) {
					if (candidate.begin < candidate.end && TokenBlocked(FoldRange(a_text, candidate.begin, candidate.end), a_profanity)) {
						spans.push_back(candidate);
						found = true;
						break;
					}
				}
				if (found) {
					continue;
				}
				auto segment = start;
				for (auto at = start; at <= index; ++at) {
					if (at != index && !IsLeetSymbol(a_text[at])) {
						continue;
					}
					const bool wholeRun = segment == start && at == index;
					if (!wholeRun && segment < at && TokenBlocked(FoldRange(a_text, segment, at), a_profanity)) {
						spans.push_back({ segment, at });
					}
					segment = at + 1;
				}
			}
			return spans;
		}

		bool ContainsSpacedLetters(std::string_view a_text, bool a_profanity)
		{
			std::string letters;
			std::size_t index = 0;
			while (index < a_text.size()) {
				if (!IsWordChar(a_text[index]) && !IsLeetSymbol(a_text[index])) {
					++index;
					continue;
				}
				const auto start = index;
				while (index < a_text.size() && (IsWordChar(a_text[index]) || IsLeetSymbol(a_text[index]))) {
					++index;
				}
				if (index - start != 1) {
					letters.clear();
					continue;
				}
				letters.push_back(Fold(a_text[start]));
				for (const auto* list : ActiveLists(a_profanity)) {
					for (const auto& entry : list->words) {
						const auto& word = entry.text;
						if (word.size() >= kMinimumSpacedWordLength && letters.size() >= word.size() &&
							letters.compare(letters.size() - word.size(), word.size(), word) == 0) {
							return true;
						}
					}
				}
			}
			return false;
		}
	}

	bool LoadWords(std::string_view a_text)
	{
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		List slurs;
		List profanity;
		List* current = nullptr;
		for (std::size_t begin = 0; begin < a_text.size();) {
			auto end = a_text.find('\n', begin);
			if (end == std::string_view::npos) {
				end = a_text.size();
			}
			auto line = a_text.substr(begin, end - begin);
			begin = end + 1;
			while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
				line.remove_prefix(1);
			}
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
				line.remove_suffix(1);
			}
			if (line.empty() || line.front() == '#') {
				continue;
			}
			if (line.front() == '[') {
				current = line == "[slurs]" ? &slurs : line == "[profanity]" ? &profanity : nullptr;
				continue;
			}
			Entry entry;
			entry.substring = line.front() == '*';
			entry.stem = line.front() == '+';
			if (entry.substring || entry.stem) {
				line.remove_prefix(1);
			}
			for (const char ch : line) {
				entry.text.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch);
			}
			const bool valid = !entry.text.empty() && std::all_of(entry.text.begin(), entry.text.end(), [](char ch) {
				return IsWordChar(ch) || ch == ' ';
			});
			if (!valid || !current) {
				continue;
			}
			if (entry.text.find(' ') != std::string::npos) {
				current->phrases.push_back(entry.text);
				continue;
			}
			entry.collapsible = entry.text.size() >= kMinimumCollapseWordLength && Collapse(entry.text) == entry.text;
			current->words.push_back(std::move(entry));
		}
		if (slurs.words.empty() && slurs.phrases.empty()) {
			return false;
		}
		g_slurs = std::move(slurs);
		g_profanity = std::move(profanity);
		return true;
	}

	void SetProfanityEnabled(bool a_enabled)
	{
		g_profanityEnabled = a_enabled;
	}

	bool ProfanityForced()
	{
		return g_profanityEnabled;
	}

	bool ContainsBlockedWord(std::string_view a_text)
	{
		return ContainsBlockedWord(a_text, ProfanityForced() ? Scope::All : Scope::Slurs);
	}

	bool ContainsBlockedWord(std::string_view a_text, Scope a_scope)
	{
		const bool profanity = a_scope == Scope::All;
		return !BlockedSpans(a_text, profanity).empty() || ContainsPhrase(a_text, profanity) || ContainsSpacedLetters(a_text, profanity);
	}

	std::string MaskBlockedWords(std::string_view a_text)
	{
		return MaskBlockedWords(a_text, ProfanityForced() ? Scope::All : Scope::Slurs);
	}

	std::string MaskBlockedWords(std::string_view a_text, Scope a_scope)
	{
		std::string out{ a_text };
		for (const auto span : BlockedSpans(a_text, a_scope == Scope::All)) {
			out.replace(span.begin, span.end - span.begin, span.end - span.begin, '*');
		}
		return out;
	}
}
