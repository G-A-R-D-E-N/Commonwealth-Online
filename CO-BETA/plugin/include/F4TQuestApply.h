#pragma once

#include "F4TQuestLogic.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::QuestApply
{
	struct QuestView
	{
		std::uint32_t stage{};
		bool          started{};
		bool          running{};
		bool          completed{};
		bool          busy{};
	};

	struct Command
	{
		std::string key;
		std::uint32_t resolvedForm{};
		std::int32_t stage{ -1 };
	};

	struct Log
	{
		std::function<void(std::string_view a_action, std::string_view a_key)>               unresolved;
		std::function<void(const QuestLogic::StageEntry& a_entry, std::uint32_t a_formId)>   applied;
		std::function<void(std::string_view a_key, std::uint32_t a_formId)>                  completed;
	};

	struct Host
	{
		std::function<std::optional<std::uint32_t>(std::string_view a_key)> formOf;
		std::function<std::optional<QuestView>(std::uint32_t a_formId)>     quest;
		std::function<void(const Command& a_command)>                       enqueue;
		Log                                                                 log;
	};

	void ApplyRemoteCompletions(QuestLogic::Tracker& a_tracker, const Host& a_host, std::uint64_t a_nowMilliseconds);
	void ApplyHeldStages(QuestLogic::Tracker& a_tracker, const Host& a_host, std::uint64_t a_nowMilliseconds);
}
