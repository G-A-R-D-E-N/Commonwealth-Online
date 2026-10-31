#include "pch.h"

#include "F4TDownedEngine.h"

#include "F4TConsoleGuard.h"
#include "F4TDebugLogging.h"
#include "F4TLocalPlayerMovement.h"
#include "F4TProxyActorController.h"

#include "RE/A/ACTOR_VALUE_MODIFIER.h"
#include "RE/B/BGSAction.h"
#include "RE/B/BGSDefaultObjectManager.h"
#include "RE/B/BGSInventoryItem.h"
#include "RE/B/BGSInventoryList.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/ButtonEvent.h"
#include "RE/C/Console.h"
#include "RE/D/DEFAULT_OBJECT.h"
#include "RE/I/InputEvent.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/E/ExtraInstanceData.h"
#include "RE/L/LoadingMenu.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESRace.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/PlayerInputHandler.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"
#include "RE/U/UI.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

namespace
{
	constexpr std::string_view kSanctuaryCellEditorId{ "SanctuaryExt" };
	constexpr std::string_view kActivateUserEvent{ "Activate" };
	constexpr std::string_view kLogComponent{ "Downed" };
	constexpr std::uint32_t kStimpakFormId{ 0x00023736 };
	constexpr std::uint32_t kPlayerCanStimpakKeywordFormId{ 0x000AD52A };
	constexpr std::uint32_t kHumanRaceFormId{ 0x00013746 };

	bool g_sessionFlagsApplied = false;
	bool g_loadingSkipLogged = false;
	std::string g_lastBedCellEditorId;

	class ReviveInputHandler final : public RE::PlayerInputHandler
	{
	public:
		explicit ReviveInputHandler(RE::PlayerControlsData& a_data) noexcept :
			RE::PlayerInputHandler(a_data)
		{}

		[[nodiscard]] bool IsActivateHeld() const noexcept { return activateHeld_; }

		bool ShouldHandleEvent(const RE::InputEvent* a_event) override
		{
			return a_event != nullptr && a_event->eventType.get() == RE::INPUT_EVENT_TYPE::kButton;
		}

		void OnButtonEvent(const RE::ButtonEvent* a_event) override
		{
			if (!a_event) {
				return;
			}
			if (a_event->QUserEvent() == kActivateUserEvent) {
				activateHeld_ = a_event->QPressed();
			}
		}

	private:
		bool activateHeld_{ false };
	};

	ReviveInputHandler* g_inputHandler = nullptr;
	bool g_inputHandlerInstalled = false;

	bool IsInBleedoutAnimation(RE::Actor& a_actor)
	{
		return a_actor.boolFlags.any(RE::Actor::BOOL_FLAGS::kInBleedoutAnimation);
	}
}

namespace F4T::Downed::Engine
{
	double NowSeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void LogInfo(std::string_view a_message)
	{
		F4T::DebugLogging::Write(kLogComponent, a_message);
	}

	bool CanTouchWorld()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->GetMenuOpen<RE::LoadingMenu>()) {
			g_loadingSkipLogged = false;
			return true;
		}
		if (!g_loadingSkipLogged) {
			g_loadingSkipLogged = true;
			LogInfo("Skipping downed engine writes while the loading menu is open.");
		}
		return false;
	}

	RE::PlayerCharacter* LocalPlayer()
	{
		return RE::PlayerCharacter::GetSingleton();
	}

	const RE::ActorValueInfo* HealthInfo()
	{
		auto* actorValues = RE::ActorValue::GetSingleton();
		return actorValues ? actorValues->health : nullptr;
	}

	float GetMaximumHealth(RE::Actor* a_actor, const RE::ActorValueInfo& a_health)
	{
		const auto permanent = a_actor->GetPermanentActorValue(a_health);
		if (std::isfinite(permanent) && permanent > 0.0F) {
			return permanent;
		}
		const auto current = a_actor->GetActorValue(a_health);
		return std::isfinite(current) ? (std::max)(current, 0.0F) : 0.0F;
	}

	bool SetHealth(RE::Actor& a_actor, const RE::ActorValueInfo& a_health, float a_target)
	{
		const auto current = a_actor.GetActorValue(a_health);
		if (!std::isfinite(current) || !std::isfinite(a_target)) {
			return false;
		}
		const auto delta = a_target - current;
		if (std::fabs(delta) < 0.01F) {
			return true;
		}
		if (delta > 0.0F) {
			a_actor.RestoreActorValue(a_health, delta);
		} else {
			a_actor.ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, a_health, delta);
		}
		const auto after = a_actor.GetActorValue(a_health);
		const bool moved = std::isfinite(after) && std::fabs(after - current) > 0.001F;
		if (!moved) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"health write did not move actor {:08X}: target={:.1f} current={:.1f} after={:.1f}",
				a_actor.GetFormID(),
				a_target,
				current,
				after);
		}
		return moved;
	}

	bool SetPose(RE::Actor& a_actor, bool a_downed)
	{
		auto* manager = RE::BGSDefaultObjectManager::GetSingleton();
		auto* action = manager ?
			manager->GetDefaultObject<RE::BGSAction>(
				a_downed ? RE::DEFAULT_OBJECT::kActionBleedoutStart : RE::DEFAULT_OBJECT::kActionBleedoutStop) :
			nullptr;
		if (!action) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"bleedout {} action missing; actor {:08X} pose unchanged",
				a_downed ? "start" : "stop",
				a_actor.GetFormID());
			return false;
		}
		const auto accepted = a_actor.PerformAction(action, nullptr);
		F4T::DebugLogging::Log(
			kLogComponent,
			"bleedout {} on actor {:08X} accepted={}",
			a_downed ? "start" : "stop",
			a_actor.GetFormID(),
			accepted);
		return accepted;
	}

	bool SetControlBlocked(RE::Actor& a_actor, bool a_blocked)
	{
		if (a_blocked) {
			a_actor.boolFlags.set(
				RE::Actor::BOOL_FLAGS::kMovementBlocked,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled);
		} else {
			a_actor.boolFlags.reset(
				RE::Actor::BOOL_FLAGS::kMovementBlocked,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled);
		}
		return true;
	}

	void ApplySessionFlags(bool a_apply)
	{
		if (g_sessionFlagsApplied == a_apply) {
			return;
		}
		auto* player = LocalPlayer();
		if (!player) {
			return;
		}
		if (a_apply) {
			player->boolFlags.reset(
				RE::Actor::BOOL_FLAGS::kMovementBlocked,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled);
			player->boolFlags.set(
				RE::Actor::BOOL_FLAGS::kEssential,
				RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
		} else {
			if (!CanTouchWorld()) {
				return;
			}
			SetPose(*player, false);
			SetControlBlocked(*player, false);
			player->boolFlags.reset(
				RE::Actor::BOOL_FLAGS::kEssential,
				RE::Actor::BOOL_FLAGS::kNoBleedoutRecovery);
		}
		g_sessionFlagsApplied = a_apply;
		F4T::DebugLogging::Log(kLogComponent, "session essential/bleedout flags applied={}", a_apply);
	}

	void InstallReviveInput()
	{
		if (g_inputHandlerInstalled) {
			return;
		}
		auto* controls = RE::PlayerControls::GetSingleton();
		if (!controls) {
			return;
		}
		g_inputHandler = new ReviveInputHandler(controls->data);
		controls->RegisterHandler(g_inputHandler);
		g_inputHandlerInstalled = true;
		LogInfo("revive input handler registered with PlayerControls.");
	}

	bool IsActivateHeld()
	{
		return g_inputHandler != nullptr && g_inputHandler->IsActivateHeld();
	}

	void RestoreLocalPlayerHealth()
	{
		auto* player = LocalPlayer();
		const auto* health = HealthInfo();
		if (!player || !health || !CanTouchWorld()) {
			return;
		}
		(void)SetHealth(*player, *health, GetMaximumHealth(player, *health));
	}

	bool HasLastBedCell()
	{
		return !g_lastBedCellEditorId.empty();
	}

	void RecordLastBed()
	{
		auto* player = LocalPlayer();
		if (!player || !player->isSleeping) {
			return;
		}
		auto* cell = player->GetParentCell();
		if (!cell) {
			return;
		}
		const auto* editorId = cell->GetFormEditorID();
		if (!editorId || editorId[0] == '\0' || g_lastBedCellEditorId == editorId) {
			return;
		}
		g_lastBedCellEditorId = editorId;
		F4T::DebugLogging::Log(kLogComponent, "recorded last-bed cell editor id '{}'", g_lastBedCellEditorId);
	}

	bool CaptureCheckpoint(Checkpoint& a_out)
	{
		auto* player = LocalPlayer();
		if (!player || !CanTouchWorld()) {
			return false;
		}
		auto* cell = player->GetParentCell();
		if (!cell) {
			return false;
		}
		Checkpoint checkpoint;
		checkpoint.interior = cell->worldSpace == nullptr;
		checkpoint.space = checkpoint.interior ? cell->GetFormID() : cell->worldSpace->GetFormID();
		const auto position = player->GetPosition();
		checkpoint.x = position.x;
		checkpoint.y = position.y;
		checkpoint.z = position.z;
		checkpoint.rotZ = player->GetHeading();
		if (!IsValidCheckpoint(checkpoint)) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"checkpoint capture rejected: space={:08X} pos=({:.0f},{:.0f},{:.0f})",
				checkpoint.space,
				checkpoint.x,
				checkpoint.y,
				checkpoint.z);
			return false;
		}
		a_out = checkpoint;
		return true;
	}

	bool TeleportToCheckpoint(const Checkpoint& a_checkpoint)
	{
		auto* player = LocalPlayer();
		if (!player || !IsValidCheckpoint(a_checkpoint)) {
			return false;
		}
		if (a_checkpoint.interior) {
			auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(a_checkpoint.space);
			if (!cell) {
				F4T::DebugLogging::Log(kLogComponent, "checkpoint cell {:08X} not found", a_checkpoint.space);
				return false;
			}
			player->SetParentCell(cell);
		} else {
			auto* worldspace = RE::TESForm::GetFormByID<RE::TESWorldSpace>(a_checkpoint.space);
			auto* cell = player->GetParentCell();
			if (!worldspace || !cell || cell->worldSpace != worldspace) {
				F4T::DebugLogging::Log(
					kLogComponent,
					"checkpoint worldspace {:08X} is not the current one; cannot place the player there",
					a_checkpoint.space);
				return false;
			}
		}
		player->SetPosition(RE::NiPoint3{ a_checkpoint.x, a_checkpoint.y, a_checkpoint.z }, true);
		player->SetAngleOnReference(RE::NiPoint3{ 0.0F, 0.0F, a_checkpoint.rotZ });
		player->SetHeading(a_checkpoint.rotZ);
		F4T::DebugLogging::Log(
			kLogComponent,
			"respawned at the last save: {} {:08X} ({:.0f},{:.0f},{:.0f})",
			a_checkpoint.interior ? "cell" : "worldspace",
			a_checkpoint.space,
			a_checkpoint.x,
			a_checkpoint.y,
			a_checkpoint.z);
		return true;
	}

	void TeleportForRespawn(
		RespawnPoint a_point,
		std::optional<std::uint32_t> a_friendId,
		const std::optional<Checkpoint>& a_checkpoint)
	{
		F4T::LocalPlayerMovement::RequestExplicitTeleport();
		const auto sanctuaryCommand = std::format("coc {}", kSanctuaryCellEditorId);
		auto runCommand = [](const std::string& a_command) {
				F4T::ConsoleGuard::ExecutePluginCommand(a_command.c_str());
			F4T::DebugLogging::Log(kLogComponent, "respawn console command: {}", a_command);
		};

		switch (a_point) {
		case RespawnPoint::kCheckpoint:
			if (a_checkpoint && TeleportToCheckpoint(*a_checkpoint)) {
				return;
			}
			LogInfo("No usable last-save position; falling back to the last bed.");
			if (!g_lastBedCellEditorId.empty()) {
				runCommand(std::format("coc {}", g_lastBedCellEditorId));
				return;
			}
			runCommand(sanctuaryCommand);
			return;
		case RespawnPoint::kFriend: {
			const auto friendId = a_friendId.value_or(0);
			auto* proxy = friendId != 0 ?
				F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(friendId) :
				nullptr;
			if (proxy) {
				runCommand(std::format("player.moveto {:08X}", proxy->GetFormID()));
				return;
			}
			F4T::DebugLogging::Log(
				kLogComponent,
				"friend respawn target {} has no proxy actor; falling back to Sanctuary",
				friendId);
			runCommand(sanctuaryCommand);
			return;
		}
		case RespawnPoint::kLastBed:
			if (!g_lastBedCellEditorId.empty()) {
				runCommand(std::format("coc {}", g_lastBedCellEditorId));
				return;
			}
			LogInfo("No last-bed cell recorded; falling back to Sanctuary.");
			runCommand(sanctuaryCommand);
			return;
		case RespawnPoint::kSanctuary:
		default:
			runCommand(sanctuaryCommand);
			return;
		}
	}

	std::optional<std::uint32_t> CountStimpaks()
	{
		auto* player = LocalPlayer();
		auto* stimpak = RE::TESForm::GetFormByID<RE::TESBoundObject>(kStimpakFormId);
		if (!player || !stimpak) {
			return std::nullopt;
		}
		return player->GetInventoryObjectCount(stimpak);
	}

	bool SpendStimpak()
	{
		auto* player = LocalPlayer();
		auto* stimpak = RE::TESForm::GetFormByID<RE::TESBoundObject>(kStimpakFormId);
		if (!player || !stimpak || !CanTouchWorld()) {
			return false;
		}
		const auto before = player->GetInventoryObjectCount(stimpak);
		if (before == 0) {
			return false;
		}
		RE::TESObjectREFR::RemoveItemData data{ stimpak, 1 };
		(void)player->RemoveItem(data);
		const auto after = player->GetInventoryObjectCount(stimpak);
		F4T::DebugLogging::Log(kLogComponent, "revive spent a Stimpak: {}", after);
		return after < before;
	}

	bool SetCanStimpak(RE::Actor& a_actor, bool a_on)
	{
		static bool warned = false;
		auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kPlayerCanStimpakKeywordFormId);
		if (!keyword) {
			if (!warned) {
				warned = true;
				LogInfo("playerCanStimpak keyword not found; the revive animation cannot play.");
			}
			return false;
		}
		if (a_actor.HasKeyword(keyword) == a_on) {
			return true;
		}
		if (a_on) {
			a_actor.AddKeyword(keyword);
		} else {
			a_actor.RemoveKeyword(keyword);
		}
		const auto applied = a_actor.HasKeyword(keyword) == a_on;
		F4T::DebugLogging::Log(
			kLogComponent,
			"playerCanStimpak {} on {:08X} applied={}",
			a_on ? "added" : "removed",
			a_actor.GetFormID(),
			applied);
		return applied;
	}

	ReviveIdleFacts ReadReviveIdleFacts(RE::Actor& a_target)
	{
		ReviveIdleFacts facts;
		auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kPlayerCanStimpakKeywordFormId);
		facts.keywordResolved = keyword != nullptr;
		facts.keywordPresent = keyword && a_target.HasKeyword(keyword);
		facts.humanRace = a_target.race && a_target.race->GetFormID() == kHumanRaceFormId;
		facts.targetDead = a_target.IsDead(false);
		return facts;
	}

	bool EnsureCanStimpak(RE::Actor& a_target)
	{
		const auto facts = ReadReviveIdleFacts(a_target);
		if (!facts.keywordResolved) {
			return false;
		}
		if (facts.keywordPresent) {
			return true;
		}
		return SetCanStimpak(a_target, true);
	}

	bool PlayStimpakReviveIdle(RE::Actor& a_reviver, RE::Actor& a_target)
	{
		(void)EnsureCanStimpak(a_target);

		const auto facts = ReadReviveIdleFacts(a_target);
		const auto blocker = DiagnoseReviveIdle(facts);
		const auto stimpaks = CountStimpaks();
		F4T::DebugLogging::Log(
			kLogComponent,
			"revive idle check: target={:08X} race={:08X} humanRace={} keywordResolved={} keywordPresent={} dead={} stimpaks={} -> {}",
			a_target.GetFormID(),
			a_target.race ? a_target.race->GetFormID() : 0,
			facts.humanRace,
			facts.keywordResolved,
			facts.keywordPresent,
			facts.targetDead,
			stimpaks.value_or(0),
			DescribeReviveIdleBlocker(blocker));
		if (blocker != ReviveIdleBlocker::kNone) {
			LogInfo("the Stimpak revive idle was not requested; the check above says why.");
			return false;
		}

		auto* manager = RE::BGSDefaultObjectManager::GetSingleton();
		auto* action = manager ?
			manager->GetDefaultObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionActivate) :
			nullptr;
		if (!action) {
			LogInfo("the Activate action is missing; the Stimpak revive idle cannot play.");
			return false;
		}
		const auto positionBefore = a_target.GetPosition();
		const auto bleedoutBefore = IsInBleedoutAnimation(a_target);
		const auto played = a_reviver.PerformAction(action, &a_target);
		const auto positionAfter = a_target.GetPosition();
		const auto bleedoutAfter = IsInBleedoutAnimation(a_target);
		const auto after = ReadReviveIdleFacts(a_target);
		F4T::DebugLogging::Log(
			kLogComponent,
			"revive idle result: from={:08X} onto={:08X} requested={} keywordAfter={} deadAfter={}",
			a_reviver.GetFormID(),
			a_target.GetFormID(),
			played,
			after.keywordPresent,
			after.targetDead);
		if (!played) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"revive idle refused: onto={:08X} posBefore=({:.1f},{:.1f},{:.1f}) posAfter=({:.1f},{:.1f},{:.1f}) drift={:.2f} bleedoutAnimBefore={} bleedoutAnimAfter={} dead={} keyword={}: the target did not start the Stimpak idle",
				a_target.GetFormID(),
				positionBefore.x,
				positionBefore.y,
				positionBefore.z,
				positionAfter.x,
				positionAfter.y,
				positionAfter.z,
				DistanceBetween(positionAfter, positionBefore),
				bleedoutBefore,
				bleedoutAfter,
				after.targetDead,
				after.keywordPresent);
		}
		return played;
	}

	bool DropHeldItem(RE::Actor& a_actor)
	{
		auto* inventory = a_actor.inventoryList;
		if (!inventory || !CanTouchWorld()) {
			return false;
		}
		RE::TESBoundObject* held{ nullptr };
		RE::TBO_InstanceData* instance{ nullptr };
		for (auto& item : inventory->data) {
			if (!item.object || !item.object->As<RE::TESObjectWEAP>()) {
				continue;
			}
			for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				if (!stack->IsEquipped()) {
					continue;
				}
				held = item.object;
				if (stack->extra) {
					if (auto* extra = stack->extra->GetByType<RE::ExtraInstanceData>()) {
						instance = extra->data.get();
					}
				}
				break;
			}
			if (held) {
				break;
			}
		}
		if (!held) {
			F4T::DebugLogging::Log(kLogComponent, "bled out with no weapon in hand: nothing to drop");
			return false;
		}

		const RE::BGSObjectInstance object{ held, instance };
		if (auto* equipManager = RE::ActorEquipManager::GetSingleton()) {
			equipManager->UnequipObject(&a_actor, &object, 1, nullptr, 0, false, false, true, true, nullptr);
		}
		const auto position = a_actor.GetPosition();
		const auto dropped = a_actor.DropObject(object, nullptr, 1, &position, nullptr);
		const auto droppedRef = dropped.get();
		if (!droppedRef) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"bleed-out dropped item={:08X} ref=none gearPos=({:.1f},{:.1f},{:.1f}): refused (the engine returned no reference)",
				held->GetFormID(),
				position.x,
				position.y,
				position.z);
			return false;
		}

		const auto droppedPosition = droppedRef->GetPosition();
		F4T::DebugLogging::Log(
			kLogComponent,
			"bleed-out dropped item={:08X} ref={:08X} refPos=({:.1f},{:.1f},{:.1f}) gearPos=({:.1f},{:.1f},{:.1f}) drift={:.2f}: placed",
			held->GetFormID(),
			droppedRef->GetFormID(),
			droppedPosition.x,
			droppedPosition.y,
			droppedPosition.z,
			position.x,
			position.y,
			position.z,
			DistanceBetween(droppedPosition, position));
		return true;
	}

	float DistanceBetween(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
	{
		const auto dx = a_lhs.x - a_rhs.x;
		const auto dy = a_lhs.y - a_rhs.y;
		const auto dz = a_lhs.z - a_rhs.z;
		return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
	}

	float FacingCosine(const RE::NiPoint3& a_from, float a_heading, const RE::NiPoint3& a_to)
	{
		const auto dx = a_to.x - a_from.x;
		const auto dy = a_to.y - a_from.y;
		const auto length = std::sqrt((dx * dx) + (dy * dy));
		if (!std::isfinite(length) || length < 1.0F || !std::isfinite(a_heading)) {
			return -1.0F;
		}
		return ((std::sin(a_heading) * dx) + (std::cos(a_heading) * dy)) / length;
	}
}
