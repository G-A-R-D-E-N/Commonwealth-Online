#include "pch.h"

#include "F4TQuestSync.h"

#include "F4TQuestApply.h"
#include "F4TQuestFormKey.h"
#include "F4TQuestInbound.h"
#include "F4TQuestNotices.h"
#include "F4TQuestPapyrus.h"
#include "F4TQuestProtocol.h"
#include "F4TQuestReadyGate.h"
#include "F4TQuestStoryOrder.h"
#include "F4TQuestUI.h"
#include "F4TQuestWire.h"
#include "F4TSessionCharacterSetupFlow.h"
#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"

#include "RE/L/LoadingMenu.h"
#include "RE/M/Main.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SendHUDMessage.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESQuest.h"
#include "RE/U/UI.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace F4T::QuestSync
{
	namespace
	{
		constexpr auto kPollInterval = 500ms;
		constexpr std::uint16_t kQuestEnabledFlag = 0x1;
		constexpr std::size_t kQuestQueryBatch = 32;
		constexpr std::uint64_t kScriptSilenceMilliseconds = 10'000;
		constexpr std::string_view kQuestScriptSilentNotice =
			"Quest sync is not running: CommonwealthOnline.esp is missing its quest script";
		const std::string kTutorialQuestKey = QuestKey::Format(QuestKey::Key{
			std::string{ F4T::QuestProtocol::kTutorialQuestPlugin },
			F4T::QuestProtocol::kTutorialQuestLocalId });

		QuestLogic::Tracker g_tracker;
		QuestReady::Gate g_readyGate;
		std::unordered_map<std::uint32_t, std::optional<std::string>> g_keyCache;
		std::unordered_set<std::string> g_unresolvedLogged;
		std::unordered_set<std::string> g_droppedLogged;
		std::unordered_set<std::string> g_noticedCompletions;
		QuestFormKey::LoadOrder g_loadOrder;
		std::size_t g_loadOrderStamp{ 0 };
		std::vector<QuestPapyrus::Query> g_candidates;
		std::vector<QuestPapyrus::Query> g_publishedBatch;
		std::size_t g_queryCursor{ 0 };
		std::optional<std::uint64_t> g_scriptSilenceSince;
		bool g_scriptSilenceWarned{ false };
		std::atomic_bool g_scriptSilenceResetRequested{ false };
		std::atomic_bool g_noticeResetRequested{ false };
		std::atomic_bool g_questReadyResetRequested{ false };
		std::chrono::steady_clock::time_point g_nextPoll{};
		bool g_readyExhaustedLogged{ false };
		SendFn g_send{ nullptr };
		std::atomic_bool g_questReadySent{ false };

		std::uint64_t NowMilliseconds()
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		bool DefaultSend(std::string_view a_json)
		{
			return F4T::Networking::Detail::SendPacketBytes(a_json);
		}

		bool IsInWorld()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (ui && (ui->GetMenuOpen<RE::LoadingMenu>() || ui->GetMenuOpen<RE::MainMenu>())) {
				return false;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->GetParentCell();
		}

		bool IsMenuMode()
		{
			const auto* main = RE::Main::GetSingleton();
			return main && main->inMenuMode;
		}

		const std::string* KeyFor(std::uint32_t a_formId, const QuestFormKey::LoadOrder& a_loadOrder)
		{
			const auto cached = g_keyCache.find(a_formId);
			if (cached != g_keyCache.end()) {
				return cached->second ? &*cached->second : nullptr;
			}
			const auto inserted = g_keyCache.emplace(a_formId, QuestFormKey::ToKey(a_formId, a_loadOrder));
			return inserted.first->second ? &*inserted.first->second : nullptr;
		}

		void LogUnresolved(std::string_view a_action, std::string_view a_key)
		{
			if (!g_unresolvedLogged.emplace(std::string(a_key)).second) {
				return;
			}
			F4T::DebugLogging::Log(
				"QuestSync",
				"cannot {} quest={}: no such quest form in this load order",
				a_action,
				a_key);
		}

		bool TutorialComplete()
		{
			return F4T::SessionCharacterSetupFlow::IsPodExitSignal();
		}

		bool SendQuestReady()
		{
			const auto packet = QuestWire::EncodeReady();
			if (!g_send(packet.dump())) {
				F4T::DebugLogging::Log("QuestSync", "could not send the quest-ready signal");
				return false;
			}
			F4T::DebugLogging::Log("QuestSync", "sent the quest-ready signal after the tutorial");
			return true;
		}

		std::uint16_t TutorialQuestStage()
		{
			const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(F4T::QuestProtocol::kTutorialQuestLocalId);
			if (!quest) {
				return 0;
			}
			return static_cast<std::uint16_t>((std::min)(
				static_cast<std::uint32_t>(quest->currentStage),
				static_cast<std::uint32_t>(F4T::QuestProtocol::kMaximumStage)));
		}

		bool SendTutorialReport(std::uint16_t a_stage)
		{
			const auto packet = QuestWire::EncodeReport(QuestWire::QuestReport{ kTutorialQuestKey, a_stage, false });
			if (!g_send(packet.dump())) {
				F4T::DebugLogging::Log("QuestSync", "could not send the tutorial report stage={}", a_stage);
				return false;
			}
			F4T::DebugLogging::Log(
				"QuestSync",
				"reported the tutorial progress quest={} stage={}",
				kTutorialQuestKey,
				a_stage);
			return true;
		}

		std::string ClockNow()
		{
			try {
				return std::format("{:%H:%M}", std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::system_clock::now() });
			} catch (const std::exception&) {
				return std::format("{:%H:%M}", std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()));
			}
		}

		void ShowQuestHud(std::string_view a_text)
		{
			static std::string s_hudText;
			s_hudText = std::string(a_text);
			RE::SendHUDMessage::ShowHUDMessage(s_hudText.c_str(), nullptr, true, true);
		}

		void DriveTutorialGate()
		{
			const bool complete = TutorialComplete();
			const auto stage = TutorialQuestStage();
			const auto result = QuestReady::Drive(
				g_readyGate,
				complete,
				stage,
				NowMilliseconds(),
				[stage] { return SendTutorialReport(stage); },
				[] { return SendQuestReady(); });
			if (result.exhausted && !g_readyExhaustedLogged) {
				g_readyExhaustedLogged = true;
				F4T::DebugLogging::Log(
					"QuestSync",
					"stopped retrying the quest-ready signal after {} refusals this session",
					QuestReady::kMaximumAttempts);
				const auto text = F4T::QuestNotices::FormatReadyExhausted();
				F4T::QuestNotices::Push(text, ClockNow());
				ShowQuestHud(text);
			}
			g_questReadySent.store(g_readyGate.ReadySent(), std::memory_order_release);
		}

		void SendReport(const QuestLogic::Report& a_report)
		{
			if (!QuestInbound::IsActive() || !F4T::Networking::IsConnectedToServer()) {
				return;
			}
			if (!TutorialComplete() || !g_questReadySent.load(std::memory_order_acquire)) {
				return;
			}
			const auto packet =
				QuestWire::EncodeReport(QuestWire::QuestReport{ a_report.quest, a_report.stage, a_report.completed });
			if (!g_send(packet.dump())) {
				F4T::DebugLogging::Log(
					"QuestSync",
					"could not send the report for quest={} stage={} completed={}",
					a_report.quest,
					a_report.stage,
					a_report.completed);
				return;
			}
			F4T::DebugLogging::Log(
				"QuestSync",
				"reported quest={} stage={} completed={}",
				a_report.quest,
				a_report.stage,
				a_report.completed);
		}

		std::string RemoteCompleterName(std::string_view a_quest)
		{
			const auto snapshot = g_tracker.Snapshot();
			const auto view = std::find_if(snapshot.begin(), snapshot.end(), [&](const QuestLogic::View& a_view) {
				return a_view.quest == a_quest;
			});
			if (view != snapshot.end()) {
				const auto completer = std::find_if(view->friends.begin(), view->friends.end(), [](const QuestLogic::FriendStage& a_friend) {
					return a_friend.completed;
				});
				if (completer != view->friends.end()) {
					for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
						if (remote.playerId == completer->playerId) {
							const auto shown =
								F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName);
							if (!shown.empty()) return shown;
						}
					}
				}
			}
			return "Another player";
		}

		void LogDroppedStage(const QuestLogic::DroppedStage& a_dropped)
		{
			const auto key = std::format("{}|{}", a_dropped.quest, static_cast<int>(a_dropped.cause));
			if (!g_droppedLogged.emplace(key).second) {
				return;
			}
			switch (a_dropped.cause) {
			case QuestLogic::DropCause::NotReached:
				F4T::DebugLogging::Log(
					"QuestSync",
					"dropped a held stage for quest={}: the local player has not reached it",
					a_dropped.quest);
				break;
			case QuestLogic::DropCause::NotShared:
				F4T::DebugLogging::Log(
					"QuestSync",
					"dropped a held stage for quest={}: this mode does not share quest progress",
					a_dropped.quest);
				break;
			case QuestLogic::DropCause::Solo:
				F4T::DebugLogging::Log(
					"QuestSync",
					"dropped a held stage for quest={}: the quest is solo for this player",
					a_dropped.quest);
				break;
			}
		}

		void NoteCompletion(std::string_view a_quest, std::string_view a_completer)
		{
			if (!g_noticedCompletions.emplace(std::string(a_quest)).second) {
				return;
			}
			const auto name = ViewName(a_quest);
			const auto label = name.empty() ? std::string(a_quest) : name;
			static std::string s_hudText;
			s_hudText = F4T::QuestNotices::FormatCompletion(label, a_completer);
			F4T::QuestNotices::Push(s_hudText, ClockNow());
			RE::SendHUDMessage::ShowHUDMessage(s_hudText.c_str(), nullptr, true, true);
		}

		void RebuildQuestCandidates(const QuestFormKey::LoadOrder& a_loadOrder)
		{
			g_candidates.clear();
			g_publishedBatch.clear();
			g_queryCursor = 0;

			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			for (auto* quest : data->GetFormArray<RE::TESQuest>()) {
				if (!quest) {
					continue;
				}
				if (!QuestLogic::IsSharedQuestType(static_cast<std::int32_t>(quest->data.questType))) {
					continue;
				}
				const auto* key = KeyFor(quest->GetFormID(), a_loadOrder);
				if (!key) {
					continue;
				}
				g_candidates.push_back(QuestPapyrus::Query{ *key, quest->GetFormID() });
			}
		}

		void WarnIfQuestScriptSilent()
		{
			if (g_scriptSilenceResetRequested.exchange(false)) {
				g_scriptSilenceSince.reset();
				g_scriptSilenceWarned = false;
			}
			if (g_scriptSilenceWarned || QuestPapyrus::HasScriptContact()) {
				return;
			}
			if (IsMenuMode()) {
				return;
			}

			const auto now = NowMilliseconds();
			if (!g_scriptSilenceSince) {
				g_scriptSilenceSince = now;
				return;
			}
			if (now - *g_scriptSilenceSince < kScriptSilenceMilliseconds) {
				return;
			}

			g_scriptSilenceWarned = true;
			REX::WARN(
				"[QuestSync] no COQuestSyncScript native call arrived within {} s of an in-world quest-sync-v1 session; "
				"CommonwealthOnline.esp is missing its quest script (see creation-kit/esp/README.md)",
				kScriptSilenceMilliseconds / 1000);
			RE::SendHUDMessage::ShowHUDMessage(kQuestScriptSilentNotice.data(), nullptr, true, true);
		}

		void PollLocalProgress(std::uint64_t a_nowMilliseconds)
		{
			for (const auto& entry : g_publishedBatch) {
				const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(entry.resolvedForm);
				const auto reported = QuestPapyrus::FindReport(entry.resolvedForm);
				if (!quest || !reported) {
					continue;
				}

				const bool scriptRunning = reported->running;
				const bool started = scriptRunning && quest->currentStage > 0;
				const QuestLogic::Observation observation{
					entry.key,
					quest->currentStage,
					static_cast<std::int32_t>(quest->data.questType),
					(quest->data.flags & kQuestEnabledFlag) != 0,
					reported->completed,
					started,
				};
				const auto report = g_questReadySent.load(std::memory_order_acquire)
					? g_tracker.Observe(observation, a_nowMilliseconds)
					: std::nullopt;
				if (report) {
					SendReport(*report);
					if (report->completed) {
						NoteCompletion(report->quest, "You");
					}
				}
			}

			if (g_candidates.empty()) {
				g_publishedBatch.clear();
				QuestPapyrus::PublishQueries({});
				return;
			}

			const auto total = g_candidates.size();
			const auto count = (std::min)(kQuestQueryBatch, total);
			g_queryCursor %= total;

			std::vector<QuestPapyrus::Query> batch;
			batch.reserve(count);
			for (std::size_t offset = 0; offset < count; ++offset) {
				batch.push_back(g_candidates[(g_queryCursor + offset) % total]);
			}
			g_queryCursor = (g_queryCursor + count) % total;

			QuestPapyrus::PublishQueries(batch);
			g_publishedBatch = std::move(batch);
		}

		QuestApply::Host MakeApplyHost(const QuestFormKey::LoadOrder& a_loadOrder, bool a_busy, bool a_notify = true)
		{
			QuestApply::Host host;
			host.formOf = [&a_loadOrder](std::string_view a_key) { return QuestFormKey::ToFormId(a_key, a_loadOrder); };
			host.quest = [a_busy](std::uint32_t a_formId) -> std::optional<QuestApply::QuestView> {
				const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(a_formId);
				if (!quest) {
					return std::nullopt;
				}
				const auto reported = QuestPapyrus::FindReport(a_formId);
				const bool scriptRunning = reported && reported->running;
				return QuestApply::QuestView{
					quest->currentStage,
					scriptRunning && quest->currentStage > 0,
					(quest->data.flags & kQuestEnabledFlag) != 0,
					reported && reported->completed,
					a_busy,
				};
			};
			host.enqueue = [](const QuestApply::Command& a_command) {
				QuestPapyrus::Enqueue(a_command.key, a_command.resolvedForm, a_command.stage);
			};
			host.log.unresolved = [](std::string_view a_action, std::string_view a_key) { LogUnresolved(a_action, a_key); };
			host.log.applied = [](const QuestLogic::StageEntry& a_entry, std::uint32_t a_formId) {
				F4T::DebugLogging::Log(
					"QuestSync",
					"applied quest={} stage={} formId={:08X} completed={}",
					a_entry.quest,
					a_entry.stage,
					a_formId,
					a_entry.completed);
			};
			host.log.completed = [a_notify](std::string_view a_key, std::uint32_t a_formId) {
				F4T::DebugLogging::Log("QuestSync", "completed quest={} formId={:08X}", a_key, a_formId);
				if (a_notify) {
					NoteCompletion(a_key, RemoteCompleterName(a_key));
				}
			};
			return host;
		}

		void ApplyRemoteCompletions(const QuestFormKey::LoadOrder& a_loadOrder, std::uint64_t a_nowMilliseconds)
		{
			QuestApply::ApplyRemoteCompletions(g_tracker, MakeApplyHost(a_loadOrder, false, false), a_nowMilliseconds);
		}

		void ApplyHeldStages(const QuestFormKey::LoadOrder& a_loadOrder, std::uint64_t a_nowMilliseconds)
		{
			QuestApply::ApplyHeldStages(g_tracker, MakeApplyHost(a_loadOrder, false), a_nowMilliseconds);
		}
	}

	std::string_view ViewMode()
	{
		return QuestLogic::ModeName(g_tracker.GetPolicy().mode);
	}

	std::vector<ViewQuest> ViewQuests()
	{
		const auto views = g_tracker.Snapshot();
		std::vector<ViewQuest> rows;
		rows.reserve((std::min)(views.size(), kMaximumViewQuests));
		for (const auto& view : views) {
			if (rows.size() >= kMaximumViewQuests) {
				break;
			}
			if (!g_tracker.IsStoryReached(view.quest)) {
				continue;
			}
			rows.push_back(ViewQuest{ view.quest, view.stage, view.completed, view.held, g_tracker.IsSolo(view.quest), g_tracker.IsParty(view.quest), view.friends });
		}
		return rows;
	}

	std::string NextStoryQuest()
	{
		const auto next = g_tracker.NextStoryStep();
		if (!next || next->empty()) {
			return {};
		}
		for (const auto& row : ViewQuests()) {
			if (row.quest == *next) {
				return {};
			}
		}
		return *next;
	}

	std::string ViewName(std::string_view a_quest)
	{
		if (g_loadOrder.empty()) {
			return {};
		}
		const auto questForm = QuestFormKey::ToFormId(a_quest, g_loadOrder);
		if (!questForm) {
			return {};
		}
		const auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(*questForm);
		if (!quest) {
			return {};
		}
		return quest->fullName.c_str();
	}

	void SetSendFunction(SendFn a_send)
	{
		g_send = a_send ? a_send : &DefaultSend;
	}

	void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope)
	{
		const auto parsed = QuestKey::Parse(a_quest);
		if (!parsed) {
			F4T::DebugLogging::Log("QuestSync", "ignored a scope request for a malformed quest key");
			return;
		}
		if (!QuestInbound::IsActive() || !F4T::Networking::IsConnectedToServer() || !TutorialComplete() ||
			!g_questReadySent.load(std::memory_order_acquire)) {
			return;
		}
		if (!g_send) {
			g_send = &DefaultSend;
		}
		const auto key = QuestKey::Format(*parsed);
		const auto packet = QuestWire::EncodeScope(QuestWire::QuestScope{ key, a_scope });
		const auto scopeName = packet.at("scope").get<std::string>();
		if (!g_send(packet.dump())) {
			F4T::DebugLogging::Log("QuestSync", "could not request the scope quest={} scope={}", key, scopeName);
			return;
		}
		F4T::DebugLogging::Log("QuestSync", "requested scope quest={} scope={}", key, scopeName);
	}

	void NoteWelcome(const nlohmann::json& a_packet)
	{
		bool advertised = false;
		if (const auto capabilities = a_packet.find("capabilities");
			capabilities != a_packet.end() && capabilities->is_array()) {
			advertised = std::any_of(capabilities->begin(), capabilities->end(), [](const nlohmann::json& a_entry) {
				return a_entry.is_string() && a_entry.get_ref<const std::string&>() == kCapability;
			});
		}
		QuestInbound::SetHudFunction(&ShowQuestHud);
		QuestInbound::NoteConnection(advertised);
		F4T::QuestUI::RequestRefresh();
	}

	void ClearSession()
	{
		QuestInbound::ClearSession();
		QuestInbound::ClearReadyRetry();
		g_questReadyResetRequested = true;
		g_questReadySent.store(false, std::memory_order_release);
		QuestPapyrus::Clear();
		g_noticeResetRequested = true;
		g_scriptSilenceResetRequested = true;
		F4T::QuestUI::RequestRefresh();
	}

	void Reset()
	{
		g_tracker.Reset();
		g_tracker.SetStoryOrder(F4T::QuestStory::Order::Default());
		g_readyGate.Reset();
		g_questReadySent.store(false, std::memory_order_release);
		g_readyExhaustedLogged = false;
		g_noticedCompletions.clear();
		g_droppedLogged.clear();
		F4T::QuestNotices::Clear();
		g_keyCache.clear();
		g_unresolvedLogged.clear();
		g_loadOrder.clear();
		g_loadOrderStamp = 0;
		g_candidates.clear();
		g_publishedBatch.clear();
		g_queryCursor = 0;
	}

	void BeginSettle()
	{
		g_tracker.BeginSettle(NowMilliseconds());
	}

	void Tick()
	{
		if (g_noticeResetRequested.exchange(false)) {
			g_noticedCompletions.clear();
			g_droppedLogged.clear();
			F4T::QuestNotices::Clear();
		}
		if (g_questReadyResetRequested.exchange(false)) {
			g_readyGate.Reset();
			g_questReadySent.store(false, std::memory_order_release);
			g_readyExhaustedLogged = false;
		}
		if (QuestInbound::TakeReadyRetry()) {
			g_readyGate.NoteRefused(NowMilliseconds());
			g_questReadySent.store(g_readyGate.ReadySent(), std::memory_order_release);
		}
		if (!g_send) {
			g_send = &DefaultSend;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now < g_nextPoll) {
			return;
		}
		g_nextPoll = now + kPollInterval;

		if (!QuestInbound::IsActive() || !F4T::Networking::IsConnectedToServer()) {
			return;
		}

		DriveTutorialGate();

		g_tracker.SetLocalPlayerId(F4T::RemotePlayerState::GetAssignedPlayerId());
		QuestInbound::Drain(g_tracker);

		if (!IsInWorld()) {
			return;
		}

		WarnIfQuestScriptSilent();

		const auto loadOrderStamp = QuestFormKey::GetLoadOrderStamp();
		if (loadOrderStamp != g_loadOrderStamp) {
			g_loadOrder = QuestFormKey::GetLoadOrder();
			g_loadOrderStamp = loadOrderStamp;
			g_keyCache.clear();
			g_unresolvedLogged.clear();
			RebuildQuestCandidates(g_loadOrder);
		}
		if (g_loadOrder.empty()) {
			return;
		}

		const auto nowMilliseconds = NowMilliseconds();
		ApplyRemoteCompletions(g_loadOrder, nowMilliseconds);
		PollLocalProgress(nowMilliseconds);
		ApplyHeldStages(g_loadOrder, nowMilliseconds);
		for (const auto& dropped : g_tracker.TakeDroppedStages()) {
			LogDroppedStage(dropped);
		}
	}
}
