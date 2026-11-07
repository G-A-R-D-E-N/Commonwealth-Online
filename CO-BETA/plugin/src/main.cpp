// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TRemotePlayerState.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDeathBagGrant.h"
#include "F4TDeathBagView.h"
#include "F4TSaveLineageSession.h"
#include "F4TNetworking.h"
#include "F4TObjectSync.h"
#include "F4TDebugLogging.h"
#include "F4TDownedUi.h"
#include "F4TNpcSync.h"
#include "F4TRuntimeSelect.h"
#include "F4TUiPlugin.h"
#include "F4TNpcActorGuard.h"
#include "F4TProxyAttackHitGuard.h"
#include "F4TLocalAnimationGraphDebug.h"
#include "F4TLoadOrderSync.h"
#include "F4TModpackAutoFixClient.h"
#include "F4TUIClient.h"
#include "F4TUIClientModpackProgress.h"
#include "F4TUIApiGuard.h"
#include "F4TTextEntryMode.h"
#include "F4TVoiceRuntime.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSurvivalPolicyClient.h"
#include "F4TProxyActorController.h"
#include "F4TProxySaveSafety.h"
#include "F4TProxyAnimationSync.h"
#include "F4TProxyPuppet.h"
#include "F4TWorkshopCommitHooks.h"
#include "F4TWorkshopEntryHook.h"
#include "F4TSleepBedHook.h"
#include "F4TPowerArmorExitHook.h"
#include "F4TParkedSuitSync.h"
#include "F4TSleepMarker.h"
#include "F4TSteamJoinPrompt.h"
#include "F4TStructureHitClient.h"
#include "F4TInventoryHooks.h"
#include "F4TInventoryGrant.h"
#include "F4TMapKey.h"
#include "F4TMapFogMemory.h"
#include "F4TPipboyUnpause.h"
#include "F4TJoinFlow.h"
#include "F4TSoloCell.h"
#include "F4TWeaponClassification.h"
#include "F4TWorldStateSync.h"
#include "F4TWorldPolicyEnforcer.h"
#include "F4TRangedCombatCapture.h"
#include "F4TFrameLimiter.h"
#include "F4TReplicationRuntime.h"
#include "F4TDifficultyReportClient.h"
#include "F4TContainerChangedHub.h"
#include "F4TCombatActionContract.h"
#include "F4TF4SECapabilities.h"
#include "F4TCrashForensics.h"
#include "F4TAdminClient.h"
#include "F4TTradeWindow.h"
#include "F4TTeleportOverlay.h"
#include "F4TPreviousLog.h"
#include "F4TQuestPapyrus.h"
#include "F4TSurvivalNeedsClient.h"
#include "F4TQuestUI.h"
#include "F4TStateBagSync.h"

#include "RE/A/ACTOR_STANCE.h"
#include "RE/A/ACTOR_VALUE_MODIFIER.h"
#include "RE/A/ActorValue.h"
#include "RE/B/BIPED_OBJECT.h"
#include "RE/B/BipedAnim.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSPointerHandle.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/S/Setting.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <format>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "F4TLocalPlayerState.h"

using F4T::LocalPlayerState::GetLocalPlayerLogMessage;
using F4T::LocalPlayerState::LogWarningWithLocalPlayerPrefix;

namespace
{
	constexpr std::uint32_t kStandInSerializationUniqueId = 'COPS';
	constexpr std::uint32_t kStandInRecordType = 'STIN';
	constexpr std::uint32_t kStandInRecordVersion = 1;

	std::atomic<std::uint64_t> g_loadOrderTick{};

	std::uint64_t NextLoadOrderTick()
	{
		return g_loadOrderTick.fetch_add(1, std::memory_order_acq_rel) + 1;
	}

	void F4SEAPI OnStandInSave(const F4SE::SerializationInterface* a_intfc)
	{
		const auto formIds = F4T::ProxySaveSafety::StandInFormIdsForSave();
		if (F4T::ProxySaveSafety::IsSaveTeardownPending()) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"WARN [RT-VERIFY 408-A] the cosave is written while the save teardown is still queued; {} stand-in ids recorded",
				formIds.size());
		}
		if (!a_intfc->WriteRecord(
				kStandInRecordType,
				kStandInRecordVersion,
				formIds.data(),
				static_cast<std::uint32_t>(formIds.size() * sizeof(std::uint32_t)))) {
			F4T::DebugLogging::Log("ProxySaveSafety", "the stand-in record could not be written; this save keeps its leftover stand-ins");
		}
		F4T::MapFogMemory::Save(a_intfc);
		F4T::DeathBagGrantLedger::SaveReceipts(a_intfc);
	}

	// TODO-verify(#407): cosave load order before kPostLoadGame source:rt-briefs/thread-408.md (comment 20722)
	void F4SEAPI OnStandInLoad(const F4SE::SerializationInterface* a_intfc)
	{
		std::vector<std::uint32_t> formIds;
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		std::size_t standInIds = 0;
		bool standInsSeen = false;
		const auto loadTick = NextLoadOrderTick();
		F4T::MapFogMemory::Clear();
		auto hasRecord = a_intfc->GetNextRecordInfo(type, version, length);
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-A] cosave load callback tick={} thread={} hasRecord={} type={:08X} version={} length={}",
			loadTick,
			REX::W32::GetCurrentThreadId(),
			hasRecord,
			type,
			version,
			length);
		while (hasRecord) {
			if (type == F4T::DeathBagGrantLedger::ReceiptRecordType) {
				F4T::DeathBagGrantLedger::LoadReceipts(a_intfc, version, length);
			} else if (type == F4T::MapFogMemory::kRecordType) {
				F4T::MapFogMemory::Load(a_intfc, version, length);
			} else if (type == kStandInRecordType && version == kStandInRecordVersion && !standInsSeen) {
				standInsSeen = true;
				standInIds = length / sizeof(std::uint32_t);
				if (!F4T::ProxySaveSafety::IsValidStandInRecordLength(length)) {
					if (length % sizeof(std::uint32_t) != 0) {
						F4T::DebugLogging::Log("ProxySaveSafety", "the stand-in record length {} is not a whole id list; loading no stand-ins", length);
					} else {
						F4T::DebugLogging::Log(
							"ProxySaveSafety",
							"the stand-in record holds {} ids, over the bound of {}; loading no stand-ins",
							length / sizeof(std::uint32_t),
							F4T::ProxySaveSafety::kMaximumStandInFormIds);
					}
				} else {
					std::vector<std::uint32_t> savedFormIds(length / sizeof(std::uint32_t));
					if (a_intfc->ReadRecordData(savedFormIds.data(), length) != length) {
						F4T::DebugLogging::Log("ProxySaveSafety", "the stand-in record was read short; loading no stand-ins");
					} else {
						for (const auto savedFormId : savedFormIds) {
							// TODO-verify(#407): ResolveFormID on 0xFF ids source:rt-briefs/thread-408.md (comment 20722)
							const auto resolved = a_intfc->ResolveFormID(savedFormId);
							F4T::DebugLogging::Log(
								"ProxySaveSafety",
								"runtime-unknown [RT-VERIFY 408-B] ResolveFormID saved={:08X} resolved={} result={:08X}",
								savedFormId,
								static_cast<bool>(resolved),
								resolved ? *resolved : 0u);
							if (resolved) {
								formIds.push_back(*resolved);
							}
						}
					}
				}
			}
			hasRecord = a_intfc->GetNextRecordInfo(type, version, length);
		}
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"[RT-VERIFY 408-A] cosave stand-in load tick={} thread={} ids={} kept={}",
			loadTick,
			REX::W32::GetCurrentThreadId(),
			standInIds,
			formIds.size());
		F4T::ProxySaveSafety::LoadStandInFormIds(std::move(formIds));
	}

	void F4SEAPI OnStandInRevert(const F4SE::SerializationInterface*)
	{
		F4T::ProxySaveSafety::ClearStandInFormIds();
		F4T::MapFogMemory::Clear();
	}

	void RunProxyLoadSweepOnMainThread(bool a_clearStandIns, std::uint64_t a_survivalLoad = 0)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			F4T::DeathBagView::SweepSavedContainers();
			F4T::ProxySaveSafety::AfterLoadSweep(false, a_clearStandIns);
			if (a_survivalLoad != 0) F4T::SurvivalPolicy::g_clientState.CompleteLoad(a_survivalLoad);
			return;
		}

		taskInterface->AddTask([a_clearStandIns, a_survivalLoad]() {
			F4T::DeathBagView::SweepSavedContainers();
			F4T::ProxySaveSafety::AfterLoadSweep(false, a_clearStandIns);
			if (a_survivalLoad != 0) F4T::SurvivalPolicy::g_clientState.CompleteLoad(a_survivalLoad);
		});
	}

	void StartPlayerPositionPolling()
	{
		static std::atomic<bool> pollingStarted{ false };
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogWarningWithLocalPlayerPrefix("Failed to get F4SE task interface; player position changes will not be sent or logged.");
			return;
		}

		if (pollingStarted.exchange(true)) {
			return;
		}

		F4T::ProxyPuppet::Install();
		F4T::NpcActorGuard::Install();
		F4T::ProxyAttackHitGuard::Install();
		F4T::WorkshopCommitHooks::Install();
		F4T::StructureHitClient::Install();
		F4T::InventoryHooks::Install();
		F4T::SessionSaveGuard::Install();
		F4T::SleepBedHook::Install();
		F4T::WorkshopEntryHook::Install();
		F4T::PowerArmorExitHook::Install();

		F4T::F4SECapabilities::AddPermanentTask([]() {
			F4T::MapKey::Install();
			F4T::PipboyUnpause::Install();
			F4T::ReplicationRuntime::Tick();
			F4T::SleepBedHook::Tick();
		});
	}

	bool F4SEAPI RegisterPapyrusDebug(RE::BSScript::IVirtualMachine* a_vm)
	{
		return F4T::DebugLogging::RegisterPapyrus(a_vm);
	}

	bool F4SEAPI RegisterPapyrusQuest(RE::BSScript::IVirtualMachine* a_vm)
	{
		return F4T::QuestPapyrus::RegisterPapyrus(a_vm);
	}

	bool F4SEAPI RegisterPapyrusNeeds(RE::BSScript::IVirtualMachine* a_vm)
	{
		return F4T::SurvivalNeedsClient::RegisterPapyrus(a_vm);
	}

	void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		F4T::DebugLogging::Log("F4SE", "message type={}", a_msg->type);
		F4T::UiPlugin::OnMessage(a_msg);

		switch (a_msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			F4T::ProxySaveSafety::NoteGameThreadId();
			if (auto* setting = RE::GetINISetting("bDisplayMissingContentDialogue:SaveGame"); setting && setting->GetType() == RE::Setting::SETTING_TYPE::kBinary) {
				setting->SetBinary(false);
				F4T::DebugLogging::Log("F4SE", "bDisplayMissingContentDialogue:SaveGame set to 0");
			} else {
				F4T::DebugLogging::Log("F4SE", "bDisplayMissingContentDialogue:SaveGame not found; missing-content box stays on");
			}
			F4T::LoadOrderSync::CaptureLoadedPlugins();
			F4T::UIClient::OnGameDataReady();
			F4T::VoiceRuntime::Start();
			F4T::QuestUI::Attach();
			F4T::Admin::Attach();
			F4T::TradeWindow::Attach();
			F4T::StateBagSync::Attach();
			F4T::Downed::AttachDownedUi();
			F4T::TeleportOverlay::AttachUi();
			F4T::ModpackAutoFixClient::OnGameDataReady();
			F4T::RangedCombatCapture::Initialize();
			F4T::FrameLimiter::Install();
			break;
		case F4SE::MessagingInterface::kPreLoadGame: {
			F4T::SurvivalPolicy::g_clientState.BeginTrackedLoad();
			F4T::ProxySaveSafety::DropPrivateProxyBasesWithoutFreeing();
			F4T::DeathBagGrant::OnLoad();
			F4T::DeathBagGrantLedger::NoteLoad(a_msg->data ? std::string{ static_cast<const char*>(a_msg->data), strnlen(static_cast<const char*>(a_msg->data), (std::min)(static_cast<std::size_t>(a_msg->dataLen), std::size_t{ 260 })) } : std::string{});
			F4T::DifficultyReportClient::OnLoad();
			F4T::InventoryGrant::OnPreLoad(static_cast<const char*>(a_msg->data), a_msg->dataLen);
			const auto loadTick = NextLoadOrderTick();
			F4T::JoinFlow::OnWorldReset(false);
			F4T::ParkedSuitSync::OnWorldReset();
			const auto recorded = F4T::ProxySaveSafety::StandInFormIdCount();
			F4T::ProxySaveSafety::ClearStandInFormIds();
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-A] kPreLoadGame tick={} thread={} cleared={}",
				loadTick,
				REX::W32::GetCurrentThreadId(),
				recorded);
			break;
		}
		case F4SE::MessagingInterface::kPreSaveGame:
			F4T::WorldPolicyEnforcer::ReleaseForSave();
			F4T::ProxySaveSafety::PrepareForSave();
			{
				const auto saveName = F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg);
				F4T::DeathBagGrantLedger::BeginSave(saveName, F4T::SaveLineageSession::CaptureSaveBaseline(saveName));
			}
			break;
		case F4SE::MessagingInterface::kPostSaveGame:
			F4T::WorldPolicyEnforcer::ReapplyAfterSave();
			F4T::SessionSaveGuard::NotePostSaveGame();
			F4T::DeathBagGrantLedger::FinishSave(F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg));
			F4T::SessionLaunch::OnF4SEMessage(a_msg);
			break;
		case F4SE::MessagingInterface::kNewGame: {
			const auto survivalLoad = F4T::SurvivalPolicy::g_clientState.BeginLoad();
			F4T::InventoryGrant::OnNewGame();
			F4T::ProxySaveSafety::ClearStandInFormIds();
			RunProxyLoadSweepOnMainThread(true, survivalLoad);
			F4T::DeathBagGrant::OnLoad();
			F4T::DeathBagGrantLedger::NewGame();
			F4T::DifficultyReportClient::OnLoad();
			F4T::JoinFlow::OnWorldReset(true);
			F4T::ParkedSuitSync::OnWorldReset();
			StartPlayerPositionPolling();
			F4T::SessionLaunch::OnF4SEMessage(a_msg);
			break;
		}
		case F4SE::MessagingInterface::kGameLoaded: {
			// TODO-verify(#407): kGameLoaded order vs the cosave load callback source:rt-briefs/thread-408.md (comment 20722)
			const auto loadTick = NextLoadOrderTick();
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"runtime-unknown [RT-VERIFY 408-A] kGameLoaded tick={} thread={} recordedStandIns={} clearStandIns=false",
				loadTick,
				REX::W32::GetCurrentThreadId(),
				F4T::ProxySaveSafety::StandInFormIdCount());
			RunProxyLoadSweepOnMainThread(false);
			StartPlayerPositionPolling();
			F4T::SessionLaunch::OnF4SEMessage(a_msg);
			break;
		}
		case F4SE::MessagingInterface::kPostLoadGame: {
			const auto survivalLoad = F4T::SurvivalPolicy::g_clientState.TakePendingLoad();
			if (survivalLoad == 0) F4T::SurvivalPolicy::g_clientState.UntrackedLoad();
			const auto loadSucceeded = F4T::ProxySaveSafety::IsSuccessfulLoadMessageData(reinterpret_cast<std::uintptr_t>(a_msg->data));
			F4T::InventoryGrant::OnPostLoad(loadSucceeded);
			const auto postLoadTick = NextLoadOrderTick();
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-A] kPostLoadGame tick={} thread={} loadSucceeded={}",
				postLoadTick,
				REX::W32::GetCurrentThreadId(),
				loadSucceeded);
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (taskInterface) {
				taskInterface->AddTask([loadSucceeded, survivalLoad]() {
					F4SE::MessagingInterface::Message postLoadMessage{};
					postLoadMessage.type = F4SE::MessagingInterface::kPostLoadGame;
					F4T::ObjectSync::RefreshTrackedLocks();
					F4T::WorldPolicyEnforcer::ReapplyAfterSave();
					F4T::DeathBagView::SweepSavedContainers();
					F4T::ProxySaveSafety::AfterLoadSweep(loadSucceeded, !loadSucceeded);
					if (survivalLoad != 0) F4T::SurvivalPolicy::g_clientState.CompleteLoad(survivalLoad, loadSucceeded);
					StartPlayerPositionPolling();
					F4T::SessionLaunch::OnF4SEMessage(&postLoadMessage);
					if (loadSucceeded) {
						F4T::UIApiGuard::ShowPendingNotice();
					}
				});
			} else {
				F4T::DeathBagView::SweepSavedContainers();
				F4T::ProxySaveSafety::AfterLoadSweep(loadSucceeded, !loadSucceeded);
				if (survivalLoad != 0) F4T::SurvivalPolicy::g_clientState.CompleteLoad(survivalLoad, loadSucceeded);
				StartPlayerPositionPolling();
				F4T::SessionLaunch::OnF4SEMessage(a_msg);
			}
			F4T::DeathBagGrantLedger::FinishReceiptLoad(loadSucceeded);
			break;
		}
		default:
			break;
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	// Must run before F4SE::Init: CommonLibF4 opens the plugin log and truncates it.
	F4T::PreviousLog::KeepPreviousLog(a_f4se);
	F4SE::Init(a_f4se);
	if (F4T::RuntimeSelect::LegacyDllsPresent()) {
		REX::WARN("Commonwealth Online: delete CommonwealthOnline-NG.dll, CommonwealthOnline-OG.dll and CommonwealthOnlineUI.dll from Data/F4SE/Plugins. This one CommonwealthOnline.dll replaces them. Not loading.");
		::MessageBoxW(nullptr, L"Commonwealth Online did not load. Delete CommonwealthOnline-NG.dll, CommonwealthOnline-OG.dll and CommonwealthOnlineUI.dll from Data/F4SE/Plugins. CommonwealthOnline.dll replaces them.", L"Commonwealth Online", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return false;
	}
	F4T::RuntimeSelect::RegisterClientTarget();
	F4T::DebugLogging::Initialize();
	F4T::SleepMarker::Install();
	F4T::SteamJoinPrompt::Install();
	F4T::ModpackAutoFixClient::OnPluginLoad();
	F4T::UIClientModpackProgress::Install();
	F4T::CrashForensics::Install();
	F4T::UIApiGuard::Install();
	F4T::ProxySaveSafety::NoteGameThreadId();

	if (const auto* serialization = F4SE::GetSerializationInterface()) {
		serialization->SetUniqueID(kStandInSerializationUniqueId);
		serialization->SetSaveCallback(OnStandInSave);
		serialization->SetLoadCallback(OnStandInLoad);
		serialization->SetRevertCallback(OnStandInRevert);
	} else {
		LogWarningWithLocalPlayerPrefix("F4SE serialization interface unavailable; proxy stand-ins will not be recorded in saves.");
	}

	REX::INFO("{}", GetLocalPlayerLogMessage("Commonwealth Online plugin loaded successfully."));
	F4T::DebugLogging::Log("Plugin", "load interface initialized");

	std::atexit(F4T::Networking::DisconnectFromLocalServer);
	std::atexit(F4T::JoinFlow::Shutdown);
	std::atexit(F4T::RangedCombatCapture::Shutdown);
	std::atexit(F4T::ContainerChangedHub::Shutdown);
	std::atexit(F4T::TextEntryMode::Reset);

	F4T::UiPlugin::OnLoad();

	const auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
		LogWarningWithLocalPlayerPrefix("Failed to register F4SE message listener; player position will not be logged.");
	} else {
		F4T::DebugLogging::Log("Plugin", "F4SE message listener registered");
	}

	const auto* papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(RegisterPapyrusDebug)) {
		LogWarningWithLocalPlayerPrefix("Failed to register Commonwealth Online debug Papyrus natives.");
	} else {
		F4T::DebugLogging::Log("Plugin", "Papyrus debug natives registered");
	}
	if (!papyrus || !papyrus->Register(RegisterPapyrusQuest)) {
		LogWarningWithLocalPlayerPrefix("Failed to register Commonwealth Online quest Papyrus natives.");
	} else {
		F4T::DebugLogging::Log("Plugin", "Papyrus quest natives registered");
	}
	if (!papyrus || !papyrus->Register(RegisterPapyrusNeeds)) {
		LogWarningWithLocalPlayerPrefix("Failed to register Commonwealth Online needs Papyrus natives.");
	} else {
		F4T::DebugLogging::Log("Plugin", "Papyrus needs natives registered");
	}

	return true;
}
