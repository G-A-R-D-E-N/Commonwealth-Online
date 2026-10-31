#pragma once

#include "F4TEquipmentState.h"
#include "F4TRemotePlayerState.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSInventoryList.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstanceExtra.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace F4T::EquipmentInstance
{
	using RemotePlayerState::RemoteItemMod;

	struct FoundStack
	{
		RE::BGSInventoryItem::Stack* stack{ nullptr };
		std::uint32_t stackId{ 0 };
	};

	// TODO-verify: source:RE-Work/subsystems/commonwealth-online-re/co245-inventory-equipment/README.md (G3.2 route; G3.1 layout, ObjectIndexData entry)
	inline std::optional<std::vector<RemoteItemMod>> ReadStackMods(const RE::BGSInventoryItem::Stack& a_stack)
	{
		if (!a_stack.extra) {
			return std::nullopt;
		}
		const auto* instanceExtra = a_stack.extra->GetByType<RE::BGSObjectInstanceExtra>();
		if (!instanceExtra || !instanceExtra->values) {
			return std::nullopt;
		}
		std::vector<RemoteItemMod> mods;
		for (const auto& entry : instanceExtra->GetIndexData()) {
			if (entry.disabled != 0) {
				continue;
			}
			mods.push_back({ entry.objectID, entry.index, entry.rank });
		}
		if (mods.size() > EquipmentState::kMaximumItemMods) {
			return std::nullopt;
		}
		return mods;
	}

	using EquipmentState::SameMods;

	template <class Match>
	inline FoundStack FindStackIndexed(RE::Actor& a_actor, std::uint32_t a_baseFormId, Match&& a_match)
	{
		auto* list = a_actor.inventoryList;
		if (!list) {
			return {};
		}
		const RE::BSAutoReadLock lock{ list->rwLock };
		for (auto& item : list->data) {
			if (!item.object || item.object->GetFormID() != a_baseFormId) {
				continue;
			}
			std::uint32_t stackId = 0;
			for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackId) {
				if (a_match(*stack, stackId)) {
					return { stack, stackId };
				}
			}
		}
		return {};
	}

	template <class Match>
	inline FoundStack FindStack(RE::Actor& a_actor, std::uint32_t a_baseFormId, Match&& a_match)
	{
		return FindStackIndexed(a_actor, a_baseFormId,
			[&](const RE::BGSInventoryItem::Stack& a_stack, std::uint32_t) { return a_match(a_stack); });
	}

	inline FoundStack FindEquippedStack(RE::Actor& a_actor, std::uint32_t a_baseFormId)
	{
		return FindStack(a_actor, a_baseFormId, [](const RE::BGSInventoryItem::Stack& a_stack) { return a_stack.IsEquipped(); });
	}

	inline std::optional<std::vector<RemoteItemMod>> CaptureEquippedItemMods(RE::Actor& a_actor, std::uint32_t a_baseFormId)
	{
		const auto found = FindEquippedStack(a_actor, a_baseFormId);
		return found.stack ? ReadStackMods(*found.stack) : std::nullopt;
	}

	// Constraint: one inventory read lock per publish tick, not one per slot; source:docs/code-notes/equipment-state.md (armor recipe capture).
	inline std::vector<std::optional<std::vector<RemoteItemMod>>> CaptureEquippedItemMods(
		RE::Actor& a_actor,
		const std::vector<std::uint32_t>& a_baseFormIds)
	{
		std::vector<std::optional<std::vector<RemoteItemMod>>> recipes(a_baseFormIds.size());
		std::vector<bool> captured(a_baseFormIds.size(), false);
		auto* list = a_actor.inventoryList;
		if (!list) {
			return recipes;
		}
		const RE::BSAutoReadLock lock{ list->rwLock };
		for (auto& item : list->data) {
			if (!item.object) {
				continue;
			}
			const auto formId = item.object->GetFormID();
			for (std::size_t i = 0; i < a_baseFormIds.size(); ++i) {
				if (captured[i] || a_baseFormIds[i] != formId) {
					continue;
				}
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					if (stack->IsEquipped()) {
						recipes[i] = ReadStackMods(*stack);
						captured[i] = true;
						break;
					}
				}
			}
		}
		return recipes;
	}

	enum class ModdedStackStatus
	{
		kResolved,
		kBaseFormOnly,
		kModFormMissing,
		kStackMissing
	};

	struct ResolvedModdedStack
	{
		FoundStack found;
		RE::TBO_InstanceData* instanceData{ nullptr };
		ModdedStackStatus status{ ModdedStackStatus::kResolved };
	};

	// Constraint: mod forms must resolve before the BGSObjectInstanceExtra allocation; source:docs/code-notes/power-armor-sync.md.
	template <class LogMissing>
	inline ResolvedModdedStack ResolveModdedStack(
		RE::Actor& a_actor,
		RE::TESBoundObject& a_object,
		const std::vector<RemoteItemMod>* a_mods,
		LogMissing&& a_logMissing)
	{
		ResolvedModdedStack result;

		std::vector<RE::BGSMod::Attachment::Mod*> forms;
		if (a_mods && !a_mods->empty()) {
			forms.reserve(a_mods->size());
			for (const auto& mod : *a_mods) {
				auto* form = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(mod.formId);
				if (!form) {
					a_logMissing(mod.formId);
					forms.clear();
					break;
				}
				forms.push_back(form);
			}
		}

		if (!forms.empty()) {
			const auto existing = FindStack(
				a_actor,
				a_object.GetFormID(),
				[&](const RE::BGSInventoryItem::Stack& a_stack) {
					const auto stackMods = ReadStackMods(a_stack);
					return stackMods && SameMods(*stackMods, *a_mods);
				});
			if (existing.stack && existing.stack->extra) {
				result.found = existing;
				result.instanceData = existing.stack->extra->CreateInstanceData(std::addressof(a_object), false);
				return result;
			}

			auto* instanceExtra = new RE::BGSObjectInstanceExtra(nullptr, std::addressof(a_object), nullptr);
			RE::BSTSmartPointer<RE::ExtraDataList> extraList{ new RE::ExtraDataList() };
			extraList->AddExtra(instanceExtra);
			for (std::size_t i = 0; i < forms.size(); ++i) {
				instanceExtra->AddMod(*forms[i], a_mods->at(i).index, a_mods->at(i).rank, false);
			}

			a_actor.AddObjectToContainer(
				std::addressof(a_object),
				extraList,
				1,
				nullptr,
				RE::ITEM_REMOVE_REASON::kNone);

			const auto byPointer = FindStack(
				a_actor,
				a_object.GetFormID(),
				[&](const RE::BGSInventoryItem::Stack& a_stack) { return a_stack.extra.get() == extraList.get(); });
			result.found = byPointer.stack
							   ? byPointer
							   : FindStack(
									 a_actor,
									 a_object.GetFormID(),
									 [&](const RE::BGSInventoryItem::Stack& a_stack) {
										 const auto stackMods = ReadStackMods(a_stack);
										 return stackMods && SameMods(*stackMods, *a_mods);
									 });
			if (!result.found.stack || !result.found.stack->extra) {
				result.status = ModdedStackStatus::kStackMissing;
				return result;
			}
			result.instanceData = result.found.stack->extra->CreateInstanceData(std::addressof(a_object), false);
			return result;
		}

		result.status = a_mods && !a_mods->empty() ? ModdedStackStatus::kModFormMissing : ModdedStackStatus::kBaseFormOnly;
		result.found = FindStack(a_actor, a_object.GetFormID(), [](const RE::BGSInventoryItem::Stack&) { return true; });
		if (!result.found.stack) {
			a_actor.AddObjectToContainer(
				std::addressof(a_object),
				RE::BSTSmartPointer<RE::ExtraDataList>{},
				1,
				nullptr,
				RE::ITEM_REMOVE_REASON::kNone);
		}
		return result;
	}
}
