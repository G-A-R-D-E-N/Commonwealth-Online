#include "pch.h"

#include "F4TQuestPapyrus.h"

#include "F4TDebugLogging.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESQuest.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace
{
	struct StoredCommand
	{
		std::string key;
		std::uint32_t resolvedForm{};
		std::int32_t stage{ -1 };
		std::int32_t kind{ 0 };
		std::uint32_t objectiveId{};
		std::string creditId;
		int attempts{ 0 };
	};

	// constraint: BGSQuestObjective::index is uint16_t
	constexpr std::uint32_t kMaximumCreditObjectiveId = 0xFFFFu;
	constexpr std::size_t kMaximumRecentCredits = 64;
	constexpr int kMaximumApplyAttempts = 3;

	std::mutex g_mutex;
	std::deque<StoredCommand> g_commands;
	std::optional<StoredCommand> g_lastPopped;
	std::vector<F4T::QuestPapyrus::CreditAck> g_creditAcks;
	std::deque<std::string> g_recentCredits;
	std::vector<F4T::QuestPapyrus::Query> g_queries;
	std::unordered_map<std::uint32_t, F4T::QuestPapyrus::State> g_reports;
	std::atomic<bool> g_scriptContact{ false };

	void MarkContact()
	{
		g_scriptContact.store(true, std::memory_order_relaxed);
	}

	bool PapyrusHasCommand(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		return !g_commands.empty();
	}

	std::int32_t PapyrusNextCommandQuest(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return 0;
		}
		return static_cast<std::int32_t>(g_commands.front().resolvedForm);
	}

	std::int32_t PapyrusNextCommandStage(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return 0;
		}
		return g_commands.front().stage;
	}

	std::int32_t PapyrusNextCommandKind(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return 0;
		}
		return g_commands.front().kind;
	}

	std::int32_t PapyrusNextCommandObjective(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return 0;
		}
		return static_cast<std::int32_t>(g_commands.front().objectiveId);
	}

	std::string PapyrusNextCommandCreditId(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return {};
		}
		return g_commands.front().creditId;
	}

	void PapyrusReportCreditApplied(std::monostate, std::string a_creditId, std::int32_t a_stage)
	{
		MarkContact();
		if (a_creditId.empty() || a_stage < 0) {
			return;
		}
		std::lock_guard lock(g_mutex);
		if (!g_lastPopped || g_lastPopped->kind != 1 || g_lastPopped->creditId != a_creditId) {
			return;
		}
		g_lastPopped.reset();
		g_recentCredits.push_back(a_creditId);
		while (g_recentCredits.size() > kMaximumRecentCredits) {
			g_recentCredits.pop_front();
		}
		g_creditAcks.push_back(F4T::QuestPapyrus::CreditAck{ std::move(a_creditId), static_cast<std::uint32_t>(a_stage) });
	}

	void PapyrusPopCommand(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (g_commands.empty()) {
			return;
		}
		g_lastPopped = std::move(g_commands.front());
		g_commands.pop_front();
	}

	void PapyrusReportApplied(std::monostate, bool a_applied)
	{
		MarkContact();
		StoredCommand command;
		{
			std::lock_guard lock(g_mutex);
			if (!g_lastPopped) {
				return;
			}
			command = std::move(*g_lastPopped);
			g_lastPopped.reset();
		}
		if (!a_applied && command.attempts + 1 < kMaximumApplyAttempts) {
			++command.attempts;
			std::lock_guard lock(g_mutex);
			g_commands.push_front(std::move(command));
			return;
		}
		F4T::DebugLogging::Log(
			"QuestSync",
			"result quest={} stage={} form={:08X} applied={}",
			command.key,
			command.stage,
			command.resolvedForm,
			a_applied);
	}

	std::int32_t PapyrusQueryCount(std::monostate)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		return static_cast<std::int32_t>(g_queries.size());
	}

	std::int32_t PapyrusQueryQuest(std::monostate, std::int32_t a_index)
	{
		MarkContact();
		std::lock_guard lock(g_mutex);
		if (a_index < 0 || static_cast<std::size_t>(a_index) >= g_queries.size()) {
			return 0;
		}
		return static_cast<std::int32_t>(g_queries[static_cast<std::size_t>(a_index)].resolvedForm);
	}

	void PapyrusReportState(std::monostate, RE::TESQuest* a_quest, bool a_completed, bool a_running)
	{
		MarkContact();
		if (!a_quest) {
			return;
		}
		std::lock_guard lock(g_mutex);
		g_reports[a_quest->GetFormID()] = F4T::QuestPapyrus::State{ a_completed, a_running };
	}
}

namespace F4T::QuestPapyrus
{
	void Enqueue(std::string a_key, std::uint32_t a_resolvedForm, std::int32_t a_stage)
	{
		if (a_key.empty() || a_resolvedForm == 0) {
			return;
		}
		std::lock_guard lock(g_mutex);
		g_commands.push_back(StoredCommand{ std::move(a_key), a_resolvedForm, a_stage, 0, 0, {} });
	}

	void EnqueueCredit(std::string a_key, std::uint32_t a_resolvedForm, std::uint32_t a_objectiveId, std::string a_creditId)
	{
		if (a_key.empty() || a_resolvedForm == 0 || a_creditId.empty() || a_objectiveId > kMaximumCreditObjectiveId) {
			return;
		}
		std::lock_guard lock(g_mutex);
		const auto isSameCredit = [&a_creditId](const StoredCommand& a_command) {
			return a_command.kind == 1 && a_command.creditId == a_creditId;
		};
		if (std::find(g_recentCredits.begin(), g_recentCredits.end(), a_creditId) != g_recentCredits.end() ||
			(g_lastPopped && isSameCredit(*g_lastPopped)) || std::any_of(g_commands.begin(), g_commands.end(), isSameCredit)) {
			return;
		}
		g_commands.push_back(StoredCommand{ std::move(a_key), a_resolvedForm, -1, 1, a_objectiveId, std::move(a_creditId) });
	}

	void PublishQueries(std::vector<Query> a_queries)
	{
		std::lock_guard lock(g_mutex);
		g_queries = std::move(a_queries);
	}

	std::optional<State> FindReport(std::uint32_t a_resolvedForm)
	{
		std::lock_guard lock(g_mutex);
		const auto found = g_reports.find(a_resolvedForm);
		if (found == g_reports.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	bool HasScriptContact()
	{
		return g_scriptContact.load(std::memory_order_relaxed);
	}

	std::vector<CreditAck> TakeCreditAcks()
	{
		std::lock_guard lock(g_mutex);
		std::vector<CreditAck> acks = std::move(g_creditAcks);
		g_creditAcks.clear();
		return acks;
	}

	void Clear()
	{
		std::lock_guard lock(g_mutex);
		g_commands.clear();
		g_lastPopped.reset();
		g_creditAcks.clear();
		g_queries.clear();
		g_recentCredits.clear();
		g_reports.clear();
		g_scriptContact.store(false, std::memory_order_relaxed);
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "HasCommand", PapyrusHasCommand, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "NextCommandQuest", PapyrusNextCommandQuest, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "NextCommandStage", PapyrusNextCommandStage, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "PopCommand", PapyrusPopCommand, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "ReportApplied", PapyrusReportApplied, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "QueryCount", PapyrusQueryCount, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "QueryQuest", PapyrusQueryQuest, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "ReportState", PapyrusReportState, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "NextCommandKind", PapyrusNextCommandKind, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "NextCommandObjective", PapyrusNextCommandObjective, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "NextCommandCreditId", PapyrusNextCommandCreditId, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineQuest", "ReportCreditApplied", PapyrusReportCreditApplied, false, false);
		return true;
	}
}
