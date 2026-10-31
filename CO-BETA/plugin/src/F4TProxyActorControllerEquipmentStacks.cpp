#include "pch.h"

#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyEquipmentApply.h"

#include <algorithm>

using namespace F4T::ProxyActorController::detail;

namespace
{
	using F4T::RemotePlayerState::RemoteItemMod;
	using F4T::ProxyEquipmentStacks::StackRemoval;
	using F4T::ProxyEquipmentStacks::TrackedStack;

	std::string DescribeStackMods(const RE::BGSInventoryItem::Stack& a_stack)
	{
		const auto mods = F4T::EquipmentInstance::ReadStackMods(a_stack);
		return mods ? F4T::EquipmentState::DescribeMods(*mods) : std::string{ "unset" };
	}
}

namespace F4T::ProxyActorController::detail
{
	StackCensus CensusStacks(RE::Actor& a_proxy, std::uint32_t a_formId)
	{
		StackCensus census;
		if (!F4T::DebugLogging::IsEnabled()) {
			return census;
		}
		F4T::EquipmentInstance::FindStackIndexed(a_proxy, a_formId,
			[&](const RE::BGSInventoryItem::Stack& a_stack, std::uint32_t a_stackId) {
				++census.total;
				if (!a_stack.extra) {
					++census.nullExtra;
				}
				census.stackIds += std::format("{}{}", census.stackIds.empty() ? "" : ",", a_stackId);
				return false;
			});
		return census;
	}

	std::size_t CountStacks(RE::Actor& a_proxy, std::uint32_t a_formId)
	{
		std::size_t total = 0;
		F4T::EquipmentInstance::FindStackIndexed(a_proxy, a_formId,
			[&](const RE::BGSInventoryItem::Stack&, std::uint32_t) {
				++total;
				return false;
			});
		return total;
	}

	F4T::EquipmentInstance::FoundStack FindTrackedStack(RE::Actor& a_proxy, const TrackedStack& a_tracked)
	{
		return F4T::EquipmentInstance::FindStackIndexed(a_proxy, a_tracked.formId,
			[&](const RE::BGSInventoryItem::Stack& a_stack, std::uint32_t a_stackId) {
				return F4T::ProxyEquipmentStacks::MatchesTrackedStack(
					a_tracked,
					a_tracked.formId,
					a_stackId,
					a_stack.extra.get());
			});
	}

	std::optional<std::uint32_t> LiveStackId(RE::Actor& a_proxy, const TrackedStack& a_tracked)
	{
		if (!a_tracked.extra) {
			std::optional<std::uint32_t> plainId;
			std::size_t plainCount = 0;
			F4T::EquipmentInstance::FindStackIndexed(a_proxy, a_tracked.formId,
				[&](const RE::BGSInventoryItem::Stack& a_stack, std::uint32_t a_stackId) {
					if (!a_stack.extra) {
						plainId = a_stackId;
						++plainCount;
					}
					return false;
				});
			// TODO-verify(#407) source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md (rows 3, 18: at most one null-extra stack per form)
			if (plainCount != 1) {
				return std::nullopt;
			}
			return plainId;
		}
		const auto found = FindTrackedStack(a_proxy, a_tracked);
		if (!found.stack) {
			return std::nullopt;
		}
		return found.stackId;
	}

	void RefreshStackIds(RE::Actor& a_proxy, std::vector<TrackedStack>& a_stacks)
	{
		F4T::ProxyEquipmentStacks::RefreshTrackedStackIds(a_stacks, [&](const TrackedStack& a_entry) { return LiveStackId(a_proxy, a_entry); });
	}

	// Constraint: a null-extra entry whose stack is already gone is dropped before its pass, so no removal in the pass can re-bind it; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	void DropVanishedNullExtra(RE::Actor& a_proxy, std::vector<TrackedStack>& a_stacks)
	{
		std::erase_if(a_stacks, [&](const TrackedStack& a_entry) { return !a_entry.extra && !FindTrackedStack(a_proxy, a_entry).stack; });
	}

	// Constraint: a tracked stack is only removed while unequipped; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	F4T::ProxyEquipmentStacks::StackRemovalResult RemoveTrackedStack(
		F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		const TrackedStack& a_tracked,
		std::string_view a_logKey)
	{
		const auto found = FindTrackedStack(a_proxy, a_tracked);
		if (!found.stack) {
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					"proxy_361_ledger_missing_" + std::string{ a_logKey } + "_" + std::to_string(a_tracked.formId),
					std::format("[RT-VERIFY 361-G] ledger remove formId={:08X} stackId={}: tracked stack not found, dropping entry", a_tracked.formId, a_tracked.stackId),
					1s);
			}
			return { StackRemoval::kMissing, found.stackId };
		}
		auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_tracked.formId);
		if (!object || found.stack->IsEquipped()) {
			// TODO-verify(#407)
			LogThrottledWarning(
				std::string{ "proxy_tracked_stack_blocked_" } + std::string{ a_logKey } + "_" + std::to_string(a_tracked.formId),
				std::format("Tracked stack {:08X} stackId={} is equipped or unresolved; kept for retry.", a_tracked.formId, found.stackId));
			return { StackRemoval::kBlocked, found.stackId };
		}
		// TODO-verify(#407)
		const auto removedExtra = found.stack->extra.get();
		const auto removedMods = F4T::DebugLogging::IsEnabled() ? DescribeStackMods(*found.stack) : std::string{};
		const auto before = CensusStacks(a_proxy, a_tracked.formId);
		const auto stacksBefore = CountStacks(a_proxy, a_tracked.formId);
		RE::TESObjectREFR::RemoveItemData removeData{ object, 1 };
		removeData.stackData.push_back(found.stackId);
		a_proxy.RemoveItem(removeData);
		// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
		const auto unlinked = CountStacks(a_proxy, a_tracked.formId) < stacksBefore;
		if (unlinked) {
			a_ledger.ShiftStackIdsAfterRemoval(a_tracked.formId, found.stackId);
		}
		a_ledger.RefreshStackIds([&](const TrackedStack& a_entry) { return LiveStackId(a_proxy, a_entry); });
		const auto after = CensusStacks(a_proxy, a_tracked.formId);
		if (F4T::DebugLogging::IsEnabled()) {
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps1/README.md
			LogThrottledInfo(
				"proxy_361_ledger_removed_" + std::string{ a_logKey } + "_" + std::to_string(a_tracked.formId),
				std::format(
					"[RT-VERIFY {}] ledger removed formId={:08X} stackId={} extra={:p} mods={} census total {}->{} nullExtra {}->{} remainingStackIds=[{}]",
					a_tracked.extra ? "361-F" : "361-K", a_tracked.formId, found.stackId, static_cast<const void*>(removedExtra), removedMods,
					before.total, after.total, before.nullExtra, after.nullExtra, after.stackIds),
				1s);
		}
		return { StackRemoval::kRemoved, found.stackId, unlinked };
	}

	// Constraint: a tracked stack that could not be removed stays tracked until it can be; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	void RemoveTrackedStacks(F4T::ProxyEquipmentStacks::Ledger& a_ledger, RE::Actor& a_proxy, std::string_view a_logKey)
	{
		auto pending = a_ledger.ForgetAll();
		DropVanishedNullExtra(a_proxy, pending);
		if (!pending.empty() && F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				"proxy_ledger_forget_" + std::string{ a_logKey },
				std::format("[RT-VERIFY 361-F] ledger forget: {} tracked stacks taken for removal", pending.size()),
				1s);
		}
		for (const auto& tracked : pending) {
			const auto removal = RemoveTrackedStack(a_ledger, a_proxy, tracked, a_logKey);
			// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
			if (removal.outcome == StackRemoval::kRemoved) {
				if (removal.unlinked) {
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(pending, tracked.formId, removal.stackId);
				}
				RefreshStackIds(a_proxy, pending);
				continue;
			}
			if (F4T::ProxyEquipmentStacks::RetryTrackedStack(removal.outcome)) {
				if (F4T::DebugLogging::IsEnabled()) {
					LogThrottledInfo(
						"proxy_ledger_retrack_" + std::string{ a_logKey } + "_" + std::to_string(tracked.formId),
						std::format("[RT-VERIFY 361-F] ledger retrack formId={:08X} stackId={}: removal blocked, kept tracked for retry", tracked.formId, removal.stackId),
						1s);
				}
				a_ledger.Track(tracked);
			}
		}
	}

	const F4T::ProxyEquipmentStacks::TrackedStack* FindReusableTrackedStack(
		const F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		std::uint32_t a_formId,
		const std::vector<RemoteItemMod>* a_mods)
	{
		const auto matches = [&](const TrackedStack& a_entry) {
			const auto live = FindTrackedStack(a_proxy, a_entry).stack;
			return live && F4T::EquipmentInstance::SameMods(
				F4T::EquipmentInstance::ReadStackMods(*live).value_or(std::vector<RemoteItemMod>{}),
				a_mods ? *a_mods : std::vector<RemoteItemMod>{});
		};
		const auto* reusable = a_ledger.Find(a_formId, matches);
		return reusable ? reusable : a_ledger.Find(a_formId);
	}

	const ArmorTarget* FindArmorTarget(const std::vector<ArmorTarget>& a_targets, std::uint32_t a_formId)
	{
		const auto found = std::ranges::find(a_targets, a_formId, &ArmorTarget::formId);
		return found == a_targets.end() ? nullptr : std::addressof(*found);
	}

	std::string InstanceDataFailureKey(std::uint32_t a_formId, const std::vector<RemoteItemMod>& a_mods)
	{
		return std::format("{:08X}{}", a_formId, F4T::EquipmentState::DescribeMods(a_mods));
	}

	const std::vector<RemoteItemMod>* ResolveTargetMods(const RemoteEquippedItem& a_item)
	{
		if (a_item.mods && !a_item.mods->empty()) {
			return std::addressof(*a_item.mods);
		}
		return nullptr;
	}

	std::optional<AddedArmorStack> AddTrackedArmorCopy(
		F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		RE::TESObjectARMO& a_armor,
		const std::vector<RemoteItemMod>* a_mods,
		std::string_view a_logKey)
	{
		if (!a_mods) {
			const auto existing = F4T::EquipmentInstance::FindStack(a_proxy, a_armor.GetFormID(),
				[](const RE::BGSInventoryItem::Stack& a_stack) { return !F4T::EquipmentInstance::ReadStackMods(a_stack); });
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					std::string{ "proxy_361_plain_merge_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
					std::format("[RT-VERIFY 361-C] plain add formId={:08X} merge={}", a_armor.GetFormID(), existing.stack != nullptr),
					1s);
			}
			if (existing.stack) {
				return AddedArmorStack{ existing.stackId, nullptr };
			}
			const auto before = CensusStacks(a_proxy, a_armor.GetFormID());
			a_proxy.AddObjectToContainer(
				std::addressof(a_armor),
				RE::BSTSmartPointer<RE::ExtraDataList>{},
				1,
				nullptr,
				RE::ITEM_REMOVE_REASON::kNone);
			const auto after = CensusStacks(a_proxy, a_armor.GetFormID());
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					std::string{ "proxy_361_plain_census_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
					std::format("[RT-VERIFY 361-B] plain add formId={:08X} census total {}->{} nullExtra {}->{} remainingStackIds=[{}]",
						a_armor.GetFormID(), before.total, after.total, before.nullExtra, after.nullExtra, after.stackIds),
					1s);
			}
			const auto added = F4T::EquipmentInstance::FindStack(a_proxy, a_armor.GetFormID(),
				[](const RE::BGSInventoryItem::Stack& a_stack) { return !F4T::EquipmentInstance::ReadStackMods(a_stack); });
			if (!added.stack) {
				LogThrottledWarning(
					std::string{ "proxy_armor_plain_add_missing_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
					std::format("Plain armor add {:08X} produced no stack; the armor is not equipped in this pass.", a_armor.GetFormID()));
				return std::nullopt;
			}
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co367-armormods/README.md
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					std::string{ "proxy_361_plain_tail_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
					std::format("[RT-VERIFY 361-C] plain add formId={:08X} tracked tail stackId={} extra={:p} chainCount={}",
						a_armor.GetFormID(), added.stackId, static_cast<const void*>(added.stack->extra.get()), after.total),
					1s);
			}
			a_ledger.Track(TrackedStack{ a_armor.GetFormID(), added.stackId, added.stack->extra.get() });
			return AddedArmorStack{ added.stackId, nullptr };
		}

		const auto failureKey = InstanceDataFailureKey(a_armor.GetFormID(), *a_mods);
		if (a_ledger.HasInstanceDataFailure(failureKey)) {
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					std::string{ "proxy_361_mods_memo_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
					std::format("[RT-VERIFY 361-H] mods copy formId={:08X} mods={} skipped: instance data failed earlier for this recipe, base copy fallback used.",
						a_armor.GetFormID(), F4T::EquipmentState::DescribeMods(*a_mods)),
					1s);
			}
			return std::nullopt;
		}

		std::vector<RE::BGSMod::Attachment::Mod*> forms;
		forms.reserve(a_mods->size());
		for (const auto& mod : *a_mods) {
			auto* form = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(mod.formId);
			if (!form) {
				LogThrottledWarning(
					std::string{ "proxy_armor_mod_missing_" } + std::string{ a_logKey } + "_" + std::to_string(mod.formId),
					std::format("Could not resolve armor mod {:08X}; no recipe copy is added, so the base copy fallback runs.", mod.formId));
				return std::nullopt;
			}
			forms.push_back(form);
		}

		auto* instanceExtra = new RE::BGSObjectInstanceExtra(nullptr, std::addressof(a_armor), nullptr);
		RE::BSTSmartPointer<RE::ExtraDataList> extraList{ new RE::ExtraDataList() };
		extraList->AddExtra(instanceExtra);
		for (std::size_t i = 0; i < forms.size(); ++i) {
			instanceExtra->AddMod(*forms[i], (*a_mods)[i].index, (*a_mods)[i].rank, false);
		}

		const auto countBefore = CountStacks(a_proxy, a_armor.GetFormID());
		// TODO-verify(#407)
		a_proxy.AddObjectToContainer(
			std::addressof(a_armor),
			extraList,
			1,
			nullptr,
			RE::ITEM_REMOVE_REASON::kNone);
		const auto countAfter = CountStacks(a_proxy, a_armor.GetFormID());

		const auto found = F4T::EquipmentInstance::FindStack(a_proxy, a_armor.GetFormID(),
			[&](const RE::BGSInventoryItem::Stack& a_stack) { return a_stack.extra.get() == extraList.get(); });
		const bool owned = found.stack != nullptr;
		if (F4T::DebugLogging::IsEnabled()) {
			const auto census = CensusStacks(a_proxy, a_armor.GetFormID());
			LogThrottledInfo(
				std::string{ "proxy_361_mods_readback_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
				std::format("[RT-VERIFY 361-D] mods add formId={:08X} stackId={} expectedMods={} readBackMods={} remainingStackIds=[{}]",
					a_armor.GetFormID(), found.stackId, F4T::EquipmentState::DescribeMods(*a_mods),
					found.stack ? DescribeStackMods(*found.stack) : std::string{ "no stack" }, census.stackIds),
				1s);
		}
		if (!owned) {
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled() && ShouldLog(std::string{ "proxy_361_mods_pointer_miss_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()), std::chrono::hours{ 24 })) {
				const auto recipeMatch = F4T::EquipmentInstance::FindStack(a_proxy, a_armor.GetFormID(),
					[&](const RE::BGSInventoryItem::Stack& a_stack) {
						const auto stackMods = F4T::EquipmentInstance::ReadStackMods(a_stack);
						return stackMods && F4T::EquipmentInstance::SameMods(*stackMods, *a_mods);
					}).stack != nullptr;
				F4T::DebugLogging::Log("Equipment",
					"[RT-VERIFY 361-G] mods add formId={:08X} countBefore={} countAfter={} not matched by its extra pointer; recipe match found={}; not removed; base copy fallback runs; runtime question 361-C decides whether this copy stays untracked.",
					a_armor.GetFormID(), countBefore, countAfter, recipeMatch);
			}
			return std::nullopt;
		}
		// TODO-verify(#407)
		auto* instanceData = found.stack->extra
			? found.stack->extra->CreateInstanceData(std::addressof(a_armor), false)
			: nullptr;
		F4T::DebugLogging::Log("Equipment", "[RT-VERIFY 361-H] CreateInstanceData formId={:08X} stackId={} extraFound={} instanceData={}",
			a_armor.GetFormID(), found.stackId, found.stack->extra != nullptr, instanceData != nullptr);
		if (!instanceData) {
			a_ledger.RememberInstanceDataFailure(failureKey);
			LogThrottledWarning(
				std::string{ "proxy_armor_mod_instance_failed_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
				std::format("Armor {:08X} with mods has no instance data; the recipe copy is dropped or kept for retry, and the base copy fallback runs.", a_armor.GetFormID()));
			const TrackedStack added{ a_armor.GetFormID(), found.stackId, found.stack->extra.get() };
			if (F4T::ProxyEquipmentStacks::RetryTrackedStack(RemoveTrackedStack(a_ledger, a_proxy, added, a_logKey).outcome)) {
				a_ledger.Track(added);
				if (F4T::DebugLogging::IsEnabled()) {
					LogThrottledInfo(
						std::string{ "proxy_361_mods_retrack_" } + std::string{ a_logKey } + "_" + std::to_string(a_armor.GetFormID()),
						std::format("[RT-VERIFY 361-F] mods copy formId={:08X} stackId={} kept tracked for retry after instance data failure", a_armor.GetFormID(), found.stackId),
						1s);
				}
			}
			return std::nullopt;
		}

		if (F4T::DebugLogging::IsEnabled()) {
			F4T::DebugLogging::Log("Equipment", "[RT-VERIFY 361-D] mods copy tracked formId={:08X} stackId={} extra={:p} mods={}",
				a_armor.GetFormID(), found.stackId, static_cast<const void*>(found.stack->extra.get()), F4T::EquipmentState::DescribeMods(*a_mods));
		}
		a_ledger.Track(TrackedStack{ a_armor.GetFormID(), found.stackId, found.stack->extra.get() });
		return AddedArmorStack{ found.stackId, instanceData };
	}

	void RetainSyncedStacks(
		F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		const std::vector<ArmorTarget>& a_wantedArmor,
		const std::vector<RemoteItemMod>* a_rightHandMods,
		std::uint32_t a_rightHandWeaponFormId,
		std::string_view a_logKey)
	{
		// Constraint: the ledger holds only stacks this client added, one pass drops every entry whose stack is gone; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
		auto pending = a_ledger.ForgetAll();
		DropVanishedNullExtra(a_proxy, pending);
		std::vector<TrackedStack> kept;
		for (const auto& tracked : pending) {
			const auto* wanted = FindArmorTarget(a_wantedArmor, tracked.formId);
			const auto* wantedMods = wanted ? wanted->mods : a_rightHandMods;
			const bool wantedForm = wanted != nullptr || tracked.formId == a_rightHandWeaponFormId;
			const auto found = FindTrackedStack(a_proxy, tracked);
			const bool keep = F4T::ProxyEquipmentStacks::KeepTrackedStack(
				wantedForm,
				found.stack != nullptr,
				found.stack && F4T::EquipmentInstance::SameMods(
					F4T::EquipmentInstance::ReadStackMods(*found.stack).value_or(std::vector<RemoteItemMod>{}),
					wantedMods ? *wantedMods : std::vector<RemoteItemMod>{})) &&
				!F4T::ProxyEquipmentStacks::HasTrackedForm(kept, tracked.formId);
			if (F4T::ProxyEquipmentStacks::HasTrackedStack(kept, tracked)) {
				continue;
			}
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					"proxy_361_ledger_keep_" + std::string{ a_logKey } + "_" + std::to_string(tracked.formId),
					std::format("ledger keep formId={:08X} stackId={} wanted={} keep={}", tracked.formId, found.stackId, wantedForm, keep),
					1s);
			}
			const auto removal = keep ? F4T::ProxyEquipmentStacks::StackRemovalResult{}
				: RemoveTrackedStack(a_ledger, a_proxy, tracked, a_logKey);
			if (removal.outcome == StackRemoval::kRemoved) {
				// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
				if (removal.unlinked) {
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(pending, tracked.formId, removal.stackId);
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(kept, tracked.formId, removal.stackId);
				}
				RefreshStackIds(a_proxy, pending);
				RefreshStackIds(a_proxy, kept);
				continue;
			}
			if (keep || F4T::ProxyEquipmentStacks::RetryTrackedStack(removal.outcome)) {
				if (F4T::DebugLogging::IsEnabled()) {
					LogThrottledInfo(
						"proxy_ledger_retain_" + std::string{ a_logKey } + "_" + std::to_string(tracked.formId),
						std::format("ledger retain formId={:08X} stackId={} keep={}", tracked.formId, found.stackId, keep),
						1s);
				}
				a_ledger.Track(tracked);
			}
			if (keep) {
				kept.push_back(tracked);
			}
		}
	}
}
