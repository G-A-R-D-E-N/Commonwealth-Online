// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TRemotePlayerState.h"
#include "F4TNetworking.h"
#include "F4TObjectSync.h"
#include "F4TDebugLogging.h"
#include "F4TDownedUi.h"
#include "F4TNpcSync.h"
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
#include "F4TProxyActorController.h"
#include "F4TProxySaveSafety.h"
#include "F4TProxyAnimationSync.h"
#include "F4TProxyPuppet.h"
#include "F4TWorkshopCommitHooks.h"
#include "F4TInventoryHooks.h"
#include "F4TJoinFlow.h"
#include "F4TSoloCell.h"
#include "F4TWeaponClassification.h"
#include "F4TWorldStateSync.h"
#include "F4TWorldPolicyEnforcer.h"
#include "F4TRangedCombatCapture.h"
#include "F4TFrameLimiter.h"
#include "F4TReplicationRuntime.h"
#include "F4TContainerChangedHub.h"
#include "F4TCombatActionContract.h"
#include "F4TF4SECapabilities.h"
#include "F4TCrashForensics.h"
#include "F4TCrashReport.h"
#include "F4TQuestPapyrus.h"
#include "F4TQuestUI.h"

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
	}

	// TODO-verify(#407): cosave load order before kPostLoadGame source:rt-briefs/thread-408.md (comment 20722)
	void F4SEAPI OnStandInLoad(const F4SE::SerializationInterface* a_intfc)
	{
		std::vector<std::uint32_t> formIds;
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		const auto loadTick = NextLoadOrderTick();
		const auto hasRecord = a_intfc->GetNextRecordInfo(type, version, length);
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-A] cosave load callback tick={} thread={} hasRecord={} type={:08X} version={} length={}",
			loadTick,
			REX::W32::GetCurrentThreadId(),
			hasRecord,
			type,
			version,
			length);
		if (hasRecord && type == kStandInRecordType && version == kStandInRecordVersion) {
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
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"[RT-VERIFY 408-A] cosave stand-in load tick={} thread={} ids={} kept={}",
			loadTick,
			REX::W32::GetCurrentThreadId(),
			length / sizeof(std::uint32_t),
			formIds.size());
		F4T::ProxySaveSafety::LoadStandInFormIds(std::move(formIds));
	}

	void F4SEAPI OnStandInRevert(const F4SE::SerializationInterface*)
	{
		F4T::ProxySaveSafety::ClearStandInFormIds();
	}

	void RunProxyLoadSweepOnMainThread(bool a_clearStandIns)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			F4T::ProxySaveSafety::AfterLoadSweep(false, a_clearStandIns);
			return;
		}

		taskInterface->AddTask([a_clearStandIns]() {
			F4T::ProxySaveSafety::AfterLoadSweep(false, a_clearStandIns);
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
		F4T::InventoryHooks::Install();
		F4T::SessionSaveGuard::Install();

		F4T::F4SECapabilities::AddPermanentTask([]() {
			F4T::ReplicationRuntime::Tick();
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

	void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		F4T::DebugLogging::Log("F4SE", "message type={}", a_msg->type);

		switch (a_msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			F4T::CrashReport::Reassert();
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
			F4T::Downed::AttachDownedUi();
			F4T::ModpackAutoFixClient::OnGameDataReady();
			F4T::RangedCombatCapture::Initialize();
			F4T::FrameLimiter::Install();
			break;
		case F4SE::MessagingInterface::kPreLoadGame: {
			const auto loadTick = NextLoadOrderTick();
			F4T::JoinFlow::OnWorldReset(false);
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
			break;
		case F4SE::MessagingInterface::kPostSaveGame:
			F4T::WorldPolicyEnforcer::ReapplyAfterSave();
			F4T::SessionSaveGuard::NotePostSaveGame();
			F4T::SessionLaunch::OnF4SEMessage(a_msg);
			break;
		case F4SE::MessagingInterface::kNewGame:
			F4T::ProxySaveSafety::ClearStandInFormIds();
			RunProxyLoadSweepOnMainThread(true);
			F4T::JoinFlow::OnWorldReset(true);
			StartPlayerPositionPolling();
			F4T::SessionLaunch::OnF4SEMessage(a_msg);
			break;
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
			F4T::CrashReport::Reassert();
			const auto loadSucceeded = F4T::ProxySaveSafety::IsSuccessfulLoadMessageData(reinterpret_cast<std::uintptr_t>(a_msg->data));
			const auto postLoadTick = NextLoadOrderTick();
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-A] kPostLoadGame tick={} thread={} loadSucceeded={}",
				postLoadTick,
				REX::W32::GetCurrentThreadId(),
				loadSucceeded);
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (taskInterface) {
				taskInterface->AddTask([loadSucceeded]() {
					F4SE::MessagingInterface::Message postLoadMessage{};
					postLoadMessage.type = F4SE::MessagingInterface::kPostLoadGame;
					F4T::ObjectSync::RefreshTrackedLocks();
					F4T::WorldPolicyEnforcer::ReapplyAfterSave();
					F4T::ProxySaveSafety::AfterLoadSweep(loadSucceeded, !loadSucceeded);
					StartPlayerPositionPolling();
					F4T::SessionLaunch::OnF4SEMessage(&postLoadMessage);
					if (loadSucceeded) {
						F4T::UIApiGuard::ShowPendingNotice();
					}
				});
			} else {
				F4T::ProxySaveSafety::AfterLoadSweep(loadSucceeded, !loadSucceeded);
				StartPlayerPositionPolling();
				F4T::SessionLaunch::OnF4SEMessage(a_msg);
			}
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
	F4T::CrashReport::KeepPreviousLog(a_f4se);
	F4SE::Init(a_f4se);
	F4T::DebugLogging::Initialize();
	F4T::ModpackAutoFixClient::OnPluginLoad();
	F4T::UIClientModpackProgress::Install();
	F4T::CrashForensics::Install();
	F4T::CrashReport::Install();
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

	return true;
}
