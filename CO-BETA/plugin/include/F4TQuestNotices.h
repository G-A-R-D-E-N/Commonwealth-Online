#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestNotices
{
	inline constexpr std::size_t kMaximumNotices = 16;

	struct Notice
	{
		std::string text;
		std::string time;
	};

	[[nodiscard]] std::string FormatCompletion(std::string_view a_questName, std::string_view a_completer);
	[[nodiscard]] std::string FormatNotReady(std::string_view a_reason);
	[[nodiscard]] std::string FormatNotReachable(std::string_view a_reason, std::string_view a_previousLabel);
	[[nodiscard]] std::string FormatReadyExhausted();
	void Push(std::string_view a_text, std::string_view a_time);
	[[nodiscard]] std::vector<Notice> Snapshot();
	[[nodiscard]] std::uint64_t Revision();
	void Clear();
}
