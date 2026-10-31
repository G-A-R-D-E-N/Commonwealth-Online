#include "pch.h"

#include "F4TQuestPapyrus.h"

#include "F4TDebugLogging.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESQuest.h"

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
	};

	std::mutex g_mutex;
	std::deque<StoredCommand> g_commands;
	std::optional<StoredCommand> g_lastPopped;
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
		g_commands.push_back(StoredCommand{ std::move(a_key), a_resolvedForm, a_stage });
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

	void Clear()
	{
		std::lock_guard lock(g_mutex);
		g_commands.clear();
		g_lastPopped.reset();
		g_queries.clear();
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
		return true;
	}
}
