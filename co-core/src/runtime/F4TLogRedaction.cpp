// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TLogRedaction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace
{
	constexpr std::string_view kIpMarker{ "<ip>" };
	constexpr std::string_view kSteamIdMarker{ "<steamid>" };
	constexpr std::string_view kUserMarker{ "<user>" };
	constexpr std::string_view kRedactedMarker{ "<redacted>" };

	constexpr std::array<std::string_view, 28> kSensitiveKeys{
		"resume_token_protected",
		"resumeTokenProtected",
		"serverPasswordSha256",
		"resume_token_ack",
		"server_password",
		"resumeTokenAck",
		"serverPassword",
		"refresh_token",
		"session_nonce",
		"refreshToken",
		"access_token",
		"sessionNonce",
		"passwordHash",
		"resume_token",
		"accessToken",
		"resumeToken",
		"credentials",
		"auth_token",
		"session_id",
		"credential",
		"authToken",
		"sessionId",
		"password",
		"api_key",
		"apiKey",
		"secret",
		"bearer",
		"token"
	};

	constexpr std::array<std::string_view, 4> kSpaceSeparatedKeys{
		"serverPassword",
		"resumeToken",
		"password",
		"bearer"
	};

	constexpr std::string_view kSpaceSeparatedStopWords[]{
		"protected", "required", "set", "unset", "missing", "is", "was", "were",
		"be", "not", "changed", "reset", "expired", "invalid", "valid", "enabled",
		"disabled", "verification", "mismatch", "failed", "success", "error",
		"prompt", "field", "value", "token", "empty", "null", "none", "the", "a",
		"an", "of", "to", "for", "with", "and", "or", "has", "have", "had", "must",
		"should", "would", "will", "cannot", "can", "may", "does", "did", "do"
	};

	[[nodiscard]] bool IsAsciiDigit(char a_char)
	{
		return a_char >= '0' && a_char <= '9';
	}

	[[nodiscard]] bool IsAsciiAlpha(char a_char)
	{
		return (a_char >= 'a' && a_char <= 'z') || (a_char >= 'A' && a_char <= 'Z');
	}

	[[nodiscard]] bool IsHexDigit(char a_char)
	{
		return IsAsciiDigit(a_char) || (a_char >= 'a' && a_char <= 'f') || (a_char >= 'A' && a_char <= 'F');
	}

	[[nodiscard]] bool IsIdentifierChar(char a_char)
	{
		return IsAsciiDigit(a_char) || IsAsciiAlpha(a_char) || a_char == '_';
	}

	[[nodiscard]] bool IsIpNeighbourChar(char a_char)
	{
		return IsIdentifierChar(a_char) || a_char == '.';
	}

	[[nodiscard]] bool IsSlash(char a_char)
	{
		return a_char == '\\' || a_char == '/';
	}

	[[nodiscard]] bool IsValueDelimiter(char a_char)
	{
		return a_char == ' ' || a_char == '\t' || a_char == '\r' || a_char == '\n' ||
		       a_char == ',' || a_char == ';' || a_char == ')' || a_char == ']' ||
		       a_char == '}' || a_char == '&';
	}

	[[nodiscard]] bool IsUrlDelimiter(char a_char)
	{
		return IsValueDelimiter(a_char) || a_char == '"' || a_char == '\'' ||
		       a_char == '<' || a_char == '>' || a_char == '|';
	}

	[[nodiscard]] bool IsUserPathEndChar(char a_char)
	{
		return a_char == '\r' || a_char == '\n' || a_char == '"' || a_char == '<' ||
		       a_char == '>' || a_char == '|' || a_char == '\t';
	}

	[[nodiscard]] bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right)
	{
		if (a_left.size() != a_right.size()) {
			return false;
		}
		for (std::size_t index = 0; index < a_left.size(); ++index) {
			const auto left = (a_left[index] >= 'A' && a_left[index] <= 'Z') ? static_cast<char>(a_left[index] - 'A' + 'a') : a_left[index];
			const auto right = (a_right[index] >= 'A' && a_right[index] <= 'Z') ? static_cast<char>(a_right[index] - 'A' + 'a') : a_right[index];
			if (left != right) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool StartsWithIgnoreCase(std::string_view a_text, std::size_t a_offset, std::string_view a_prefix)
	{
		return a_offset + a_prefix.size() <= a_text.size() && EqualsIgnoreCase(a_text.substr(a_offset, a_prefix.size()), a_prefix);
	}

	[[nodiscard]] bool IsSpaceSeparatedKey(std::string_view a_key)
	{
		for (const auto key : kSpaceSeparatedKeys) {
			if (EqualsIgnoreCase(a_key, key)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool IsSpaceSeparatedStopWord(std::string_view a_token)
	{
		for (const auto word : kSpaceSeparatedStopWords) {
			if (EqualsIgnoreCase(a_token, word)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] std::size_t ConsumeSlashRun(std::string_view a_text, std::size_t a_offset)
	{
		if (a_offset >= a_text.size() || !IsSlash(a_text[a_offset])) {
			return std::string_view::npos;
		}
		auto cursor = a_offset + 1;
		if (cursor < a_text.size() && IsSlash(a_text[cursor])) {
			++cursor;
		}
		return cursor;
	}

	[[nodiscard]] bool IsHexGroup(std::string_view a_group)
	{
		if (a_group.empty() || a_group.size() > 4) {
			return false;
		}
		for (const auto character : a_group) {
			if (!IsHexDigit(character)) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool CountGroups(std::string_view a_part, std::size_t& a_groups)
	{
		if (a_part.empty()) {
			return true;
		}
		std::size_t start = 0;
		while (start <= a_part.size()) {
			const auto next = a_part.find(':', start);
			const auto length = next == std::string_view::npos ? std::string_view::npos : next - start;
			if (!IsHexGroup(a_part.substr(start, length))) {
				return false;
			}
			++a_groups;
			if (next == std::string_view::npos) {
				break;
			}
			start = next + 1;
		}
		return true;
	}

	[[nodiscard]] bool IsIpv6Token(std::string_view a_token)
	{
		if (a_token.size() < 3 || a_token.size() > 45) {
			return false;
		}

		const auto compression = a_token.find("::");
		if (compression != std::string_view::npos) {
			if (a_token.find("::", compression + 1) != std::string_view::npos) {
				return false;
			}
			std::size_t groups = 0;
			if (!CountGroups(a_token.substr(0, compression), groups)) {
				return false;
			}
			if (!CountGroups(a_token.substr(compression + 2), groups)) {
				return false;
			}
			if (groups == 0 || groups > 7) {
				return false;
			}
			if (a_token.back() == ':' && !(a_token.size() >= 2 && a_token.compare(a_token.size() - 2, 2, "::") == 0)) {
				return false;
			}
			return true;
		}

		if (static_cast<std::size_t>(std::count(a_token.begin(), a_token.end(), ':')) != 7) {
			return false;
		}
		std::size_t groups = 0;
		return CountGroups(a_token, groups) && groups == 8;
	}

	[[nodiscard]] bool IsSteamId64Token(std::string_view a_token)
	{
		if (a_token.size() != 17 || !a_token.starts_with("7656119")) {
			return false;
		}
		for (const auto character : a_token) {
			if (!IsAsciiDigit(character)) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] std::size_t SkipKeySeparator(std::string_view a_text, std::size_t a_offset)
	{
		auto cursor = a_offset;
		while (cursor < a_text.size() && (a_text[cursor] == ' ' || a_text[cursor] == '\t')) {
			++cursor;
		}
		if (cursor >= a_text.size() || (a_text[cursor] != '=' && a_text[cursor] != ':')) {
			return std::string_view::npos;
		}
		++cursor;
		while (cursor < a_text.size() && (a_text[cursor] == ' ' || a_text[cursor] == '\t')) {
			++cursor;
		}
		return cursor;
	}

	[[nodiscard]] std::size_t ConsumeValue(std::string_view a_text, std::size_t a_offset, std::string& a_out)
	{
		if (a_offset >= a_text.size()) {
			a_out += kRedactedMarker;
			return a_offset;
		}

		if (a_text[a_offset] == '"') {
			auto cursor = a_offset + 1;
			while (cursor < a_text.size()) {
				if (a_text[cursor] == '\\') {
					cursor += 2;
					continue;
				}
				if (a_text[cursor] == '"') {
					break;
				}
				++cursor;
			}
			if (cursor >= a_text.size()) {
				a_out += kRedactedMarker;
				return a_text.size();
			}
			a_out += '"';
			a_out += kRedactedMarker;
			a_out += '"';
			return cursor + 1;
		}

		auto cursor = a_offset;
		while (cursor < a_text.size() && !IsValueDelimiter(a_text[cursor])) {
			++cursor;
		}
		if (cursor == a_offset) {
			a_out += kRedactedMarker;
			return a_offset;
		}
		a_out += kRedactedMarker;
		return cursor;
	}

	[[nodiscard]] bool TrySpaceSeparatedValue(std::string_view a_text, std::size_t a_keyEnd, std::size_t a_start,
	                                          std::string& a_out, std::size_t& a_index)
	{
		auto gap = a_keyEnd;
		while (gap < a_text.size() && (a_text[gap] == ' ' || a_text[gap] == '\t')) {
			++gap;
		}
		if (gap == a_keyEnd || gap >= a_text.size()) {
			return false;
		}

		if (a_text[gap] == '"') {
			a_out.append(a_text.substr(a_start, gap - a_start));
			a_index = ConsumeValue(a_text, gap, a_out);
			return true;
		}

		auto tokenEnd = gap;
		while (tokenEnd < a_text.size() && !IsUrlDelimiter(a_text[tokenEnd])) {
			++tokenEnd;
		}
		if (tokenEnd == gap || IsSpaceSeparatedStopWord(a_text.substr(gap, tokenEnd - gap))) {
			return false;
		}

		a_out.append(a_text.substr(a_start, gap - a_start));
		a_out += kRedactedMarker;
		a_index = tokenEnd;
		return true;
	}

	[[nodiscard]] bool TrySensitiveKey(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (start > 0 && IsIdentifierChar(a_text[start - 1])) {
			return false;
		}

		const bool quoted = a_text[start] == '"';
		const auto keyStart = quoted ? start + 1 : start;

		for (const auto key : kSensitiveKeys) {
			if (keyStart + key.size() > a_text.size() || !EqualsIgnoreCase(a_text.substr(keyStart, key.size()), key)) {
				continue;
			}

			auto cursor = keyStart + key.size();
			if (quoted) {
				if (cursor >= a_text.size() || a_text[cursor] != '"') {
					continue;
				}
				++cursor;
			} else if (cursor < a_text.size() && IsIdentifierChar(a_text[cursor])) {
				continue;
			}

			const auto valueStart = SkipKeySeparator(a_text, cursor);
			if (valueStart == std::string_view::npos) {
				if (IsSpaceSeparatedKey(key) && TrySpaceSeparatedValue(a_text, cursor, start, a_out, a_index)) {
					return true;
				}
				continue;
			}

			a_out.append(a_text.substr(start, valueStart - start));
			a_index = ConsumeValue(a_text, valueStart, a_out);
			return true;
		}

		return false;
	}

	[[nodiscard]] bool TryUserPath(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (start > 0 && IsIdentifierChar(a_text[start - 1])) {
			return false;
		}

		auto cursor = start;
		if (cursor + 3 < a_text.size() && a_text[cursor] == '\\' && a_text[cursor + 1] == '\\' &&
		    a_text[cursor + 2] == '?' && IsSlash(a_text[cursor + 3])) {
			cursor += 4;
		}

		if (cursor + 2 >= a_text.size() || !IsAsciiAlpha(a_text[cursor]) || a_text[cursor + 1] != ':') {
			return false;
		}
		cursor += 2;

		const auto afterDriveSlash = ConsumeSlashRun(a_text, cursor);
		if (afterDriveSlash == std::string_view::npos) {
			return false;
		}
		cursor = afterDriveSlash;

		if (!StartsWithIgnoreCase(a_text, cursor, "Users")) {
			return false;
		}
		cursor += 5;

		const auto afterUsersSlash = ConsumeSlashRun(a_text, cursor);
		if (afterUsersSlash == std::string_view::npos) {
			return false;
		}
		cursor = afterUsersSlash;

		const auto nameStart = cursor;
		while (cursor < a_text.size() && !IsSlash(a_text[cursor]) && !IsUserPathEndChar(a_text[cursor])) {
			++cursor;
		}
		if (cursor == nameStart) {
			return false;
		}

		a_out.append(a_text.substr(start, nameStart - start));
		a_out += kUserMarker;
		a_index = cursor;
		return true;
	}

	[[nodiscard]] bool TryBracketedIp(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (a_text[start] != '[' || (start > 0 && IsIpNeighbourChar(a_text[start - 1]))) {
			return false;
		}

		const auto window = a_text.substr(start + 1, 45);
		const auto offset = window.find(']');
		if (offset == std::string_view::npos) {
			return false;
		}
		const auto close = start + 1 + offset;
		if (!IsIpv6Token(a_text.substr(start + 1, close - start - 1))) {
			return false;
		}

		auto end = close + 1;
		if (end + 1 < a_text.size() && a_text[end] == ':' && IsAsciiDigit(a_text[end + 1])) {
			auto port = end + 1;
			while (port < a_text.size() && IsAsciiDigit(a_text[port])) {
				++port;
			}
			if (port - (end + 1) <= 5) {
				end = port;
			}
		}

		a_out += kIpMarker;
		a_index = end;
		return true;
	}

	[[nodiscard]] bool TrySteamTarget(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (!StartsWithIgnoreCase(a_text, start, "steam:") || (start > 0 && IsIdentifierChar(a_text[start - 1]))) {
			return false;
		}

		auto cursor = start + 6;
		while (cursor < a_text.size() && !IsUrlDelimiter(a_text[cursor])) {
			++cursor;
		}
		if (cursor == start + 6) {
			return false;
		}

		a_out += IsSteamId64Token(a_text.substr(start + 6, cursor - start - 6)) ? kSteamIdMarker : kRedactedMarker;
		a_index = cursor;
		return true;
	}

	[[nodiscard]] bool TryIpv6(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (start > 0 && (IsIpNeighbourChar(a_text[start - 1]) || a_text[start - 1] == ':')) {
			return false;
		}

		auto end = start;
		while (end < a_text.size() && (IsHexDigit(a_text[end]) || a_text[end] == ':')) {
			++end;
		}
		if (end - start < 3 || (end < a_text.size() && IsIpNeighbourChar(a_text[end]))) {
			return false;
		}
		if (!IsIpv6Token(a_text.substr(start, end - start))) {
			return false;
		}

		if (end < a_text.size() && a_text[end] == '%') {
			auto zone = end + 1;
			while (zone < a_text.size() && IsIdentifierChar(a_text[zone])) {
				++zone;
			}
			if (zone > end + 1) {
				end = zone;
			}
		}

		a_out += kIpMarker;
		a_index = end;
		return true;
	}

	[[nodiscard]] bool TryIpv4(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (!IsAsciiDigit(a_text[start]) || (start > 0 && IsIpNeighbourChar(a_text[start - 1]))) {
			return false;
		}

		auto cursor = start;
		for (int octet = 0; octet < 4; ++octet) {
			if (cursor >= a_text.size() || !IsAsciiDigit(a_text[cursor])) {
				return false;
			}
			auto digits = cursor;
			int value = 0;
			while (digits < a_text.size() && IsAsciiDigit(a_text[digits])) {
				if (digits - cursor == 3) {
					return false;
				}
				value = value * 10 + (a_text[digits] - '0');
				++digits;
			}
			if (value > 255) {
				return false;
			}
			cursor = digits;
			if (octet < 3) {
				if (cursor >= a_text.size() || a_text[cursor] != '.') {
					return false;
				}
				++cursor;
			}
		}

		if (cursor < a_text.size() && IsIpNeighbourChar(a_text[cursor])) {
			return false;
		}

		if (cursor + 1 < a_text.size() && a_text[cursor] == ':' && IsAsciiDigit(a_text[cursor + 1])) {
			auto port = cursor + 1;
			while (port < a_text.size() && IsAsciiDigit(a_text[port])) {
				++port;
			}
			if (port - (cursor + 1) <= 5 && (port >= a_text.size() || !IsIpNeighbourChar(a_text[port]))) {
				cursor = port;
			}
		}

		a_out += kIpMarker;
		a_index = cursor;
		return true;
	}

	[[nodiscard]] bool TrySteamId64(std::string_view a_text, std::size_t& a_index, std::string& a_out)
	{
		const auto start = a_index;
		if (!IsSteamId64Token(a_text.substr(start, 17))) {
			return false;
		}
		if (start > 0 && IsIdentifierChar(a_text[start - 1])) {
			return false;
		}
		if (start + 17 < a_text.size() && IsIdentifierChar(a_text[start + 17])) {
			return false;
		}

		a_out += kSteamIdMarker;
		a_index = start + 17;
		return true;
	}

	[[nodiscard]] std::string RedactLogImpl(std::string_view a_text)
	{
		std::string redacted;
		redacted.reserve(a_text.size());

		std::size_t index = 0;
		while (index < a_text.size()) {
			if (TrySensitiveKey(a_text, index, redacted)) {
				continue;
			}
			if (TryUserPath(a_text, index, redacted)) {
				continue;
			}
			if (TryBracketedIp(a_text, index, redacted)) {
				continue;
			}
			if (TrySteamTarget(a_text, index, redacted)) {
				continue;
			}
			if (TryIpv6(a_text, index, redacted)) {
				continue;
			}
			if (TryIpv4(a_text, index, redacted)) {
				continue;
			}
			if (TrySteamId64(a_text, index, redacted)) {
				continue;
			}
			redacted.push_back(a_text[index]);
			++index;
		}

		return redacted;
	}
}

namespace F4T::LogRedaction
{
	std::string RedactLog(std::string_view a_text)
	{
		return RedactLogImpl(a_text);
	}

	std::string ComposeSharedLog(std::string_view a_previousRaw, std::string_view a_currentRaw)
	{
		auto current = RedactLog(a_currentRaw);
		if (a_previousRaw.empty()) {
			return current;
		}

		std::string composed = "===== previous session =====\n";
		composed += RedactLog(a_previousRaw);
		if (composed.back() != '\n') {
			composed.push_back('\n');
		}
		composed += "===== current session =====\n";
		composed += current;
		return composed;
	}
}
