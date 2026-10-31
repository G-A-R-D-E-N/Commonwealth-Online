#include "pch.h"

#include "F4TIntroQuestProbe.h"

#include "F4TDebugLogging.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSBaseAlias.h"
#include "RE/N/NiPointer.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESWorldSpace.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace F4T::IntroQuestProbe
{
	namespace
	{
		struct WatchedQuest
		{
			std::string_view editorId;
			std::uint32_t formId;
		};

		constexpr std::uint32_t kDialogueConcordAreaFormId = 0x000179F3;
		constexpr std::string_view kPrestonAliasName{ "Alias_PrestonGarveyAlias" };

		// TODO-verify: ESM-scan form ids, source E:/GardenEngine/re-answers/2026-10-21-first-join-409-answers.md:117
		constexpr std::array<WatchedQuest, 7> kWatchedQuests{
			WatchedQuest{ "MQ101", 0x0001ED86 },
			WatchedQuest{ "MQ102", 0x0001CC2A },
			WatchedQuest{ "DialogueVault111", 0x0001ED62 },
			WatchedQuest{ "DialogueConcordArea", kDialogueConcordAreaFormId },
			WatchedQuest{ "MQ103", 0x000229E5 },
			WatchedQuest{ "Tutorial", 0 },
			WatchedQuest{ "MQ03", 0 }
		};

		std::atomic_bool g_tracing{ false };
		std::atomic<std::uint32_t> g_prestonFormId{ 0 };
		std::uint32_t g_lastCellFormId{ 0 };
		std::uint32_t g_lastPrestonCellFormId{ 0 };
		std::uint16_t g_lastLoggedStage[kWatchedQuests.size()]{};
		bool g_haveLastLoggedStage[kWatchedQuests.size()]{};

		[[nodiscard]] RE::TESQuest* FindWatchedQuest(const WatchedQuest& a_watched)
		{
			if (a_watched.formId != 0) {
				return RE::TESForm::GetFormByID<RE::TESQuest>(a_watched.formId);
			}
			return RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ a_watched.editorId });
		}

		RE::NiPointer<RE::TESObjectREFR> RefreshPreston()
		{
			RE::NiPointer<RE::TESObjectREFR> preston;
			if (auto* concord = RE::TESForm::GetFormByID<RE::TESQuest>(kDialogueConcordAreaFormId)) {
				for (auto* alias : concord->aliases) {
					if (alias && alias->aliasName == kPrestonAliasName) {
						RE::ObjectRefHandle handle;
						concord->GetAliasedRef(&handle, alias->aliasID);
						preston = handle.get();
						break;
					}
				}
			}
			g_prestonFormId.store(preston.get() ? preston.get()->GetFormID() : 0, std::memory_order_relaxed);
			return preston;
		}

		[[nodiscard]] std::uint32_t PrestonCellFormId(const RE::TESObjectREFR* a_ref)
		{
			const auto* cell = a_ref ? a_ref->GetParentCell() : nullptr;
			return cell ? cell->GetFormID() : 0;
		}

		void LogPreston(std::string_view a_reason, RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				F4T::DebugLogging::Log("IntroQuestProbe", "preston reason={} ref=unresolved", a_reason);
				return;
			}
			const auto pos = a_ref->GetPosition();
			const auto* actor = a_ref->As<RE::Actor>();
			const auto* process = actor ? actor->currentProcess : nullptr;
			const auto* package = process ? process->currentPackage.package : nullptr;
			F4T::DebugLogging::Log(
				"IntroQuestProbe",
				"preston reason={} ref=0x{:08X} cell=0x{:08X} pos=({:.1f},{:.1f},{:.1f}) package=0x{:08X} inCombat={}",
				a_reason,
				a_ref->GetFormID(),
				PrestonCellFormId(a_ref),
				pos.x,
				pos.y,
				pos.z,
				package ? package->GetFormID() : 0,
				actor ? actor->IsInCombat() : false);
		}

		void LogPlayerCell(std::string_view a_reason)
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				F4T::DebugLogging::Log("IntroQuestProbe", "cell unknown (no player) reason={}", a_reason);
				return;
			}

			const auto* cell = player->GetParentCell();
			const char* cellEditor = cell && cell->GetFormEditorID() ? cell->GetFormEditorID() : "";
			const auto cellFormId = cell ? cell->GetFormID() : 0;
			const auto* world = cell && cell->worldSpace ? cell->worldSpace : player->cachedWorldspace;
			const char* worldEditor = world && world->GetFormEditorID() ? world->GetFormEditorID() : "";
			const auto pos = player->GetPosition();
			F4T::DebugLogging::Log(
				"IntroQuestProbe",
				"cell reason={} cell='{}' cellForm=0x{:08X} world='{}' pos=({:.1f},{:.1f},{:.1f})",
				a_reason,
				cellEditor,
				cellFormId,
				worldEditor,
				pos.x,
				pos.y,
				pos.z);
			g_lastCellFormId = cellFormId;
		}

		void LogQuestSnapshot(std::string_view a_reason)
		{
			for (std::size_t index = 0; index < kWatchedQuests.size(); ++index) {
				const auto& watched = kWatchedQuests[index];
				const auto* quest = FindWatchedQuest(watched);
				if (!quest) {
					F4T::DebugLogging::Log(
						"IntroQuestProbe",
						"snapshot reason={} quest='{}' missing",
						a_reason,
						watched.editorId);
					continue;
				}

				F4T::DebugLogging::Log(
					"IntroQuestProbe",
					"snapshot reason={} quest='{}' form=0x{:08X} stage={} active={} alreadyRun={}",
					a_reason,
					watched.editorId,
					quest->GetFormID(),
					quest->currentStage,
					quest->GetActive(),
					quest->alreadyRun);
				g_lastLoggedStage[index] = quest->currentStage;
				g_haveLastLoggedStage[index] = true;
			}
		}
	}

	void BeginFirstJoinTrace()
	{
		g_lastCellFormId = 0;
		g_lastPrestonCellFormId = 0;
		for (auto& have : g_haveLastLoggedStage) {
			have = false;
		}
		g_tracing.store(true, std::memory_order_relaxed);
		F4T::DebugLogging::Log("IntroQuestProbe", "begin first-join quest trace");
		LogQuestSnapshot("begin");
		LogPlayerCell("begin");
		const auto preston = RefreshPreston();
		g_lastPrestonCellFormId = PrestonCellFormId(preston.get());
		LogPreston("begin", preston.get());
	}

	void EndFirstJoinTrace()
	{
		if (!g_tracing.exchange(false)) {
			return;
		}

		LogQuestSnapshot("end");
		LogPlayerCell("end");
		const auto preston = RefreshPreston();
		LogPreston("end", preston.get());
		F4T::DebugLogging::Log("IntroQuestProbe", "end first-join quest trace");
	}

	void Tick()
	{
		if (!g_tracing.load(std::memory_order_relaxed)) {
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const auto cellFormId = cell ? cell->GetFormID() : 0;
		if (cellFormId != g_lastCellFormId) {
			LogPlayerCell("cell-change");
		}

		const auto preston = RefreshPreston();
		const auto prestonCellFormId = PrestonCellFormId(preston.get());
		if (prestonCellFormId != g_lastPrestonCellFormId) {
			LogPreston("cell-change", preston.get());
			g_lastPrestonCellFormId = prestonCellFormId;
		}

		bool stageChanged = false;
		for (std::size_t index = 0; index < kWatchedQuests.size(); ++index) {
			const auto& watched = kWatchedQuests[index];
			const auto* quest = FindWatchedQuest(watched);
			if (!quest) {
				continue;
			}
			if (g_haveLastLoggedStage[index] && g_lastLoggedStage[index] == quest->currentStage) {
				continue;
			}
			F4T::DebugLogging::Log(
				"IntroQuestProbe",
				"poll at={:%H:%M:%S} quest='{}' form=0x{:08X} stage={} active={}",
				std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) },
				watched.editorId,
				quest->GetFormID(),
				quest->currentStage,
				quest->GetActive());
			g_lastLoggedStage[index] = quest->currentStage;
			g_haveLastLoggedStage[index] = true;
			stageChanged = true;
		}
		if (stageChanged) {
			LogPreston("stage-change", preston.get());
		}
	}

	void LogNpcDecision(std::string_view a_decision, std::uint32_t a_sourceFormId, std::uint32_t a_baseFormId)
	{
		if (!g_tracing.load(std::memory_order_relaxed) || a_sourceFormId == 0 ||
		    a_sourceFormId != g_prestonFormId.load(std::memory_order_relaxed)) {
			return;
		}
		F4T::DebugLogging::Log(
			"IntroQuestProbe",
			"npc-decision preston decision={} source=0x{:08X} base=0x{:08X}",
			a_decision,
			a_sourceFormId,
			a_baseFormId);
	}

	void LogServerPositionCorrection(std::string_view a_payload)
	{
		if (!g_tracing.load(std::memory_order_relaxed)) {
			return;
		}
		F4T::DebugLogging::Log(
			"IntroQuestProbe",
			"server positionCorrection received payload={}",
			F4T::DebugLogging::SanitizeLogText(a_payload, 512));
	}
}
