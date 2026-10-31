#include "F4TProxyEquipmentStacks.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Constraint: this model mirrors the ledger use in ApplyProxyEquipmentFromRemoteState; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
namespace
{
	using F4T::ProxyEquipmentStacks::Ledger;
	using F4T::ProxyEquipmentStacks::StackRemoval;
	using F4T::ProxyEquipmentStacks::StackRemovalResult;
	using F4T::ProxyEquipmentStacks::TrackedStack;

	struct FakeStack
	{
		std::uint32_t slot{};
		std::uint32_t formId{};
		std::uint32_t recipe{};
		const void* extra{ nullptr };
		bool equipped{ false };
		std::uint32_t count{ 1 };
	};

	// Constraint: a state comparison needs the full wanted slot identity; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	struct Wanted
	{
		std::uint32_t slot{};
		std::uint32_t formId{};
		std::uint32_t recipe{};

		bool operator==(const Wanted&) const = default;
	};

	struct FakeProxy
	{
		std::vector<FakeStack> stacks;
		std::uintptr_t nextExtra{ 1 };
		std::uint32_t failAddFormId{};
		bool mergeReorder{ false };

		// Constraint: a stack with no recipe is added with a null extra, exactly like the applier's plain path; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
		FakeStack* Add(const Wanted& a_want)
		{
			if (a_want.formId == failAddFormId) {
				return nullptr;
			}
			const auto* extra = a_want.recipe == 0 ? nullptr : reinterpret_cast<const void*>(nextExtra++);
			stacks.push_back({ a_want.slot, a_want.formId, a_want.recipe, extra, false });
			return std::addressof(stacks.back());
		}

		std::uint32_t EquippedStackId(std::uint32_t a_formId) const
		{
			std::uint32_t stackId = 0;
			for (const auto& stack : stacks) {
				if (stack.formId != a_formId) {
					continue;
				}
				if (stack.equipped) {
					return stackId;
				}
				++stackId;
			}
			return 0;
		}

		bool UnequipEquipped(std::uint32_t a_formId)
		{
			const auto wantedId = EquippedStackId(a_formId);
			std::uint32_t stackId = 0;
			for (auto& stack : stacks) {
				if (stack.formId != a_formId) {
					continue;
				}
				if (stackId == wantedId && stack.equipped) {
					stack.equipped = false;
					return true;
				}
				++stackId;
			}
			return false;
		}

		std::uint32_t StackId(const FakeStack& a_stack) const
		{
			std::uint32_t stackId = 0;
			for (const auto& stack : stacks) {
				if (stack.formId != a_stack.formId) {
					continue;
				}
				if (std::addressof(stack) == std::addressof(a_stack)) {
					return stackId;
				}
				++stackId;
			}
			return 0;
		}

		FakeStack* FindTracked(const TrackedStack& a_tracked)
		{
			std::uint32_t stackId = 0;
			for (auto& stack : stacks) {
				if (stack.formId != a_tracked.formId) {
					continue;
				}
				if (F4T::ProxyEquipmentStacks::MatchesTrackedStack(a_tracked, stack.formId, stackId, stack.extra)) {
					return std::addressof(stack);
				}
				++stackId;
			}
			return nullptr;
		}

		StackRemovalResult Remove(const TrackedStack& a_tracked)
		{
			auto* stack = FindTracked(a_tracked);
			if (!stack) {
				return {};
			}
			const auto removedStackId = StackId(*stack);
			if (stack->equipped) {
				return { StackRemoval::kBlocked, removedStackId };
			}
			// Constraint: one unit is removed and the model unlinks only at zero, and mergeReorder moves the equipped stack to the chain head; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
			const bool unlinked = --stack->count == 0;
			if (unlinked) {
				std::erase_if(stacks, [&](const FakeStack& a_stack) { return std::addressof(a_stack) == stack; });
			}
			if (mergeReorder) {
				const auto equipped = std::find_if(stacks.begin(), stacks.end(), [&](const FakeStack& a_stack) { return a_stack.formId == a_tracked.formId && a_stack.equipped; });
				if (equipped != stacks.end()) {
					std::rotate(stacks.begin(), equipped, equipped + 1);
				}
			}
			return { StackRemoval::kRemoved, removedStackId, unlinked };
		}

		FakeStack* FindInSlot(std::uint32_t a_slot)
		{
			for (auto& stack : stacks) {
				if (stack.slot == a_slot && stack.equipped) {
					return std::addressof(stack);
				}
			}
			return nullptr;
		}

		FakeStack* FindRecipe(std::uint32_t a_formId, std::uint32_t a_recipe)
		{
			for (auto& stack : stacks) {
				if (stack.formId == a_formId && stack.recipe == a_recipe) {
					return std::addressof(stack);
				}
			}
			return nullptr;
		}
	};

	std::optional<std::uint32_t> LiveStackId(FakeProxy& a_proxy, const TrackedStack& a_tracked)
	{
		if (!a_tracked.extra) {
			std::optional<std::uint32_t> plainId;
			std::size_t plainCount = 0;
			std::uint32_t stackId = 0;
			for (const auto& stack : a_proxy.stacks) {
				if (stack.formId != a_tracked.formId) {
					continue;
				}
				if (!stack.extra) {
					plainId = stackId;
					++plainCount;
				}
				++stackId;
			}
			// TODO-verify(#407) source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md (rows 3, 18: at most one null-extra stack per form)
			if (plainCount != 1) {
				return std::nullopt;
			}
			return plainId;
		}
		const auto* stack = a_proxy.FindTracked(a_tracked);
		if (!stack) {
			return std::nullopt;
		}
		return a_proxy.StackId(*stack);
	}

	void RefreshIds(FakeProxy& a_proxy, std::vector<TrackedStack>& a_stacks)
	{
		F4T::ProxyEquipmentStacks::RefreshTrackedStackIds(a_stacks, [&](const TrackedStack& a_entry) { return LiveStackId(a_proxy, a_entry); });
	}

	void DropVanishedNullExtra(FakeProxy& a_proxy, std::vector<TrackedStack>& a_stacks)
	{
		std::erase_if(a_stacks, [&](const TrackedStack& a_entry) { return !a_entry.extra && !a_proxy.FindTracked(a_entry); });
	}

	const Wanted* FindWanted(const std::vector<Wanted>& a_wanted, std::uint32_t a_formId)
	{
		for (const auto& want : a_wanted) {
			if (want.formId == a_formId) {
				return std::addressof(want);
			}
		}
		return nullptr;
	}

	std::uint32_t TrackedRecipe(FakeProxy& a_proxy, Ledger& a_ledger, std::uint32_t a_formId)
	{
		const auto* tracked = a_ledger.Find(a_formId);
		if (!tracked) {
			return 0;
		}
		const auto* stack = a_proxy.FindTracked(*tracked);
		return stack ? stack->recipe : 0;
	}

	// Constraint: applied state latches only when every wanted slot was applied; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	struct ApplyState
	{
		bool hasApplied{ false };
		std::vector<Wanted> lastApplied;
	};

	// Constraint: the model must decide through the shared helpers, never a private copy of the rules; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	bool ApplyOutfit(FakeProxy& a_proxy, Ledger& a_ledger, const std::vector<Wanted>& a_wanted, ApplyState* a_state = nullptr)
	{
		if (a_state && a_state->hasApplied && a_state->lastApplied == a_wanted) {
			return true;
		}
		bool allApplied = true;

		for (const auto& want : a_wanted) {
			auto* current = a_proxy.FindInSlot(want.slot);
			const bool sameRecipe = current && current->recipe == want.recipe;
			if (current && !F4T::ProxyEquipmentStacks::SlotNeedsWork(current->formId, want.formId, sameRecipe)) {
				continue;
			}
			if (current) {
				current->equipped = false;
			}
			auto* stack = a_proxy.FindRecipe(want.formId, want.recipe);
			if (!stack) {
				stack = a_proxy.Add(want);
				if (!stack) {
					allApplied = false;
					continue;
				}
				a_ledger.Track({ stack->formId, a_proxy.StackId(*stack), stack->extra });
			}
			stack->slot = want.slot;
			stack->equipped = true;
		}

		// Constraint: a removal re-resolves the live ids and shifts only null-extra ids, exactly like RemoveTrackedStacks; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
		auto pending = a_ledger.ForgetAll();
		DropVanishedNullExtra(a_proxy, pending);
		std::vector<TrackedStack> kept;
		for (const auto& tracked : pending) {
			const auto* stack = a_proxy.FindTracked(tracked);
			const auto* want = FindWanted(a_wanted, tracked.formId);
			const bool keep = F4T::ProxyEquipmentStacks::KeepTrackedStack(
				want != nullptr,
				stack != nullptr,
				stack && want && stack->recipe == want->recipe) &&
				!F4T::ProxyEquipmentStacks::HasTrackedForm(kept, tracked.formId);
			if (F4T::ProxyEquipmentStacks::HasTrackedStack(kept, tracked)) {
				continue;
			}
			const auto removal = keep ? StackRemovalResult{} : a_proxy.Remove(tracked);
			// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
			if (removal.outcome == StackRemoval::kRemoved) {
				if (removal.unlinked) {
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(pending, tracked.formId, removal.stackId);
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(kept, tracked.formId, removal.stackId);
					a_ledger.ShiftStackIdsAfterRemoval(tracked.formId, removal.stackId);
				}
				// Constraint: the drained and kept ids are re-resolved from the live chain after a removal; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
				RefreshIds(a_proxy, pending);
				RefreshIds(a_proxy, kept);
				a_ledger.RefreshStackIds([&](const TrackedStack& a_entry) { return LiveStackId(a_proxy, a_entry); });
				continue;
			}
			if (keep || F4T::ProxyEquipmentStacks::RetryTrackedStack(removal.outcome)) {
				a_ledger.Track(tracked);
			}
			if (keep) {
				kept.push_back(tracked);
			}
		}

		if (a_state && allApplied) {
			a_state->hasApplied = true;
			a_state->lastApplied = a_wanted;
		}
		return allApplied;
	}

	void ClearOutfit(FakeProxy& a_proxy, Ledger& a_ledger, bool a_clearEquipped = true)
	{
		if (a_clearEquipped) {
			for (auto& stack : a_proxy.stacks) {
				stack.equipped = false;
			}
		}
		auto pending = a_ledger.ForgetAll();
		DropVanishedNullExtra(a_proxy, pending);
		for (const auto& tracked : pending) {
			const auto removal = a_proxy.Remove(tracked);
			// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
			if (removal.outcome == StackRemoval::kRemoved) {
				if (removal.unlinked) {
					F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(pending, tracked.formId, removal.stackId);
					a_ledger.ShiftStackIdsAfterRemoval(tracked.formId, removal.stackId);
				}
				RefreshIds(a_proxy, pending);
				a_ledger.RefreshStackIds([&](const TrackedStack& a_entry) { return LiveStackId(a_proxy, a_entry); });
				continue;
			}
			if (F4T::ProxyEquipmentStacks::RetryTrackedStack(removal.outcome)) {
				a_ledger.Track(tracked);
			}
		}
	}
}

int main()
{
	Ledger ledger;
	assert(ledger.Size() == 0);
	assert(ledger.Find(0x111) == nullptr);

	const TrackedStack first{ 0x111, 0, reinterpret_cast<const void*>(0x10) };
	ledger.Track(first);
	assert(ledger.Size() == 1);
	assert(ledger.Find(0x111) && *ledger.Find(0x111) == first);

	const TrackedStack replacement{ 0x111, 1, reinterpret_cast<const void*>(0x20) };
	ledger.Track(replacement);
	assert(ledger.Size() == 2);
	assert(ledger.Find(0x111) && *ledger.Find(0x111) == first);

	const auto all = ledger.ForgetAll();
	assert(all.size() == 2);
	assert(ledger.Size() == 0);
	assert(all[0] == first && all[1] == replacement);

	ledger.Track({ 0x222, 0, reinterpret_cast<const void*>(0x30) });
	assert(ledger.Find(0x222) != nullptr);
	ledger.ForgetAll();
	assert(ledger.Find(0x222) == nullptr);

	const auto extraA = reinterpret_cast<const void*>(0x40);
	const auto extraB = reinterpret_cast<const void*>(0x50);
	assert(F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 0, nullptr }, 0xA1, 0, nullptr));
	assert(!F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 0, nullptr }, 0xA1, 1, nullptr));
	assert(!F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 0, nullptr }, 0xA1, 0, extraA));
	assert(!F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 0, nullptr }, 0xA2, 0, nullptr));
	assert(F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 7, extraA }, 0xA1, 0, extraA));
	assert(!F4T::ProxyEquipmentStacks::MatchesTrackedStack({ 0xA1, 7, extraA }, 0xA1, 7, extraB));

	const auto extraC = reinterpret_cast<const void*>(0x60);
	std::vector<TrackedStack> shiftable{ { 0xA1, 0, nullptr }, { 0xA1, 2, nullptr }, { 0xA1, 3, extraC }, { 0xA2, 3, nullptr } };
	F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(shiftable, 0xA1, 1);
	assert(shiftable[0].stackId == 0 && shiftable[1].stackId == 1);
	assert(shiftable[2].stackId == 3 && shiftable[3].stackId == 3);

	Ledger shiftingLedger;
	shiftingLedger.Track({ 0xA1, 2, nullptr });
	shiftingLedger.ShiftStackIdsAfterRemoval(0xA1, 1);
	assert(shiftingLedger.Find(0xA1) != nullptr && shiftingLedger.Find(0xA1)->stackId == 1);

	std::vector<TrackedStack> refreshable{ { 0xA1, 0, extraC }, { 0xA1, 2, nullptr }, { 0xA1, 5, extraA } };
	F4T::ProxyEquipmentStacks::RefreshTrackedStackIds(refreshable, [&](const TrackedStack& a_entry) -> std::optional<std::uint32_t> {
		if (a_entry.extra == extraC) {
			return 3u;
		}
		return std::nullopt;
	});
	assert(refreshable[0].stackId == 3 && refreshable[1].stackId == 2 && refreshable[2].stackId == 5);
	Ledger refreshLedger;
	refreshLedger.Track({ 0xA1, 4, extraC });
	refreshLedger.RefreshStackIds([](const TrackedStack&) -> std::optional<std::uint32_t> { return 1u; });
	assert(refreshLedger.Find(0xA1) != nullptr && refreshLedger.Find(0xA1)->stackId == 1);

	assert(F4T::ProxyEquipmentStacks::RetryTrackedStack(StackRemoval::kBlocked));
	assert(!F4T::ProxyEquipmentStacks::RetryTrackedStack(StackRemoval::kRemoved));
	assert(!F4T::ProxyEquipmentStacks::RetryTrackedStack(StackRemoval::kMissing));

	assert(!F4T::ProxyEquipmentStacks::KeepTrackedStack(false, true, true));
	assert(!F4T::ProxyEquipmentStacks::KeepTrackedStack(true, false, true));
	assert(!F4T::ProxyEquipmentStacks::KeepTrackedStack(true, true, false));
	assert(F4T::ProxyEquipmentStacks::KeepTrackedStack(true, true, true));

	assert(!F4T::ProxyEquipmentStacks::SlotNeedsWork(0xA1, 0xA1, true));
	assert(F4T::ProxyEquipmentStacks::SlotNeedsWork(0xA1, 0xA1, false));
	assert(F4T::ProxyEquipmentStacks::SlotNeedsWork(0xA1, 0xA2, true));
	assert(F4T::ProxyEquipmentStacks::SlotNeedsWork(0, 0xA1, true));

	FakeProxy proxy;
	Ledger outfitLedger;
	const std::vector<Wanted> outfitA{ { 1, 0xA1, 1 }, { 2, 0xA2, 1 } };
	const std::vector<Wanted> outfitB{ { 1, 0xB1, 1 }, { 2, 0xB2, 1 } };
	const std::vector<Wanted> outfitARemodded{ { 1, 0xA1, 2 }, { 2, 0xA2, 1 } };
	const std::vector<Wanted> outfitABase{ { 1, 0xA1, 0 }, { 2, 0xA2, 1 } };

	ApplyOutfit(proxy, outfitLedger, outfitA);
	assert(proxy.stacks.size() == 2 && outfitLedger.Size() == 2);
	assert(TrackedRecipe(proxy, outfitLedger, 0xA1) == 1);

	ApplyOutfit(proxy, outfitLedger, outfitA);
	ApplyOutfit(proxy, outfitLedger, outfitA);
	assert(proxy.stacks.size() == 2 && outfitLedger.Size() == 2);

	ApplyOutfit(proxy, outfitLedger, outfitARemodded);
	assert(proxy.stacks.size() == 2 && outfitLedger.Size() == 2);
	assert(TrackedRecipe(proxy, outfitLedger, 0xA1) == 2);

	ApplyOutfit(proxy, outfitLedger, outfitABase);
	assert(proxy.stacks.size() == 2 && outfitLedger.Size() == 2);
	assert(TrackedRecipe(proxy, outfitLedger, 0xA1) == 0);

	ApplyOutfit(proxy, outfitLedger, outfitB);
	assert(proxy.stacks.size() == 2 && outfitLedger.Size() == 2);
	assert(outfitLedger.Find(0xA1) == nullptr && outfitLedger.Find(0xB1) != nullptr);

	for (int i = 0; i < 200; ++i) {
		ApplyOutfit(proxy, outfitLedger, (i % 3 == 0) ? outfitA : (i % 3 == 1) ? outfitARemodded : outfitB);
	}
	assert(proxy.stacks.size() == 2);
	assert(outfitLedger.Size() == 2);

	ClearOutfit(proxy, outfitLedger);
	assert(proxy.stacks.empty());
	assert(outfitLedger.Size() == 0);

	FakeProxy reusedProxy;
	Ledger reusedLedger;
	auto* foreign = reusedProxy.Add({ 1, 0xA1, 1 });
	foreign->equipped = true;
	ApplyOutfit(reusedProxy, reusedLedger, outfitA);
	assert(reusedProxy.stacks.size() == 2);
	assert(reusedLedger.Size() == 1);
	assert(reusedLedger.Find(0xA1) == nullptr && reusedLedger.Find(0xA2) != nullptr);
	ClearOutfit(reusedProxy, reusedLedger);
	assert(reusedProxy.stacks.size() == 1 && reusedProxy.stacks.front().formId == 0xA1);
	assert(reusedLedger.Size() == 0);

	FakeProxy goneProxy;
	Ledger goneLedger;
	ApplyOutfit(goneProxy, goneLedger, outfitA);
	assert(goneLedger.Size() == 2);
	goneProxy.stacks.clear();
	ApplyOutfit(goneProxy, goneLedger, { outfitA.front() });
	assert(goneProxy.stacks.size() == 1);
	assert(goneLedger.Size() == 1);
	assert(goneLedger.Find(0xA2) == nullptr);

	FakeProxy stuckProxy;
	Ledger stuckLedger;
	ApplyOutfit(stuckProxy, stuckLedger, { { 3, 0xA1, 1 } });
	assert(stuckProxy.stacks.size() == 1 && stuckLedger.Size() == 1);
	ApplyOutfit(stuckProxy, stuckLedger, { { 1, 0xA1, 2 } });
	assert(stuckProxy.stacks.size() == 2 && stuckLedger.Size() == 2);
	ApplyOutfit(stuckProxy, stuckLedger, { { 1, 0xA1, 2 }, { 3, 0xA1, 2 } });
	assert(stuckProxy.stacks.size() == 1 && stuckLedger.Size() == 1);
	assert(TrackedRecipe(stuckProxy, stuckLedger, 0xA1) == 2);

	FakeProxy slotlessProxy;
	Ledger slotlessLedger;
	ApplyOutfit(slotlessProxy, slotlessLedger, outfitA);
	ApplyOutfit(slotlessProxy, slotlessLedger, { outfitA.front() });
	assert(slotlessProxy.stacks.size() == 2 && slotlessLedger.Size() == 2);
	ClearOutfit(slotlessProxy, slotlessLedger);
	assert(slotlessProxy.stacks.empty() && slotlessLedger.Size() == 0);

	// Constraint: a modded stack removed from below a null-extra entry shifts that entry's id, so the clear still finds it; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy shiftedProxy;
	Ledger shiftedLedger;
	ApplyOutfit(shiftedProxy, shiftedLedger, { { 1, 0xA1, 1 } });
	assert(shiftedProxy.stacks.size() == 1 && shiftedLedger.Size() == 1);
	assert(shiftedProxy.stacks.front().extra != nullptr);
	ApplyOutfit(shiftedProxy, shiftedLedger, { { 1, 0xA1, 0 } });
	assert(shiftedProxy.stacks.size() == 1 && shiftedProxy.stacks.front().extra == nullptr);
	assert(shiftedLedger.Size() == 1);
	assert(shiftedLedger.Find(0xA1) != nullptr && shiftedLedger.Find(0xA1)->stackId == 0);
	assert(TrackedRecipe(shiftedProxy, shiftedLedger, 0xA1) == 0);
	ClearOutfit(shiftedProxy, shiftedLedger);
	assert(shiftedProxy.stacks.empty() && shiftedLedger.Size() == 0);

	// Constraint: a blocked removal stays tracked until the stack is actually removable; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy blockedProxy;
	Ledger blockedLedger;
	ApplyOutfit(blockedProxy, blockedLedger, outfitA);
	ClearOutfit(blockedProxy, blockedLedger, false);
	assert(blockedProxy.stacks.size() == 2 && blockedLedger.Size() == 2);
	auto* unblocked = blockedProxy.FindInSlot(1);
	assert(unblocked != nullptr);
	unblocked->equipped = false;
	ClearOutfit(blockedProxy, blockedLedger, false);
	assert(blockedProxy.stacks.size() == 1 && blockedLedger.Size() == 1);
	ClearOutfit(blockedProxy, blockedLedger);
	assert(blockedProxy.stacks.empty() && blockedLedger.Size() == 0);

	// Constraint: unequip resolves the equipped stack id, never stack id 0; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy dualStackProxy;
	dualStackProxy.Add({ 1, 0xC1, 1 });
	dualStackProxy.Add({ 2, 0xC1, 1 });
	assert(dualStackProxy.stacks.size() == 2);
	dualStackProxy.stacks[1].equipped = true;
	assert(dualStackProxy.EquippedStackId(0xC1) == 1);
	assert(dualStackProxy.UnequipEquipped(0xC1));
	assert(dualStackProxy.stacks.size() == 2);
	assert(!dualStackProxy.stacks[0].equipped && !dualStackProxy.stacks[1].equipped);
	dualStackProxy.stacks[0].equipped = true;
	assert(dualStackProxy.EquippedStackId(0xC1) == 0);
	assert(dualStackProxy.UnequipEquipped(0xC1));
	assert(!dualStackProxy.stacks[0].equipped && !dualStackProxy.stacks[1].equipped);

	// Constraint: a failed add leaves applied state unlatched so the next pass retries; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy retryProxy;
	Ledger retryLedger;
	ApplyState retryState;
	retryProxy.failAddFormId = 0xA2;
	assert(!ApplyOutfit(retryProxy, retryLedger, outfitA, &retryState));
	assert(!retryState.hasApplied && retryState.lastApplied.empty());
	retryProxy.failAddFormId = 0;
	assert(ApplyOutfit(retryProxy, retryLedger, outfitA, &retryState));
	assert(retryState.hasApplied && retryState.lastApplied == outfitA);
	assert(retryProxy.FindInSlot(1) != nullptr && retryProxy.FindInSlot(2) != nullptr);
	assert(ApplyOutfit(retryProxy, retryLedger, outfitA, &retryState));
	assert(retryProxy.stacks.size() == 2 && retryLedger.Size() == 2);

	// Constraint: retention keeps one tracked stack per form and removes a distinct duplicate; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy duplicateProxy;
	Ledger duplicateLedger;
	auto* lower = duplicateProxy.Add({ 0, 0xD1, 2 });
	auto* upper = duplicateProxy.Add({ 0, 0xD1, 2 });
	assert(lower != nullptr && upper != nullptr);
	duplicateLedger.Track({ 0xD1, duplicateProxy.StackId(*lower), lower->extra });
	duplicateLedger.Track({ 0xD1, duplicateProxy.StackId(*upper), upper->extra });
	assert(duplicateLedger.Size() == 2);
	ApplyOutfit(duplicateProxy, duplicateLedger, { { 1, 0xD1, 2 } });
	assert(duplicateProxy.stacks.size() == 1 && duplicateLedger.Size() == 1);
	assert(duplicateProxy.stacks.front().equipped);

	// Constraint: one stack tracked twice is dropped once, without removing the stack itself; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy sameStackProxy;
	Ledger sameStackLedger;
	auto* single = sameStackProxy.Add({ 0, 0xD2, 2 });
	assert(single != nullptr);
	sameStackLedger.Track({ 0xD2, 0, single->extra });
	sameStackLedger.Track({ 0xD2, 0, single->extra });
	ApplyOutfit(sameStackProxy, sameStackLedger, { { 1, 0xD2, 2 } });
	assert(sameStackProxy.stacks.size() == 1 && sameStackLedger.Size() == 1);
	assert(sameStackProxy.stacks.front().equipped);
	ClearOutfit(sameStackProxy, sameStackLedger);
	assert(sameStackProxy.stacks.empty() && sameStackLedger.Size() == 0);

	// Constraint: kept ids shift with the drained ids, so a later sibling stack is removed rather than mistaken for a kept duplicate; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy shiftKeptProxy;
	Ledger shiftKeptLedger;
	shiftKeptProxy.Add({ 0, 0xE1, 2 });
	shiftKeptProxy.Add({ 0, 0xE1, 0 });
	shiftKeptProxy.Add({ 0, 0xE1, 0 });
	assert(shiftKeptProxy.stacks.size() == 3 && shiftKeptProxy.stacks[0].extra != nullptr);
	shiftKeptLedger.Track({ 0xE1, 1, nullptr });
	shiftKeptLedger.Track({ 0xE1, 0, shiftKeptProxy.stacks[0].extra });
	shiftKeptLedger.Track({ 0xE1, 2, nullptr });
	assert(shiftKeptLedger.Size() == 3);
	assert(ApplyOutfit(shiftKeptProxy, shiftKeptLedger, { { 1, 0xE1, 0 } }));
	assert(shiftKeptProxy.stacks.size() == 1);
	assert(shiftKeptProxy.stacks.front().formId == 0xE1 && shiftKeptProxy.stacks.front().extra == nullptr);
	assert(shiftKeptProxy.stacks.front().equipped);
	assert(shiftKeptLedger.Size() == 1);
	assert(shiftKeptLedger.Find(0xE1) != nullptr && shiftKeptLedger.Find(0xE1)->stackId == 0);

	Ledger instanceFailureLedger;
	assert(!instanceFailureLedger.HasInstanceDataFailure("0000ABCD[00054321:2:3]"));
	instanceFailureLedger.RememberInstanceDataFailure("0000ABCD[00054321:2:3]");
	instanceFailureLedger.ForgetAll();
	assert(instanceFailureLedger.HasInstanceDataFailure("0000ABCD[00054321:2:3]"));
	instanceFailureLedger.RememberInstanceDataFailure("0000ABCD[00054321:2:4]");
	assert(instanceFailureLedger.ForgetInstanceDataFailures() == 2);
	assert(!instanceFailureLedger.HasInstanceDataFailure("0000ABCD[00054321:2:3]"));
	assert(!instanceFailureLedger.HasInstanceDataFailure("0000ABCD[00054321:2:4]"));
	assert(instanceFailureLedger.ForgetInstanceDataFailures() == 0);

	FakeProxy oldestWinsProxy;
	oldestWinsProxy.stacks.push_back({ 1, 0x111, 5, reinterpret_cast<const void*>(0x60), false });
	oldestWinsProxy.stacks.push_back({ 2, 0x111, 5, reinterpret_cast<const void*>(0x70), false });
	Ledger oldestWinsLedger;
	oldestWinsLedger.Track({ 0x111, 0, reinterpret_cast<const void*>(0x60) });
	oldestWinsLedger.Track({ 0x111, 1, reinterpret_cast<const void*>(0x70) });
	assert(ApplyOutfit(oldestWinsProxy, oldestWinsLedger, { Wanted{ 3, 0x111, 5 } }));
	const auto* oldestWinsTracked = oldestWinsLedger.Find(0x111);
	assert(oldestWinsTracked && oldestWinsTracked->extra == reinterpret_cast<const void*>(0x60));
	assert(oldestWinsProxy.stacks.size() == 1);
	assert(oldestWinsProxy.FindTracked(*oldestWinsTracked) == std::addressof(oldestWinsProxy.stacks[0]));
	assert(oldestWinsProxy.stacks[0].equipped && oldestWinsProxy.stacks[0].slot == 3);

	FakeProxy partialProxy;
	partialProxy.stacks.push_back({ 1, 0xF1, 0, reinterpret_cast<const void*>(0x80), false, 2 });
	partialProxy.stacks.push_back({ 2, 0xF1, 0, nullptr, false, 1 });
	Ledger partialLedger;
	partialLedger.Track({ 0xF1, 0, reinterpret_cast<const void*>(0x80) });
	partialLedger.Track({ 0xF1, 1, nullptr });
	ClearOutfit(partialProxy, partialLedger);
	assert(partialLedger.Size() == 0);
	assert(partialProxy.stacks.size() == 1);
	assert(partialProxy.stacks.front().extra == reinterpret_cast<const void*>(0x80) && partialProxy.stacks.front().count == 1);

	// Constraint: a merge that moves the equipped stack to the chain head re-resolves the stored id of a modded entry; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
	FakeProxy headProxy;
	headProxy.mergeReorder = true;
	Ledger headLedger;
	headProxy.stacks.push_back({ 0, 0xB1, 1, reinterpret_cast<const void*>(0x90), false, 2 });
	headProxy.stacks.push_back({ 2, 0xB1, 2, reinterpret_cast<const void*>(0xA0), true, 1 });
	headLedger.Track({ 0xB1, 0, reinterpret_cast<const void*>(0x90) });
	headLedger.Track({ 0xB1, 1, reinterpret_cast<const void*>(0xA0) });
	assert(ApplyOutfit(headProxy, headLedger, { { 2, 0xB1, 2 } }));
	assert(headProxy.stacks.size() == 2 && headLedger.Size() == 1);
	assert(headProxy.stacks.front().equipped && headProxy.stacks.front().extra == reinterpret_cast<const void*>(0xA0));
	assert(headLedger.Find(0xB1) != nullptr && headLedger.Find(0xB1)->stackId == headProxy.StackId(headProxy.stacks.front()));

	// Constraint: a plain entry behind an unlink and a merge reorder is re-resolved from the live chain, not only shifted; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
	FakeProxy plainHeadProxy;
	plainHeadProxy.mergeReorder = true;
	Ledger plainHeadLedger;
	plainHeadProxy.stacks.push_back({ 0, 0xB2, 1, reinterpret_cast<const void*>(0xB0), false, 1 });
	plainHeadProxy.stacks.push_back({ 1, 0xB2, 0, nullptr, false, 1 });
	plainHeadProxy.stacks.push_back({ 2, 0xB2, 2, reinterpret_cast<const void*>(0xC0), true, 1 });
	plainHeadLedger.Track({ 0xB2, 0, reinterpret_cast<const void*>(0xB0) });
	plainHeadLedger.Track({ 0xB2, 1, nullptr });
	plainHeadLedger.Track({ 0xB2, 2, reinterpret_cast<const void*>(0xC0) });
	ClearOutfit(plainHeadProxy, plainHeadLedger, false);
	assert(plainHeadProxy.stacks.size() == 1 && plainHeadProxy.stacks.front().extra == reinterpret_cast<const void*>(0xC0));
	assert(plainHeadLedger.Size() == 1 && plainHeadLedger.Find(0xB2) != nullptr && plainHeadLedger.Find(0xB2)->stackId == 0);

	// Constraint: a null-extra entry whose own stack has vanished is dropped before the pass, so an earlier removal cannot re-bind it to a foreign null-extra stack; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	FakeProxy vanishedProxy;
	Ledger vanishedLedger;
	vanishedProxy.stacks.push_back({ 0, 0xB3, 1, reinterpret_cast<const void*>(0xD0), false, 1 });
	vanishedProxy.stacks.push_back({ 1, 0xB3, 0, nullptr, false, 1 });
	vanishedLedger.Track({ 0xB3, 0, reinterpret_cast<const void*>(0xD0) });
	vanishedLedger.Track({ 0xB3, 0, nullptr });
	ClearOutfit(vanishedProxy, vanishedLedger, false);
	assert(vanishedProxy.stacks.size() == 1 && vanishedProxy.stacks.front().extra == nullptr);
	assert(vanishedLedger.Size() == 0);
}
