#include "pch.h"
// address source:commonlibf4-nomad

#include "F4TPowerArmorSync.h"

#include "F4TDebugLogging.h"
#include "F4TEquipmentInstance.h"
#include "F4TPowerArmorScript.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/B/BIPOBJECT.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BipedAnim.h"
#include "RE/C/CHANGE_TYPES.h"
#include "RE/E/ExtraDataList.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PowerArmor.h"
#include "RE/T/TESFurniture.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using F4T::ProxyActorController::detail::LogThrottledWarning;
	using F4T::EquipmentInstance::ModdedStackStatus;
	using F4T::EquipmentInstance::ResolveModdedStack;
	using F4T::EquipmentInstance::ResolvedModdedStack;

	constexpr std::uint32_t kArmorTypePowerKeywordFormId = 0x0004D8A1;

	bool IsPowerArmorPiece(const RE::TESObjectARMO& a_armor)
	{
		const auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kArmorTypePowerKeywordFormId);
		return keyword && a_armor.HasKeyword(keyword);
	}

	// Constraint: ExtraPowerArmor handle offset on OG, NG and AE; source:CodeNotes-GARDEN power-armor-sync.
	RE::ObjectRefHandle GetOccupiedFrameHandle(const RE::Actor& a_actor)
	{
		constexpr std::size_t kExtraPowerArmorHandleOffset = 0x18;
		const auto* extras = a_actor.extraList.get();
		const auto* extra = extras ? extras->GetByType(RE::EXTRA_DATA_TYPE::kPowerArmor) : nullptr;
		return extra ? *reinterpret_cast<const RE::ObjectRefHandle*>(reinterpret_cast<const std::byte*>(extra) + kExtraPowerArmorHandleOffset) : RE::ObjectRefHandle{};
	}

	// Constraint: GetDefaultFrame ids on OG, NG and AE; source:CodeNotes-GARDEN power-armor-sync.
	RE::TESObjectARMO* GetDefaultFrame()
	{
		static REL::Relocation<RE::TESObjectARMO* (*)()> func{ REL::ID{ 933667, 2194757, 2194757 } };
		return func();
	}

	bool IsFurnitureForm(std::uint32_t a_formId)
	{
		auto* form = RE::TESForm::GetFormByID(a_formId);
		return form && form->GetFormType() == RE::ENUM_FORM_ID::kFURN;
	}

	RE::TESObjectARMO* ResolveFrameArmor(std::uint32_t a_frameBaseFormId)
	{
		auto* form = RE::TESForm::GetFormByID(a_frameBaseFormId);
		if (!form) {
			return nullptr;
		}
		if (auto* armor = form->As<RE::TESObjectARMO>()) {
			return armor;
		}
		return form->GetFormType() == RE::ENUM_FORM_ID::kFURN ? GetDefaultFrame() : nullptr;
	}

	RE::BIPOBJECT* FindEquippedBipedObject(RE::Actor& a_actor, std::uint32_t a_formId)
	{
		if (!a_actor.biped) {
			return nullptr;
		}

		for (std::int32_t slot = 0; slot < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++slot) {
			auto* bipedObject = a_actor.biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(slot));
			if (bipedObject && bipedObject->parent.object && bipedObject->parent.object->GetFormID() == a_formId) {
				return bipedObject;
			}
		}

		return nullptr;
	}

	void LogPowerArmorModMissing(std::string_view a_logKey, std::uint32_t a_armorFormId, std::uint32_t a_modFormId)
	{
		LogThrottledWarning(
			std::string{ "proxy_power_armor_mod_missing_" } + std::string{ a_logKey } + "_" + std::to_string(a_modFormId),
			std::format(
				"Could not resolve power armor recipe mod {:08X} for armor {:08X} ({}); equipping the base form.",
				a_modFormId,
				a_armorFormId,
				a_logKey));
	}

	void LogPowerArmorBaseFormFallback(std::string_view a_logKey, std::uint32_t a_armorFormId)
	{
		LogThrottledWarning(
			std::string{ "proxy_power_armor_recipe_stack_missing_" } + std::string{ a_logKey } + "_" + std::to_string(a_armorFormId),
			std::format(
				"Could not resolve the modded power armor stack for armor {:08X} ({}); equipping the base form instead.",
				a_armorFormId,
				a_logKey));
	}

	ResolvedModdedStack ResolveEquipStack(
		RE::Actor& a_actor,
		RE::TESBoundObject& a_object,
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>* a_mods,
		std::string_view a_logKey)
	{
		auto resolved = ResolveModdedStack(
			a_actor,
			a_object,
			a_mods,
			[&](std::uint32_t a_modFormId) { LogPowerArmorModMissing(a_logKey, a_object.GetFormID(), a_modFormId); });

		if (resolved.status == ModdedStackStatus::kStackMissing) {
			LogPowerArmorBaseFormFallback(a_logKey, a_object.GetFormID());
		}

		return resolved;
	}

	bool EquipOnProxy(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_actor,
		RE::TESBoundObject& a_object,
		const ResolvedModdedStack& a_equip)
	{
		RE::BGSObjectInstance objectInstance{ std::addressof(a_object), a_equip.instanceData };
		return a_equipManager.EquipObject(
			std::addressof(a_actor),
			objectInstance,
			a_equip.found.stackId,
			1,
			nullptr,
			false,
			true,
			false,
			true,
			false);
	}

	using ExtraListPointers = std::vector<const RE::ExtraDataList*>;

	ExtraListPointers CollectModdedStacks(RE::Actor& a_actor, std::uint32_t a_formId)
	{
		ExtraListPointers stacks;
		auto* list = a_actor.inventoryList;
		if (!list) {
			return stacks;
		}
		const RE::BSAutoReadLock lock{ list->rwLock };
		for (auto& item : list->data) {
			if (!item.object || item.object->GetFormID() != a_formId) {
				continue;
			}
			for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				if (stack->extra && F4T::EquipmentInstance::ReadStackMods(*stack).has_value()) {
					stacks.push_back(stack->extra.get());
				}
			}
		}
		return stacks;
	}

	void RemoveStaleModdedStacks(
		RE::Actor& a_actor,
		std::uint32_t a_formId,
		const RE::ExtraDataList* a_keep,
		const ExtraListPointers& a_candidates)
	{
		auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_formId);
		if (!object) {
			return;
		}
		if (a_keep && !F4T::EquipmentInstance::FindStack(
						  a_actor,
						  a_formId,
						  [&](const RE::BGSInventoryItem::Stack& a_stack) { return a_stack.extra.get() == a_keep; })
						  .stack) {
			return;
		}
		for (const auto* candidate : a_candidates) {
			const auto found = F4T::EquipmentInstance::FindStack(
				a_actor,
				a_formId,
				[&](const RE::BGSInventoryItem::Stack& a_stack) {
					return a_stack.extra.get() == candidate && candidate != a_keep && !a_stack.IsEquipped();
				});
			if (!found.stack) {
				continue;
			}
			RE::TESObjectREFR::RemoveItemData removeData{ object, 1 };
			removeData.stackData.push_back(found.stackId);
			a_actor.RemoveItem(removeData);
		}
	}
}

namespace
{
	constexpr std::uint32_t kFurnitureTypePowerArmorKeywordFormId = 0x0003430B;
	constexpr std::uint32_t kFrameWaitTickLimit = 600;
	constexpr std::uint32_t kMaxFramePlacements = 8;
	constexpr std::uint32_t kExitRetryTicks = 60;

	F4T::PowerArmorSync::FrameSaveGate g_frameSaveGate;
	std::vector<F4T::PowerArmorSync::PlacedPowerArmorFrame> g_retiredFrames;

	void RequestFrameExit(RE::Actor& a_actor, F4T::PowerArmorSync::PlacedPowerArmorFrame& a_frame)
	{
		if (a_frame.exitRequested && ++a_frame.exitTicks < kExitRetryTicks) {
			return;
		}
		a_frame.exitTicks = 0;
		a_frame.exitRequested = F4T::PowerArmorScript::ExitActorFromPowerArmor(a_actor);
	}

	bool RetireOwnedFrame(F4T::PowerArmorSync::PlacedPowerArmorFrame& a_frame)
	{
		if (!a_frame.reference && a_frame.handle) {
			a_frame.reference = a_frame.handle.get();
			if (!a_frame.reference) {
				return false;
			}
		}
		auto* ref = a_frame.reference.get();
		if (ref && a_frame.handle) {
			const auto resolved = a_frame.handle.get();
			if (!resolved || resolved.get() != ref) {
				return false;
			}
		}
		if (!ref) {
			return true;
		}
		if (a_frame.owner) {
			const auto owner = a_frame.ownerHandle.get();
			if (!owner || owner.get() != a_frame.owner.get()) {
				return false;
			}
		}
		if (a_frame.owner && a_frame.owner->IsDeleted()) {
			return false;
		}
		if (a_frame.owner && RE::PowerArmor::ActorInPowerArmor(*a_frame.owner)) {
			if (ref->IsDeleted() || !a_frame.owner->GetParentCell()) {
				return false;
			}
			const auto occupied = GetOccupiedFrameHandle(*a_frame.owner).get();
			if (!occupied || occupied.get() != ref) {
				return false;
			}
			a_frame.confirmedEntered = true;
			if (RE::ActorEquipManager::GetSingleton()) {
				if (ref->GetFullyLoaded3D()) {
					return false;
				}
				RequestFrameExit(*a_frame.owner, a_frame);
			}
			if (RE::PowerArmor::ActorInPowerArmor(*a_frame.owner)) {
				return false;
			}
		}
		if (a_frame.owner && GetOccupiedFrameHandle(*a_frame.owner)) {
			return false;
		}
		if (a_frame.entered && !a_frame.confirmedEntered) {
			return false;
		}
		if (ref->IsDeleted()) {
			return true;
		}
		const auto* base = ref->GetObjectReference();
		if (!ref->IsCreated() || !base || base->GetFormID() != a_frame.baseFormId) {
			return false;
		}
		F4T::ProxySaveSafety::DestroyProxyActor(*ref);
		return ref->IsDeleted();
	}

	struct FrameCreationData : RE::NEW_REFR_DATA
	{
		RE::NiPointer<RE::TESObjectREFR> created;

		void HandlePre3D(RE::TESObjectREFR* a_ref) override
		{
			created.reset(a_ref);
		}
	};

}

namespace F4T::PowerArmorSync
{
	std::optional<PowerArmorState> CaptureLocalPowerArmor(
		RE::PlayerCharacter& a_player,
		const std::vector<Piece>& a_equippedItems)
	{
		PowerArmorState state;

		state.inFrame = RE::PowerArmor::ActorInPowerArmor(a_player);
		if (state.inFrame) {
			if (const auto frame = GetOccupiedFrameHandle(a_player).get()) {
				if (const auto* base = frame->GetObjectReference()) {
					state.frameBaseFormId = base->GetFormID();
				}
			}
			if (state.frameBaseFormId == 0) {
				LogThrottledWarning(
					"power_armor_frame_reference_missing",
					"In power armor without a resolved frame reference; publishing inFrame with an empty frame base form.");
			}
		}

		const auto* frameArmor = state.frameBaseFormId != 0 ? ResolveFrameArmor(state.frameBaseFormId) : nullptr;
		if (state.frameBaseFormId != 0 && !frameArmor && IsFurnitureForm(state.frameBaseFormId)) {
			LogThrottledWarning(
				"power_armor_default_frame_unresolved",
				std::format("Power armor frame {:08X} has no default frame form; this update publishes no power armor.", state.frameBaseFormId));
			return std::nullopt;
		}
		auto candidates = a_equippedItems;
		if (a_player.biped) {
			for (std::int32_t slot = 0; slot < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++slot) {
				const auto* bipedObject = a_player.biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(slot));
				const auto* armor = bipedObject && bipedObject->parent.object ? bipedObject->parent.object->As<RE::TESObjectARMO>() : nullptr;
				if (!armor || !IsPowerArmorPiece(*armor) ||
					std::ranges::any_of(a_equippedItems, [&](const Piece& a_item) { return a_item.formId == armor->GetFormID(); })) {
					continue;
				}
				candidates.push_back(Piece{ std::format("biped{}", slot), armor->GetFormID(), F4T::EquipmentInstance::CaptureEquippedItemMods(a_player, armor->GetFormID()) });
			}
		}
		for (const auto& item : candidates) {
			if (item.formId == 0) {
				continue;
			}
			// Constraint: the frame form is the frame, never a piece, on the wire; source:CodeNotes-GARDEN power-armor-sync.
			if (state.frameBaseFormId != 0 && (item.formId == state.frameBaseFormId || (frameArmor && item.formId == frameArmor->GetFormID()))) {
				continue;
			}
			const auto* armor = RE::TESForm::GetFormByID<RE::TESObjectARMO>(item.formId);
			if (!armor || !IsPowerArmorPiece(*armor)) {
				continue;
			}
			const auto known = std::ranges::any_of(state.pieces, [&](const Piece& a_piece) { return a_piece.formId == item.formId; });
			if (known) {
				continue;
			}
			state.pieces.push_back(item);
		}

		if (!IsValid(state)) {
			LogThrottledWarning(
				"power_armor_capture_invalid",
				std::format(
					"Captured power armor state is invalid ({} pieces); publishing no power armor for this update.",
					state.pieces.size()));
			return std::nullopt;
		}

		return PublishableCapture(std::move(state));
	}

	OccupiedFrame CaptureFrameFromReference(RE::TESObjectREFR& a_frame)
	{
		OccupiedFrame captured;
		captured.refFormId = a_frame.GetFormID();
		const auto position = a_frame.GetPosition();
		captured.x = position.x;
		captured.y = position.y;
		captured.z = position.z;
		captured.angleZ = a_frame.data.angle.z;
		if (const auto* cell = a_frame.GetParentCell()) {
			captured.cellId = cell->GetFormID();
			if (cell->IsExterior() && cell->worldSpace) {
				captured.worldspaceId = cell->worldSpace->GetFormID();
			}
		}
		return captured;
	}

	std::optional<OccupiedFrame> CaptureOccupiedFrame(RE::PlayerCharacter& a_player)
	{
		const auto frame = GetOccupiedFrameHandle(a_player).get();
		if (!frame) {
			return std::nullopt;
		}
		return CaptureFrameFromReference(*frame.get());
	}

	bool ProxyReportsInPowerArmor(const RE::Actor& a_proxy)
	{
		return RE::PowerArmor::ActorInPowerArmor(a_proxy);
	}

	bool IsPowerArmorPieceForm(std::uint32_t a_formId)
	{
		if (a_formId == 0) {
			return false;
		}
		const auto* armor = RE::TESForm::GetFormByID<RE::TESObjectARMO>(a_formId);
		return armor && IsPowerArmorPiece(*armor);
	}

	PowerArmorApplyResult ApplyProxyPowerArmorPlan(
		RE::Actor& a_proxy,
		const PowerArmorState& a_previous,
		const PowerArmorState& a_current,
		std::span<const PowerArmorOp> a_ops,
		std::string_view a_logKey)
	{
		PowerArmorApplyResult result;
		result.applied = a_previous;
		if (a_ops.empty()) {
			result.applied = a_current;
			return result;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			LogThrottledWarning(
				std::string{ "proxy_power_armor_manager_missing_" } + std::string{ a_logKey },
				std::format("Could not apply power armor for {}; ActorEquipManager unavailable.", a_logKey));
			return result;
		}

		std::vector<std::uint8_t> succeeded(a_ops.size(), 0);
		for (std::size_t i = 0; i < a_ops.size(); ++i) {
			const auto& op = a_ops[i];
			if (op.kind == PowerArmorOpKind::kUnequip) {
				auto* bipedObject = FindEquippedBipedObject(a_proxy, op.piece.formId);
				if (!bipedObject) {
					succeeded[i] = 1;
					RemoveStaleModdedStacks(a_proxy, op.piece.formId, nullptr, CollectModdedStacks(a_proxy, op.piece.formId));
					continue;
				}
				succeeded[i] = equipManager->UnequipObject(
							  std::addressof(a_proxy),
							  std::addressof(bipedObject->parent),
							  1,
							  nullptr,
							  0,
							  false,
							  true,
							  false,
							  true,
							  nullptr);
				if (succeeded[i]) {
					result.changed = true;
				}
				RemoveStaleModdedStacks(a_proxy, op.piece.formId, nullptr, CollectModdedStacks(a_proxy, op.piece.formId));
				continue;
			}

			auto* armor = RE::TESForm::GetFormByID<RE::TESObjectARMO>(op.piece.formId);
			if (!armor) {
				LogThrottledWarning(
					std::string{ "proxy_power_armor_form_missing_" } + std::string{ a_logKey } + "_" + std::to_string(op.piece.formId),
					std::format(
						"Could not apply power armor piece {:08X} for {}; no local ARMO form resolved.",
						op.piece.formId,
						a_logKey));
				continue;
			}

			const auto preExistingStacks = CollectModdedStacks(a_proxy, op.piece.formId);
			const auto equip = ResolveEquipStack(a_proxy, *armor, op.piece.mods ? std::addressof(*op.piece.mods) : nullptr, a_logKey);
			const auto* keepExtra = equip.found.stack ? equip.found.stack->extra.get() : nullptr;
			if (!EquipOnProxy(*equipManager, a_proxy, *armor, equip)) {
				LogThrottledWarning(
					std::string{ "proxy_power_armor_equip_failed_" } + std::string{ a_logKey } + "_" + std::to_string(op.piece.formId),
					std::format(
						"ActorEquipManager failed to equip power armor piece {:08X} on proxy {:08X} for {}.",
						op.piece.formId,
						a_proxy.GetFormID(),
						a_logKey));
				continue;
			}
			succeeded[i] = 1;
			result.changed = true;
			RemoveStaleModdedStacks(a_proxy, op.piece.formId, keepExtra, preExistingStacks);
		}

		result.applied = AppliedStateAfterOperations(a_previous, a_current, a_ops, succeeded);
		return result;
	}

	FrameSaveGate& GetFrameSaveGate()
	{
		return g_frameSaveGate;
	}

	void PreservePlacedFrame(PlacedPowerArmorFrame& a_frame)
	{
		if (!a_frame.reserved) {
			return;
		}
		g_retiredFrames.push_back(std::move(a_frame));
		a_frame = {};
	}

	bool IsRetiredFrameOwner(const RE::Actor& a_actor)
	{
		return std::ranges::any_of(g_retiredFrames, [&a_actor](const PlacedPowerArmorFrame& a_frame) {
			return a_frame.owner.get() == std::addressof(a_actor);
		});
	}

	void RetryRetiredFrames()
	{
		std::erase_if(g_retiredFrames, [](PlacedPowerArmorFrame& a_frame) {
			if (!RetireOwnedFrame(a_frame)) {
				return false;
			}
			if (a_frame.reserved) {
				g_frameSaveGate.ReleaseFrame();
			}
			return true;
		});
	}

	void DestroyPlacedFrame(PlacedPowerArmorFrame& a_frame, RE::Actor*)
	{
		const auto placements = a_frame.placements;
		if (!RetireOwnedFrame(a_frame)) {
			g_retiredFrames.push_back(std::move(a_frame));
		} else if (a_frame.reserved) {
			g_frameSaveGate.ReleaseFrame();
		}
		a_frame = {};
		a_frame.placements = placements;
	}

	bool ApplyProxyPowerArmorFrame(RE::Actor& a_proxy, std::uint32_t a_frameBaseFormId, PlacedPowerArmorFrame& a_frame, std::string_view a_logKey)
	{
		if (a_frameBaseFormId == 0) {
			return false;
		}
		a_frame.baseFormId = a_frameBaseFormId;
		if (RE::PowerArmor::ActorInPowerArmor(a_proxy)) {
			if (a_frame.reference && GetOccupiedFrameHandle(a_proxy).get().get() == a_frame.reference.get()) {
				a_frame.placements = 0;
				a_frame.confirmedEntered = true;
				return true;
			}
			return false;
		}
		if (a_frame.entered) {
			if (++a_frame.enterPollTicks < kFrameWaitTickLimit) {
				return false;
			}
			LogThrottledWarning(
				"proxy_power_armor_enter_no_effect_" + std::string{ a_logKey },
				std::format(
					"Power armor enter did not put proxy {:08X} in frame {:08X} for {} within {} ticks; the placed frame is deleted.",
					a_proxy.GetFormID(),
					a_frame.refFormId,
					a_logKey,
					kFrameWaitTickLimit));
			DestroyPlacedFrame(a_frame, &a_proxy);
			return false;
		}

		auto* base = RE::TESForm::GetFormByID(a_frameBaseFormId);
		auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kFurnitureTypePowerArmorKeywordFormId);
		auto* furniture = base && base->GetFormType() == RE::ENUM_FORM_ID::kFURN ? base->As<RE::TESFurniture>() : nullptr;
		if (!furniture || !keyword || !furniture->HasKeyword(keyword)) {
			LogThrottledWarning(
				"proxy_power_armor_frame_not_furniture_" + std::string{ a_logKey },
				std::format(
					"Power armor frame {:08X} for {} is not a FURN carrying FurnitureTypePowerArmor; the pieces are applied without the frame.",
					a_frameBaseFormId,
					a_logKey));
			return false;
		}

		if (!a_frame.reference && a_frame.handle) {
			a_frame.reference = a_frame.handle.get();
			if (!a_frame.reference) {
				return false;
			}
			a_frame.refFormId = a_frame.reference->GetFormID();
		}
		if (!a_frame.reference) {
			auto* cell = a_proxy.GetParentCell();
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!cell || !handler || (cell->IsExterior() && !cell->worldSpace)) {
				return false;
			}
			if (a_frame.placements >= kMaxFramePlacements || g_retiredFrames.size() >= 99) {
				LogThrottledWarning(
					"proxy_power_armor_frame_placements_exhausted_" + std::string{ a_logKey },
					std::format("Power armor frame {:08X} for {} reached the placement limit.", a_frameBaseFormId, a_logKey));
				return false;
			}
			const auto ownerHandle = a_proxy.GetHandle();
			const auto owner = ownerHandle.get();
			if (!owner || owner.get() != &a_proxy) {
				return false;
			}
			if (!g_frameSaveGate.ReserveFrame()) {
				return false;
			}
			a_frame.reserved = true;
			FrameCreationData data;
			data.object = furniture;
			data.location = a_proxy.GetPosition();
			data.direction = a_proxy.data.angle;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			data.clearStillLoadingFlag = true;
			++a_frame.placements;
			a_frame.owner.reset(&a_proxy);
			a_frame.ownerHandle = ownerHandle;
			a_frame.handle = handler->CreateReferenceAtLocation(data);
			a_frame.reference = std::move(data.created);
			if (!a_frame.reference && a_frame.handle) {
				a_frame.reference = a_frame.handle.get();
				if (!a_frame.reference) {
					return false;
				}
			}
			if (!a_frame.reference) {
				g_frameSaveGate.ReleaseFrame();
				a_frame.reserved = false;
				LogThrottledWarning(
					"proxy_power_armor_frame_create_failed_" + std::string{ a_logKey },
					std::format("Could not create power armor frame {:08X} for {}.", a_frameBaseFormId, a_logKey));
				return false;
			}
			a_frame.refFormId = a_frame.reference->GetFormID();
		}

		auto* frameRef = a_frame.reference.get();
		if (!frameRef->AddChange(RE::CHANGE_TYPES::kFormCreated)) {
			if (++a_frame.registrationTicks >= kFrameWaitTickLimit) {
				DestroyPlacedFrame(a_frame, &a_proxy);
			}
			return false;
		}

		if (frameRef->Get3D() == nullptr) {
			if (++a_frame.waitTicks < kFrameWaitTickLimit) {
				return false;
			}
			LogThrottledWarning(
				"proxy_power_armor_frame_not_loaded_" + std::string{ a_logKey },
				std::format("Power armor frame {:08X} for {} never loaded 3D on proxy {:08X}; deleting the frame.", a_frame.refFormId, a_logKey, a_proxy.GetFormID()));
			DestroyPlacedFrame(a_frame, &a_proxy);
			return false;
		}

		if (!F4T::PowerArmorScript::SwitchActorToPowerArmor(a_proxy, *frameRef)) {
			return false;
		}
		a_frame.entered = true;
		a_frame.enterPollTicks = 0;
		a_frame.waitTicks = 0;
		const auto inFrame = RE::PowerArmor::ActorInPowerArmor(a_proxy) &&
			GetOccupiedFrameHandle(a_proxy).get().get() == a_frame.reference.get();
		if (inFrame) {
			a_frame.placements = 0;
			a_frame.confirmedEntered = true;
		}
		return inFrame;
	}

	bool UnequipProxyPowerArmorFrame(RE::Actor& a_proxy, PlacedPowerArmorFrame& a_frame, std::string_view a_logKey)
	{
		if (a_frame.baseFormId == 0) {
			return false;
		}
		if (a_frame.owner) {
			const auto owner = a_frame.ownerHandle.get();
			if (!owner || owner.get() != a_frame.owner.get() || owner.get() != &a_proxy) {
				return false;
			}
		}
		if (RE::PowerArmor::ActorInPowerArmor(a_proxy)) {
			if (!a_proxy.GetParentCell()) {
				return false;
			}
			if (!a_frame.reference || a_frame.reference->IsDeleted() || GetOccupiedFrameHandle(a_proxy).get().get() != a_frame.reference.get()) {
				return false;
			}
			if (a_frame.reference->GetFullyLoaded3D()) {
				return false;
			}
			RequestFrameExit(a_proxy, a_frame);
			if (RE::PowerArmor::ActorInPowerArmor(a_proxy)) {
				LogThrottledWarning(
					"proxy_power_armor_exit_pending_" + std::string{ a_logKey },
					std::format("Power armor exit is pending for proxy {:08X}, frame {:08X}, {}.",
						a_proxy.GetFormID(), a_frame.baseFormId, a_logKey));
				return false;
			}
		}
		if (!RetireOwnedFrame(a_frame)) {
			return false;
		}
		const auto placements = a_frame.placements;
		if (a_frame.reserved) {
			g_frameSaveGate.ReleaseFrame();
		}
		a_frame = {};
		a_frame.placements = placements;
		return true;
	}
}
