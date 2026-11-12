#ifndef F4T_SERVER_BROWSER_DATA_TESTING
#include "pch.h"
#endif

#include "F4TServerBrowserData.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace F4T::ServerBrowser
{
	namespace
	{
		std::string Lowercase(std::string_view a_text)
		{
			std::string lower(a_text);
			std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower;
		}
	}

	std::string TrimAsciiWhitespace(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string{ a_text.substr(first, last - first + 1) };
	}

	std::string AddressKey(std::string_view a_host, int a_port)
	{
		std::string host = TrimAsciiWhitespace(a_host);
		host = Lowercase(host);
		if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
			host = TrimAsciiWhitespace(std::string_view{ host }.substr(1, host.size() - 2));
		}
		return std::format("{}:{}", host, a_port > 0 ? a_port : 7777);
	}

	bool JoinFailureRetryable(std::string_view a_reason)
	{
		static constexpr std::string_view kRetryCannotFix[] = {
			"The resume token is invalid for this profile.",
			"The server does not recognize this resume credential.",
			"This profile requires its server-issued resume token.",
			"cannot be discarded here",
			"interrupted first character could not be identified",
			"invalid_resume)",
			"unknown_resume)",
			"no record of your saved session",
		};
		for (const auto phrase : kRetryCannotFix) {
			if (a_reason.find(phrase) != std::string_view::npos) {
				return false;
			}
		}
		return true;
	}

	bool JoinFailureProvesAnswer(std::string_view a_reason)
	{
		static constexpr std::string_view kGnsReasonPrefix = "(GNS reason ";
		if (const auto gnsAt = a_reason.find(kGnsReasonPrefix); gnsAt != std::string_view::npos) {
			int closeReason = 0;
			const auto digits = a_reason.substr(gnsAt + kGnsReasonPrefix.size());
			const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), closeReason);
			if (parsed.ec != std::errc{} || closeReason < 1000 || closeReason > 2999) {
				return false;
			}
		}
		static constexpr std::string_view kServerAnswers[] = {
			"This server needs a password.",
			"Wrong password.",
			"Server requires ",
			"You are running an older version of Commonwealth Online (",
			"This server runs Commonwealth Online ",
			"Server is full",
			"Your modpack does not match",
			"Client content manifest does not match",
			"Client is missing required capability",
			"That profile is already connecting.",
			"That profile is already connected.",
			"The resume token is invalid for this profile.",
			"The server does not recognize this resume credential.",
			"Server rejected the session",
			"Server refused the connection",
			"The host refused the connection.",
			"This profile requires its server-issued resume token.",
			"All cached resume identities are in use",
			"This connection already completed identity validation.",
		};
		static constexpr std::string_view kServerCodes[] = {
			"password_required",
			"password_invalid",
			"protocol_mismatch",
			"client_version_mismatch",
			"runtime_mismatch",
			"content_mismatch",
			"capability_mismatch",
			"modpack_mismatch",
			"server_full",
			"identity_capacity",
			"resume_required",
			"invalid_resume",
			"unknown_resume",
			"duplicate_identity",
			"duplicate_hello",
		};
		for (const auto phrase : kServerAnswers) {
			if (a_reason.find(phrase) != std::string_view::npos) {
				return true;
			}
		}
		for (const auto code : kServerCodes) {
			if (a_reason == code) {
				return true;
			}
		}
		return false;
	}

	JoinFailureBadge JoinFailureBadgeFor(std::string_view a_reason)
	{
		if (JoinFailureProvesAnswer(a_reason)) {
			return JoinFailureBadge{
				.answered = true,
				.result = ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 0 }
			};
		}
		return JoinFailureBadge{
			.answered = false,
			.result = ReachabilityResult{ std::string{ ReachabilityState::kUnchecked }, std::string{ kJoinFailureBadgeReason }, 0 }
		};
	}

	void NormalizeListedEntries(std::vector<ServerEntry>& a_servers)
	{
		for (auto& entry : a_servers) {
			entry.host = TrimAsciiWhitespace(entry.host);
			if (entry.port <= 0) {
				entry.port = 7777;
			}
		}
	}

	ReachabilityResult PreserveAnsweredMilliseconds(const ServerEntry* a_listed, ReachabilityResult a_badge)
	{
		if (a_listed != nullptr && a_listed->reachability == ReachabilityState::kAnswering) {
			a_badge.milliseconds = a_listed->reachabilityMs;
		}
		return a_badge;
	}

	ReachabilityResult CachedOrJoinFailureBadge(std::optional<ReachabilityResult> a_cached)
	{
		if (a_cached) {
			return *a_cached;
		}
		return ReachabilityResult{ std::string{ ReachabilityState::kUnchecked }, std::string{ kJoinFailureBadgeReason }, 0 };
	}
}
