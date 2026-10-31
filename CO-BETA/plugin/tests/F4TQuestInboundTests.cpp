#include "F4TQuestInbound.h"

#include "F4TQuestLogic.h"
#include "F4TQuestWire.h"

#include <cassert>
#include <string>

namespace
{
	using F4T::QuestInbound::Drain;
	using F4T::QuestInbound::IsActive;
	using F4T::QuestInbound::NoteConnection;
	using F4T::QuestInbound::ClearSession;
	using F4T::QuestInbound::ClearNotReachableShown;
	using F4T::QuestInbound::NoteNotReachableShown;
	using F4T::QuestInbound::HandlePacket;
	using F4T::QuestLogic::ApplyContext;
	using F4T::QuestLogic::Tracker;
	using F4T::QuestWire::Json;

	const std::string kQuest = "00ABCD:commonwealthonline.esp";
	const std::string kOther = "001234:fallout4.esm";
	const ApplyContext kReady{ 0, true, true, false, false };

	Json StagePacket()
	{
		return Json{
			{ "type", "questStage" },
			{ "quest", kQuest },
			{ "stage", 20 },
			{ "fromPlayerId", 7 },
		};
	}

	Json CompletedStagePacket()
	{
		Json packet = StagePacket();
		packet["completed"] = true;
		return packet;
	}

	Json GatedPolicy()
	{
		return Json{
			{ "type", "questPolicy" },
			{ "mode", "faction-war" },
			{ "version", 5 },
			{ "gates", Json::array({ Json{ { "quest", kQuest }, { "stage", 20 } } }) },
		};
	}

	Json SyncPacket()
	{
		return Json{
			{ "type", "questSync" },
			{ "stages", Json::array({ Json{ { "quest", kQuest }, { "stage", 30 } }, Json{ { "quest", kQuest }, { "stage", 10 } } }) },
			{ "completed", Json::array({ kOther }) },
		};
	}

	Json NotReachablePacket()
	{
		return Json{
			{ "type", "questNotReachable" },
			{ "reason", "prerequisite_missing" },
			{ "quest", kQuest },
			{ "missing", kOther },
			{ "previousLabel", "Out of Time" },
		};
	}

	Json NotReadyPacket(std::string_view a_reason)
	{
		return Json{
			{ "type", "questNotReady" },
			{ "reason", std::string{ a_reason } },
		};
	}

	Json PolicyPacket()
	{
		return Json{
			{ "type", "questPolicy" },
			{ "mode", "faction-war" },
			{ "version", 3 },
			{ "soloQuests", Json::array({ kOther }) },
			{ "partyQuests", Json::array({ kQuest }) },
			{ "gates", Json::array({ Json{ { "quest", kQuest }, { "stage", 40 } } }) },
		};
	}

	void UnknownTypesFallThrough()
	{
		NoteConnection(true);
		const Json welcome{ { "type", "welcome" } };
		assert(!HandlePacket("welcome", welcome));
		assert(!HandlePacket("transform", welcome));
		assert(!HandlePacket("npcLoot", welcome));
		assert(!HandlePacket("questReport", welcome));
		assert(!HandlePacket("", welcome));
		assert(!HandlePacket("questStage2", StagePacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
	}

	void MalformedPayloadIsConsumedAndDropped()
	{
		NoteConnection(true);
		const Json broken{ { "type", "questStage" }, { "quest", kQuest } };
		assert(HandlePacket("questStage", broken));
		assert(HandlePacket("questSync", Json{ { "type", "questSync" }, { "stages", 5 } }));
		assert(HandlePacket("questPolicy", Json{ { "type", "questPolicy" }, { "mode", "coop-story" } }));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
	}

	void StagePacketReachesTheTracker()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", StagePacket()));

		Tracker tracker;
		assert(tracker.PendingQuests().empty());
		Drain(tracker);
		assert(tracker.PendingQuests().size() == 1);
		assert(tracker.PendingQuests().front() == kQuest);

		const auto stage = tracker.TakeReadyStage(kQuest, kReady, 0);
		assert(stage && stage->stage == 20 && stage->quest == kQuest);
	}

	void SyncPacketReplaysInOrder()
	{
		NoteConnection(true);
		assert(HandlePacket("questSync", SyncPacket()));

		Tracker tracker;
		Drain(tracker);
		const auto first = tracker.TakeReadyStage(kQuest, kReady, 0);
		assert(first && first->stage == 10);
		const auto second = tracker.TakeReadyStage(kQuest, kReady, 1000);
		assert(second && second->stage == 30);
	}

	void PolicyPacketReachesTheTracker()
	{
		NoteConnection(true);
		assert(HandlePacket("questPolicy", PolicyPacket()));

		Tracker tracker;
		Drain(tracker);
		const auto& policy = tracker.GetPolicy();
		assert(policy.mode == F4T::QuestLogic::Mode::FactionWar);
		assert(policy.version == 3);
		assert(policy.soloQuests.size() == 1 && policy.soloQuests.front() == kOther);
		assert(policy.gates.size() == 1 && policy.gates.front().stage == 40);
		assert(tracker.IsSolo(kOther));
		assert(policy.partyQuests.size() == 1 && tracker.IsParty(kQuest));
		assert(tracker.IsGated(kQuest, 40));
	}

	void PolicyClearsHeldStagesOnNonSharingMode()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", StagePacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().size() == 1);

		assert(HandlePacket("questPolicy", Json{
			{ "type", "questPolicy" },
			{ "mode", "free-roam" },
			{ "version", 4 },
		}));
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
		assert(!tracker.SharesProgress());
	}

	void DrainIsIdempotentAndANewConnectionDropsTheOldQueue()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", StagePacket()));

		Tracker tracker;
		Drain(tracker);
		Drain(tracker);
		assert(tracker.PendingQuests().size() == 1);

		NoteConnection(true);
		Tracker fresh;
		Drain(fresh);
		assert(fresh.PendingQuests().empty());
	}

	void PacketsQueuedAfterTheWelcomeAreDrained()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", StagePacket()));

		NoteConnection(true);
		assert(HandlePacket("questSync", SyncPacket()));
		assert(HandlePacket("questPolicy", PolicyPacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().size() == 1);
		assert(tracker.PendingQuests().front() == kQuest);
		assert(tracker.GetPolicy().version == 3);
		assert(tracker.TakeReadyStage(kQuest, kReady, 0).has_value());
	}

	void ServerWithoutQuestSyncIsInactive()
	{
		NoteConnection(false);
		assert(!IsActive());
		assert(HandlePacket("questStage", StagePacket()));
		assert(HandlePacket("questSync", SyncPacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
		assert(tracker.GetPolicy().version == 0);

		NoteConnection(true);
		assert(IsActive());
		assert(HandlePacket("questStage", StagePacket()));
		Drain(tracker);
		assert(tracker.PendingQuests().size() == 1);
	}
	void SyncQueuedAfterTheWelcomeSurvivesTheSessionChange()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", StagePacket()));

		NoteConnection(true);
		assert(HandlePacket("questPolicy", PolicyPacket()));
		assert(HandlePacket("questSync", SyncPacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.GetPolicy().version == 3);
		assert(tracker.PendingQuests().size() == 1);
		assert(tracker.PendingQuests().front() == kQuest);

		const auto first = tracker.TakeReadyStage(kQuest, kReady, 0);
		const auto second = tracker.TakeReadyStage(kQuest, kReady, 1000);
		const auto third = tracker.TakeReadyStage(kQuest, kReady, 2000);
		assert(first && first->stage == 10);
		assert(second && second->stage == 30);
		assert(!third);
	}

	void CompletedStageReachesTheTracker()
	{
		NoteConnection(true);
		assert(HandlePacket("questStage", CompletedStagePacket()));

		Tracker tracker;
		Drain(tracker);
		const auto stage = tracker.TakeReadyStage(kQuest, kReady, 0);
		assert(stage && stage->stage == 20 && stage->completed);
	}

	void GatedCompletionIsAppliedAfterTheGateLifts()
	{
		NoteConnection(true);
		assert(HandlePacket("questPolicy", GatedPolicy()));
		assert(HandlePacket("questStage", CompletedStagePacket()));

		Tracker tracker;
		Drain(tracker);
		assert(!tracker.TakeReadyStage(kQuest, kReady, 0).has_value());
		assert(tracker.PendingQuests().size() == 1);

		assert(HandlePacket("questPolicy", Json{
			{ "type", "questPolicy" },
			{ "mode", "faction-war" },
			{ "version", 6 },
		}));
		Drain(tracker);
		const auto applied = tracker.TakeReadyStage(kQuest, kReady, 1000);
		assert(applied && applied->stage == 20 && applied->completed);
	}

	void NotReachablePacketIsRecognisedAndLeavesTheTrackerAlone()
	{
		NoteConnection(true);
		assert(HandlePacket("questNotReachable", NotReachablePacket()));
		assert(HandlePacket("questNotReachable", Json{ { "type", "questNotReachable" } }));

		ClearSession();
		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
		assert(tracker.TakeRemoteCompletions().empty());
	}

	void NotReachableRateLimitIsPerQuestPerMinute()
	{
		ClearNotReachableShown();
		assert(NoteNotReachableShown(kQuest, 1000));
		assert(!NoteNotReachableShown(kQuest, 2000));
		assert(!NoteNotReachableShown(kQuest, 60999));
		assert(NoteNotReachableShown(kQuest, 61000));
		assert(NoteNotReachableShown(kOther, 61000));
		ClearNotReachableShown();
		assert(NoteNotReachableShown(kQuest, 61000));
	}

	void ReadyRetryIsRaisedOnlyForNotReady()
	{
		NoteConnection(true);
		F4T::QuestInbound::ClearReadyRetry();
		assert(!F4T::QuestInbound::TakeReadyRetry());

		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kUpdateRequiredReason)));
		assert(!F4T::QuestInbound::TakeReadyRetry());

		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kNotReadyReason)));
		assert(F4T::QuestInbound::TakeReadyRetry());
		assert(!F4T::QuestInbound::TakeReadyRetry());

		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kNotReadyReason)));
		F4T::QuestInbound::ClearReadyRetry();
		assert(!F4T::QuestInbound::TakeReadyRetry());

		Tracker tracker;
		Drain(tracker);
	}

	std::string g_hudText;

	void CaptureHud(std::string_view a_text)
	{
		g_hudText = std::string{ a_text };
	}

	void NoticeHudIsRateLimitedPerReason()
	{
		NoteConnection(true);
		F4T::QuestInbound::SetHudFunction(&CaptureHud);
		Tracker tracker;

		g_hudText.clear();
		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kNotReadyReason)));
		Drain(tracker);
		assert(!g_hudText.empty());
		const auto first = g_hudText;

		g_hudText.clear();
		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kNotReadyReason)));
		Drain(tracker);
		assert(g_hudText.empty());

		g_hudText.clear();
		assert(HandlePacket("questNotReady", NotReadyPacket(F4T::QuestProtocol::kUpdateRequiredReason)));
		Drain(tracker);
		assert(!g_hudText.empty());
		assert(g_hudText != first);

		F4T::QuestInbound::SetHudFunction(nullptr);
	}

	void DisconnectClearsTheCapabilityAndTheQueue()
	{
		NoteConnection(true);
		assert(IsActive());
		assert(HandlePacket("questSync", SyncPacket()));

		ClearSession();
		assert(!IsActive());
		assert(HandlePacket("questPolicy", PolicyPacket()));
		assert(HandlePacket("questSync", SyncPacket()));

		Tracker tracker;
		Drain(tracker);
		assert(tracker.PendingQuests().empty());
		assert(tracker.GetPolicy().version == 0);

		NoteConnection(true);
		assert(IsActive());
		assert(HandlePacket("questPolicy", PolicyPacket()));
		Drain(tracker);
		assert(tracker.GetPolicy().version == 3);
	}
}

int main()
{
	UnknownTypesFallThrough();
	MalformedPayloadIsConsumedAndDropped();
	StagePacketReachesTheTracker();
	SyncPacketReplaysInOrder();
	CompletedStageReachesTheTracker();
	GatedCompletionIsAppliedAfterTheGateLifts();
	PolicyPacketReachesTheTracker();
	PolicyClearsHeldStagesOnNonSharingMode();
	DrainIsIdempotentAndANewConnectionDropsTheOldQueue();
	PacketsQueuedAfterTheWelcomeAreDrained();
	ServerWithoutQuestSyncIsInactive();
	SyncQueuedAfterTheWelcomeSurvivesTheSessionChange();
	DisconnectClearsTheCapabilityAndTheQueue();
	NotReachablePacketIsRecognisedAndLeavesTheTrackerAlone();
	NotReachableRateLimitIsPerQuestPerMinute();
	ReadyRetryIsRaisedOnlyForNotReady();
	NoticeHudIsRateLimitedPerReason();
	return 0;
}
