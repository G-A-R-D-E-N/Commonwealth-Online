#include "pch.h"

#include "F4TPowerArmorSync.h"

#include "F4TConsoleGuard.h"
#include "F4TDebugLogging.h"
#include "F4TEquipmentInstance.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/B/BIPOBJECT.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BipedAnim.h"
#include "RE/C/Console.h"
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
#include <unordered_set>
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

	// Constraint: ExtraPowerArmor handle offset on OG, NG and AE; source:docs/code-notes/power-armor-sync.md.
	RE::ObjectRefHandle GetOccupiedFrameHandle(const RE::Actor& a_actor)
	{
		constexpr std::size_t kExtraPowerArmorHandleOffset = 0x18;
		const auto* extras = a_actor.extraList.get();
		const auto* extra = extras ? extras->GetByType(RE::EXTRA_DATA_TYPE::kPowerArmor) : nullptr;
		return extra ? *reinterpret_cast<const RE::ObjectRefHandle*>(reinterpret_cast<const std::byte*>(extra) + kExtraPowerArmorHandleOffset) : RE::ObjectRefHandle{};
	}

	// Constraint: GetDefaultFrame ids on OG, NG and AE; source:docs/code-notes/power-armor-sync.md.
	RE::TESObjectARMO* GetDefaultFrame()
	{
#if defined(F4T_TARGET_OG)
		static REL::Relocation<RE::TESObjectARMO* (*)()> func{ REL::ID{ 933667 } };
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
		static REL::Relocation<RE::TESObjectARMO* (*)()> func{ REL::ID{ 2194757 } };
#else
#error "A Commonwealth Online runtime target is required"
#endif
		return func();
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

	const F4T::PowerArmorSync::Piece* FindPieceBySlot(
		const std::vector<F4T::PowerArmorSync::Piece>& a_pieces,
		std::string_view a_slot,
		std::uint32_t a_formId)
	{
		for (const auto& piece : a_pieces) {
			if (piece.slot == a_slot && piece.formId == a_formId) {
				return std::addressof(piece);
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
#if defined(F4T_TARGET_OG)
	inline constexpr REL::ID kEnterPowerArmor{ 1187476 };
	inline constexpr REL::ID kExitPowerArmor{ 550299 };
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
	inline constexpr REL::ID kEnterPowerArmor{ 2219442 };
	inline constexpr REL::ID kExitPowerArmor{ 2219450 };
#else
#error "A Commonwealth Online runtime target is required"
#endif
	constexpr std::uint32_t kFurnitureTypePowerArmorKeywordFormId = 0x0003430B;
	constexpr std::uint32_t kFrameWaitTickLimit = 600;
	constexpr std::uint32_t kMaxFramePlacements = 8;
	using EnterPowerArmorFunc = void (*)(RE::Actor*, RE::TESObjectREFR*, bool);
	using ExitPowerArmorFunc = void (*)(RE::Actor&);

	std::unordered_set<std::uint32_t> g_claimedFrameRefIds;

	std::vector<std::uint32_t> FrameRefIds(RE::TESObjectCELL& a_cell, std::uint32_t a_frameBaseFormId)
	{
		std::vector<std::uint32_t> ids;
		a_cell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			const auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
			if (base && base->GetFormID() == a_frameBaseFormId) {
				ids.push_back(a_ref->GetFormID());
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return ids;
	}

	RE::TESObjectREFR* FindNewFrameRef(RE::TESObjectCELL& a_cell, RE::Actor& a_proxy, std::uint32_t a_frameBaseFormId, const std::vector<std::uint32_t>& a_preexisting)
	{
		RE::TESObjectREFR* placed = nullptr;
		float nearest = 0.0f;
		a_cell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			const auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
			if (!base || base->GetFormID() != a_frameBaseFormId || g_claimedFrameRefIds.contains(a_ref->GetFormID()) || std::ranges::find(a_preexisting, a_ref->GetFormID()) != a_preexisting.end()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const auto distance = (a_ref->GetPosition() - a_proxy.GetPosition()).Length();
			if (!placed || distance < nearest) {
				placed = a_ref;
				nearest = distance;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return placed;
	}
}

namespace F4T::PowerArmorSync
{
	std::optional<PowerArmorState> CaptureLocalPowerArmor(
		RE::PlayerCharacter& a_player,
		const std::vector<Piece>& a_equippedItems,
		const PowerArmorState* a_previous)
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
		for (const auto& item : a_equippedItems) {
			if (item.formId == 0) {
				continue;
			}
			// Constraint: the frame form is the frame, never a piece, on the wire; source:docs/code-notes/power-armor-sync.md.
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
			auto piece = item;
			const auto* previous = a_previous ? FindPieceBySlot(a_previous->pieces, item.slot, item.formId) : nullptr;
			piece.mods = previous ? previous->mods : EquipmentInstance::CaptureEquippedItemMods(a_player, item.formId);
			state.pieces.push_back(std::move(piece));
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

	void DestroyPlacedFrame(PlacedPowerArmorFrame& a_frame, RE::Actor* a_proxy)
	{
		auto* frameRef = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_frame.refFormId);
		if (!frameRef && a_frame.waitTicks > 0 && a_proxy) {
			if (auto* cell = a_proxy->GetParentCell()) {
				frameRef = FindNewFrameRef(*cell, *a_proxy, a_frame.baseFormId, a_frame.preexistingRefFormIds);
			}
		}
		const auto* frameBase = frameRef ? frameRef->GetObjectReference() : nullptr;
		if (frameBase && frameBase->GetFormID() == a_frame.baseFormId) {
			F4T::ProxySaveSafety::DestroyProxyActor(*frameRef);
		}
		g_claimedFrameRefIds.erase(a_frame.refFormId);
		const auto placements = a_frame.placements;
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
			a_frame.placements = 0;
			return true;
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

		if (a_frame.refFormId == 0) {
			auto* cell = a_proxy.GetParentCell();
			if (!cell) {
				LogThrottledWarning(
					"proxy_power_armor_frame_cell_missing_" + std::string{ a_logKey },
					std::format("Could not place power armor frame {:08X} for {}; proxy {:08X} has no parent cell.", a_frameBaseFormId, a_logKey, a_proxy.GetFormID()));
				return false;
			}
			if (a_frame.waitTicks == 0) {
				if (a_frame.placements >= kMaxFramePlacements) {
					LogThrottledWarning(
						"proxy_power_armor_frame_placements_exhausted_" + std::string{ a_logKey },
						std::format(
							"Power armor frame {:08X} for {} was placed {} times this session; no further placement on proxy {:08X}.",
							a_frameBaseFormId,
							a_logKey,
							kMaxFramePlacements,
							a_proxy.GetFormID()));
					return false;
				}
				a_frame.preexistingRefFormIds = FrameRefIds(*cell, a_frameBaseFormId);
				++a_frame.placements;
				a_frame.waitTicks = 1;
				F4T::ConsoleGuard::ExecutePluginCommand(std::format("{:08X}.placeatme {:08X} 1", a_proxy.GetFormID(), a_frameBaseFormId).c_str());
				return false;
			}
			auto* placed = FindNewFrameRef(*cell, a_proxy, a_frameBaseFormId, a_frame.preexistingRefFormIds);
			if (!placed) {
				if (++a_frame.waitTicks <= kFrameWaitTickLimit) {
					return false;
				}
				LogThrottledWarning(
					"proxy_power_armor_frame_not_found_" + std::string{ a_logKey },
					std::format("Placed power armor frame {:08X} for {} was not found near proxy {:08X}; placement is retried.", a_frameBaseFormId, a_logKey, a_proxy.GetFormID()));
				DestroyPlacedFrame(a_frame, &a_proxy);
				return false;
			}
			a_frame.refFormId = placed->GetFormID();
			g_claimedFrameRefIds.insert(a_frame.refFormId);
			a_frame.preexistingRefFormIds.clear();
			a_frame.waitTicks = 0;
		}

		auto* frameRef = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_frame.refFormId);
		if (!frameRef) {
			DestroyPlacedFrame(a_frame, &a_proxy);
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

		REL::Relocation<EnterPowerArmorFunc> enterPowerArmor{ kEnterPowerArmor };
		// TODO-verify: enter call not yet run in game.
		enterPowerArmor(&a_proxy, frameRef, false);
		a_frame.entered = true;
		a_frame.enterPollTicks = 0;
		a_frame.waitTicks = 0;
		const auto inFrame = RE::PowerArmor::ActorInPowerArmor(a_proxy);
		if (inFrame) {
			a_frame.placements = 0;
		}
		return inFrame;
	}

	bool UnequipProxyPowerArmorFrame(RE::Actor& a_proxy, PlacedPowerArmorFrame& a_frame, std::string_view a_logKey)
	{
		if (a_frame.baseFormId == 0) {
			return false;
		}
		if (RE::PowerArmor::ActorInPowerArmor(a_proxy)) {
			REL::Relocation<ExitPowerArmorFunc> exitPowerArmor{ kExitPowerArmor };
			// TODO-verify: exit call not yet run in game.
			exitPowerArmor(a_proxy);
			if (RE::PowerArmor::ActorInPowerArmor(a_proxy)) {
				LogThrottledWarning(
					"proxy_power_armor_exit_no_effect_" + std::string{ a_logKey },
					std::format(
						"Power armor exit did not take proxy {:08X} out of frame {:08X} for {}; the frame is released anyway.",
						a_proxy.GetFormID(),
						a_frame.baseFormId,
						a_logKey));
			}
		}
		DestroyPlacedFrame(a_frame, &a_proxy);
		return true;
	}
}
