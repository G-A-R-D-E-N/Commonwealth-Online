#include "F4TQuestInbound.h"

#include "F4TDebugLogging.h"
#include "F4TFireteamClient.h"
#include "F4TQuestNotices.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestWire.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace F4T::Networking::Detail
{
	bool SendPacketBytes(std::string_view a_packet);
}

namespace F4T::QuestInbound
{
	namespace
	{
		std::mutex g_inboundMutex;
		std::vector<Item> g_inbound;
		std::vector<PendingCredit> g_pendingCredits;
		std::uint64_t g_connectionEpoch{ 1 };
		std::atomic<bool> g_active{ false };
		std::atomic<bool> g_readyRetryRequested{ false };
		std::atomic<HudFn> g_hud{ nullptr };
		constexpr std::uint64_t kNotReachableWindowMilliseconds = 60'000;
		std::unordered_map<std::string, std::uint64_t> g_notReachableShown;
		std::unordered_set<std::string> g_storyRefused;
		constexpr std::size_t kMaximumInboundItems = 1024;
		constexpr std::size_t kMaximumPendingCreditItems = 256;
		constexpr std::uint32_t kMaximumCreditResolveAttempts = 60;

		std::uint64_t NowMilliseconds()
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		void NoteStoryRefused(std::string_view a_quest)
		{
			{
				const std::scoped_lock lock(g_inboundMutex);
				if (!g_storyRefused.emplace(std::string(a_quest)).second) {
					return;
				}
			}
			F4T::DebugLogging::Log("QuestSync", "withheld quest={}: the story has not reached it yet", a_quest);
		}

		void ShowHud(std::string_view a_text)
		{
			const auto hud = g_hud.load(std::memory_order_acquire);
			if (hud != nullptr) {
				hud(a_text);
			}
		}

		std::string ClockNow()
		{
			try {
				return std::format("{:%H:%M}", std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::system_clock::now() });
			} catch (const std::exception&) {
				return std::format("{:%H:%M}", std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()));
			}
		}

		std::optional<Item> Decode(std::string_view a_type, const nlohmann::json& a_packet)
		{
			Item item{};
			if (a_type == QuestWire::kStageType) {
				const auto decoded = QuestWire::DecodeStage(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Stage;
				item.stage = QuestLogic::StageEntry{ decoded->formKey, decoded->stage, decoded->completed };
				item.fromPlayerId = decoded->fromPlayerId;
			} else if (a_type == QuestWire::kSyncType) {
				const auto decoded = QuestWire::DecodeSync(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Sync;
				item.stages = decoded->stages;
				item.completed = decoded->completed;
			} else if (a_type == QuestWire::kNotReadyType) {
				const auto decoded = QuestWire::DecodeNotReady(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Notice;
				item.noticeReason = decoded->reason;
				item.noticeQuest = decoded->quest;
			} else if (a_type == QuestWire::kNotReachableType) {
				const auto decoded = QuestWire::DecodeNotReachable(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::NotReachable;
				item.noticeReason = decoded->reason;
				item.noticeQuest = decoded->quest;
				item.noticeMissing = decoded->missing;
				item.noticePreviousLabel = decoded->previousLabel;
			} else if (a_type == QuestWire::kPolicyType) {
				const auto decoded = QuestWire::DecodePolicy(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Policy;
				item.policy.mode = QuestLogic::ParseMode(decoded->mode);
				item.policy.version = decoded->version;
				item.policy.soloQuests = decoded->soloQuests;
				item.policy.gates = decoded->gates;
				item.policy.partyQuests = decoded->partyQuests;
				item.policy.fireteamQuests = decoded->fireteamQuests;
				item.policy.fireteamSupported = decoded->fireteamSupported;
				item.policy.storyOrder = decoded->storyOrder;
			} else if (a_type == QuestWire::kPresenceType) {
				const auto decoded = QuestWire::DecodePresence(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Presence;
				item.stage = QuestLogic::StageEntry{ decoded->formKey, static_cast<std::uint16_t>(decoded->stage), false };
				item.fromPlayerId = decoded->ownerPlayerId;
			} else if (a_type == QuestWire::kCreditType) {
				const auto decoded = QuestWire::DecodeCredit(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::Credit;
				item.creditId = decoded->creditId;
				item.creditFormKey = decoded->formKey;
				item.creditObjectiveId = decoded->objectiveId;
				item.creditKind = decoded->kind;
				item.creditTargetKey = decoded->targetFormKey;
			} else if (a_type == QuestWire::kFireteamSnapshotType) {
				const auto decoded = QuestWire::DecodeFireteamSnapshot(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::FireteamSnapshot;
				item.fireteamSnapshot = *decoded;
			} else if (a_type == QuestWire::kFireteamInviteType) {
				const auto decoded = QuestWire::DecodeFireteamInvite(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::FireteamInvite;
				item.fireteamInvite = *decoded;
			} else if (a_type == QuestWire::kFireteamRespondType && a_packet.contains("accept")) {
				const auto decoded = QuestWire::DecodeFireteamDecline(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::FireteamDecline;
				item.fireteamDecline = *decoded;
			} else if (a_type == QuestWire::kFireteamRespondType || a_type == QuestWire::kFireteamEventType) {
				const auto decoded = QuestWire::DecodeFireteamEvent(a_packet);
				if (!decoded) {
					return std::nullopt;
				}
				item.kind = Item::Kind::FireteamEvent;
				item.fireteamEvent = *decoded;
			} else {
				return std::nullopt;
			}
			return item;
		}
	}

	void SetHudFunction(HudFn a_hud)
	{
		g_hud.store(a_hud, std::memory_order_release);
	}

	bool TakeReadyRetry()
	{
		return g_readyRetryRequested.exchange(false);
	}

	void ClearReadyRetry()
	{
		g_readyRetryRequested.store(false, std::memory_order_release);
	}

	void NoteConnection(bool a_questSyncActive)
	{
		std::uint64_t epoch = 0;
		{
			const std::scoped_lock lock(g_inboundMutex);
			epoch = ++g_connectionEpoch;
			std::erase_if(g_inbound, [](const Item& a_item) { return a_item.connectionEpoch != g_connectionEpoch; });
			g_storyRefused.clear();
			g_notReachableShown.clear();
		}
		F4T::FireteamClient::ClearSession();
		g_active.store(a_questSyncActive, std::memory_order_release);
		F4T::DebugLogging::Log(
			"QuestSync",
			"new connection epoch={} server quest sync {}",
			epoch,
			a_questSyncActive ? "advertised" : "not advertised");
	}

	void ClearSession()
	{
		std::uint64_t epoch = 0;
		{
			const std::scoped_lock lock(g_inboundMutex);
			epoch = ++g_connectionEpoch;
			g_inbound.clear();
			g_pendingCredits.clear();
			g_storyRefused.clear();
			g_notReachableShown.clear();
		}
		g_active.store(false, std::memory_order_release);
		F4T::FireteamClient::ClearSession();
		F4T::DebugLogging::Log(
			"QuestSync",
			"session ended epoch={}: quest sync is not advertised until the next welcome; queued packets dropped",
			epoch);
	}

	bool IsActive()
	{
		return g_active.load(std::memory_order_acquire);
	}

	bool NoteNotReachableShown(std::string_view a_quest, std::uint64_t a_nowMilliseconds)
	{
		const std::scoped_lock lock(g_inboundMutex);
		const auto shown = g_notReachableShown.find(std::string(a_quest));
		if (shown != g_notReachableShown.end() &&
			a_nowMilliseconds - shown->second < kNotReachableWindowMilliseconds) {
			return false;
		}
		g_notReachableShown[std::string(a_quest)] = a_nowMilliseconds;
		return true;
	}

	void ClearNotReachableShown()
	{
		const std::scoped_lock lock(g_inboundMutex);
		g_notReachableShown.clear();
	}

	bool HandlePacket(std::string_view a_type, const nlohmann::json& a_packet)
	{
		const bool recognised = a_type == QuestWire::kStageType ||
			a_type == QuestWire::kSyncType ||
			a_type == QuestWire::kPolicyType ||
			a_type == QuestWire::kNotReadyType ||
			a_type == QuestWire::kPresenceType ||
			a_type == QuestWire::kCreditType ||
			a_type == QuestWire::kNotReachableType ||
			a_type == QuestWire::kFireteamSnapshotType ||
			a_type == QuestWire::kFireteamInviteType ||
			a_type == QuestWire::kFireteamRespondType ||
			a_type == QuestWire::kFireteamEventType;
		if (!recognised) {
			return false;
		}
		if (!IsActive()) {
			return true;
		}

		auto item = Decode(a_type, a_packet);
		if (!item) {
			F4T::DebugLogging::Log("QuestSync", "ignored malformed {} packet", a_type);
			return true;
		}

		if (item->kind == Item::Kind::Notice && item->noticeReason == F4T::QuestProtocol::kNotReadyReason) {
			g_readyRetryRequested.store(true, std::memory_order_release);
		}
		const std::scoped_lock lock(g_inboundMutex);
		if (g_inbound.size() >= kMaximumInboundItems) {
			F4T::DebugLogging::Log("QuestSync", "dropped a {} packet: the inbound queue is full", a_type);
			return true;
		}
		item->connectionEpoch = g_connectionEpoch;
		g_inbound.push_back(std::move(*item));
		return true;
	}

	void Drain(QuestLogic::Tracker& a_tracker)
	{
		std::vector<Item> work;
		std::uint64_t epoch = 0;
		{
			const std::scoped_lock lock(g_inboundMutex);
			work.swap(g_inbound);
			epoch = g_connectionEpoch;
		}

		for (auto& item : work) {
			if (item.connectionEpoch != epoch) {
				continue;
			}
			switch (item.kind) {
			case Item::Kind::Notice: {
				static std::string s_noticeText;
				const auto key = std::format("{}|{}", item.noticeReason, item.noticeQuest);
				if (!NoteNotReachableShown(key, NowMilliseconds())) {
					F4T::DebugLogging::Log(
						"QuestSync",
						"suppressed a repeated questNotReady notice key={}",
						key);
					break;
				}
				s_noticeText = F4T::QuestNotices::FormatNotReady(item.noticeReason);
				F4T::QuestNotices::Push(s_noticeText, ClockNow());
				ShowHud(s_noticeText);
				break;
			}
			case Item::Kind::NotReachable: {
				static std::string s_notReachableText;
				if (!NoteNotReachableShown(item.noticeQuest, NowMilliseconds())) {
					F4T::DebugLogging::Log(
						"QuestSync",
						"suppressed a repeated questNotReachable for quest={}",
						item.noticeQuest);
					break;
				}
				s_notReachableText =
					F4T::QuestNotices::FormatNotReachable(item.noticeReason, item.noticePreviousLabel);
				F4T::QuestNotices::Push(s_notReachableText, ClockNow());
				ShowHud(s_notReachableText);
				break;
			}
			case Item::Kind::Policy:
				F4T::DebugLogging::Log(
					"QuestSync",
					"policy mode={} version={} solo={} party={} gates={}",
					QuestLogic::ModeName(item.policy.mode),
					item.policy.version,
					item.policy.soloQuests.size(),
					item.policy.partyQuests.size(),
					item.policy.gates.size());
				a_tracker.SetPolicy(std::move(item.policy));
				break;
			case Item::Kind::Stage:
				if (!a_tracker.IsStoryReached(item.stage.quest)) {
					NoteStoryRefused(item.stage.quest);
				}
				if (a_tracker.QueueRemoteStage(item.stage.quest, item.stage.stage, item.stage.completed)) {
					a_tracker.NoteFriendStage(item.stage.quest, item.fromPlayerId, item.stage.stage, item.stage.completed);
				}
				break;
			case Item::Kind::Sync:
				F4T::DebugLogging::Log(
					"QuestSync",
					"sync stages={} completed={}",
					item.stages.size(),
					item.completed.size());
				for (const auto& stage : item.stages) {
					if (!a_tracker.IsStoryReached(stage.quest)) {
						NoteStoryRefused(stage.quest);
					}
				}
				for (const auto& quest : item.completed) {
					if (!a_tracker.IsStoryReached(quest)) {
						NoteStoryRefused(quest);
					}
				}
				a_tracker.QueueRemoteSync(item.stages, item.completed);
				break;
			case Item::Kind::Presence:
				if (!a_tracker.IsStoryReached(item.stage.quest)) {
					NoteStoryRefused(item.stage.quest);
					break;
				}
				a_tracker.NoteFriendStage(item.stage.quest, item.fromPlayerId, item.stage.stage, false);
				break;
			case Item::Kind::FireteamSnapshot:
				F4T::FireteamClient::ApplySnapshot(item.fireteamSnapshot);
				break;
			case Item::Kind::FireteamInvite:
				F4T::FireteamClient::ApplyInvite(item.fireteamInvite, NowMilliseconds());
				break;
			case Item::Kind::FireteamDecline:
				F4T::FireteamClient::ApplyDecline(item.fireteamDecline);
				break;
			case Item::Kind::FireteamEvent:
				F4T::FireteamClient::ApplyEvent(item.fireteamEvent);
				break;
			case Item::Kind::Credit:
				F4T::DebugLogging::Log(
					"QuestSync",
					"credit {} quest={} objective={} kind={}",
					item.creditId,
					item.creditFormKey,
					item.creditObjectiveId,
					item.creditKind);
				{
					const std::scoped_lock lock(g_inboundMutex);
					if (g_pendingCredits.size() >= kMaximumPendingCreditItems) {
						F4T::DebugLogging::Log("QuestSync", "dropped credit {}: the pending credit queue is full", item.creditId);
					} else {
						g_pendingCredits.push_back(PendingCredit{ item.creditFormKey, item.creditObjectiveId, item.creditId, item.creditKind, item.creditTargetKey });
					}
				}
				break;
			}
		}
	}

	bool SendFireteamPacket(const nlohmann::json& a_packet)
	{
		return F4T::Networking::Detail::SendPacketBytes(a_packet.dump());
	}

	std::vector<PendingCredit> TakePendingCredits()
	{
		const std::scoped_lock lock(g_inboundMutex);
		std::vector<PendingCredit> credits = std::move(g_pendingCredits);
		g_pendingCredits.clear();
		return credits;
	}

	void RequeueUnresolvedCredit(PendingCredit a_credit)
	{
		if (++a_credit.resolveAttempts >= kMaximumCreditResolveAttempts) {
			F4T::DebugLogging::Log("QuestSync", "dropped credit {} quest={}: it did not resolve after {} attempts", a_credit.creditId, a_credit.formKey, a_credit.resolveAttempts);
			return;
		}
		const std::scoped_lock lock(g_inboundMutex);
		if (g_pendingCredits.size() >= kMaximumPendingCreditItems) {
			F4T::DebugLogging::Log("QuestSync", "dropped credit {}: the pending credit queue is full", a_credit.creditId);
			return;
		}
		g_pendingCredits.push_back(std::move(a_credit));
	}
}
