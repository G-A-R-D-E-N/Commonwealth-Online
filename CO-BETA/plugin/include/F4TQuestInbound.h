#pragma once

#include "F4TQuestLogic.h"
#include "F4TQuestWire.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestInbound
{
	using HudFn = void (*)(std::string_view a_text);

	struct Item
	{
		enum class Kind
		{
			Stage,
			Sync,
			Policy,
			Notice,
			NotReachable,
			Presence,
			Credit,
			FireteamSnapshot,
			FireteamInvite,
			FireteamDecline,
			FireteamEvent
		};

		Kind kind{ Kind::Stage };
		std::uint64_t connectionEpoch{};
		std::string noticeReason;
		std::string noticeQuest;
		std::string noticeMissing;
		std::string noticePreviousLabel;
		QuestLogic::StageEntry stage{};
		std::uint32_t fromPlayerId{};
		std::vector<QuestLogic::StageEntry> stages{};
		std::vector<std::string> completed{};
		QuestLogic::Policy policy{};
		std::string creditId;
		std::string creditFormKey;
		std::uint32_t creditObjectiveId{};
		std::string creditKind;
		std::string creditTargetKey;
		QuestWire::FireteamSnapshotIn fireteamSnapshot{};
		QuestWire::FireteamInviteIn fireteamInvite{};
		QuestWire::FireteamDeclineIn fireteamDecline{};
		QuestWire::FireteamEventIn fireteamEvent{};
	};

	struct PendingCredit
	{
		std::string formKey;
		std::uint32_t objectiveId{};
		std::string creditId;
		std::string kind;
		std::string targetKey;
	};

	void SetHudFunction(HudFn a_hud);
	void NoteConnection(bool a_questSyncActive);
	void ClearSession();
	// constraint: consumed on the tick thread; set by a questNotReady frame.
	[[nodiscard]] bool TakeReadyRetry();
	void ClearReadyRetry();
	// constraint: one questNotReady or questNotReachable HUD message per key per minute.
	[[nodiscard]] bool NoteNotReachableShown(std::string_view a_quest, std::uint64_t a_nowMilliseconds);
	void ClearNotReachableShown();
	[[nodiscard]] bool IsActive();
	[[nodiscard]] bool HandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Drain(QuestLogic::Tracker& a_tracker);
	[[nodiscard]] std::vector<PendingCredit> TakePendingCredits();
	[[nodiscard]] bool SendFireteamPacket(const nlohmann::json& a_packet);
}
