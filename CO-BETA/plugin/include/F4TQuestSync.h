#pragma once

#include "F4TClientCompatibility.h"
#include "F4TQuestLogic.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestSync
{
	using SendFn = bool (*)(std::string_view a_json);

	inline constexpr std::string_view kCapability = "quest-sync-v1";
	inline constexpr std::string_view kReadyCapability = "quest-ready-v1";

	inline constexpr std::size_t kMaximumViewQuests = 32;

	struct ViewQuest
	{
		std::string quest;
		std::uint32_t stage{};
		bool completed{ false };
		bool held{ false };
		bool solo{ false };
		bool party{ false };
		std::vector<QuestLogic::FriendStage> friends;
	};

	inline void AddSessionCapabilities(F4T::ClientCompatibility::Metadata& a_metadata)
	{
		a_metadata.capabilities.emplace_back(kCapability);
		a_metadata.capabilities.emplace_back(kReadyCapability);
	}

	[[nodiscard]] std::string_view ViewMode();
	[[nodiscard]] std::vector<ViewQuest> ViewQuests();
	[[nodiscard]] std::string NextStoryQuest();
	[[nodiscard]] std::string ViewName(std::string_view a_quest);

	void SetSendFunction(SendFn a_send);
	void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope);
	void NoteWelcome(const nlohmann::json& a_packet);
	void ClearSession();
	void Reset();
	void BeginSettle();
	void Tick();
}
