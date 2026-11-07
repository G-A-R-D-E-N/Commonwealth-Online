#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace F4T::ProxyEquipmentStacks
{
	// Constraint: a tracked stack is identified by form id, stack id and extra pointer; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	struct TrackedStack
	{
		std::uint32_t formId{};
		std::uint32_t stackId{};
		const void* extra{ nullptr };

		bool operator==(const TrackedStack&) const = default;
	};

	[[nodiscard]] inline bool MatchesTrackedStack(
		const TrackedStack& a_tracked,
		std::uint32_t a_formId,
		std::uint32_t a_stackId,
		const void* a_extra)
	{
		if (a_tracked.formId != a_formId) {
			return false;
		}
		if (a_tracked.extra) {
			return a_extra == a_tracked.extra;
		}
		return a_extra == nullptr && a_stackId == a_tracked.stackId;
	}

	// Constraint: an unlink shifts only the null-extra entries above it; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
	inline void ShiftTrackedStackIdsAfterRemoval(
		std::vector<TrackedStack>& a_stacks,
		std::uint32_t a_formId,
		std::uint32_t a_removedStackId)
	{
		for (auto& entry : a_stacks) {
			if (entry.formId == a_formId && entry.extra == nullptr && entry.stackId > a_removedStackId) {
				entry.stackId -= 1;
			}
		}
	}

	// Constraint: after a removal a tracked stack takes the id the live chain resolves for it, and a null-extra stack resolves only when it is the sole null-extra stack of its form; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md.
	template <class Resolve>
	inline void RefreshTrackedStackIds(std::vector<TrackedStack>& a_stacks, Resolve a_resolve)
	{
		for (auto& entry : a_stacks) {
			if (const auto stackId = a_resolve(entry)) {
				entry.stackId = *stackId;
			}
		}
	}

	enum class StackRemoval
	{
		kRemoved,
		kMissing,
		kBlocked
	};

	// Constraint: a removal outcome carries the stack index it removed; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	struct StackRemovalResult
	{
		StackRemoval outcome{ StackRemoval::kMissing };
		std::uint32_t stackId{};
		bool unlinked{ false };
	};

	// Constraint: every stack this client added stays tracked until it is removed; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	class Ledger
	{
	public:
		[[nodiscard]] const TrackedStack* Find(std::uint32_t a_formId) const
		{
			return Find(a_formId, [](const TrackedStack&) { return true; });
		}

		// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
		template <class Predicate>
		[[nodiscard]] const TrackedStack* Find(std::uint32_t a_formId, Predicate a_predicate) const
		{
			for (const auto& entry : stacks_) {
				if (entry.formId == a_formId && a_predicate(entry)) {
					return std::addressof(entry);
				}
			}
			return nullptr;
		}

		void Track(TrackedStack a_stack)
		{
			stacks_.push_back(a_stack);
		}

		void ShiftStackIdsAfterRemoval(std::uint32_t a_formId, std::uint32_t a_removedStackId)
		{
			ShiftTrackedStackIdsAfterRemoval(stacks_, a_formId, a_removedStackId);
		}

		template <class Resolve>
		void RefreshStackIds(Resolve a_resolve)
		{
			RefreshTrackedStackIds(stacks_, a_resolve);
		}

		std::vector<TrackedStack> ForgetAll()
		{
			return std::exchange(stacks_, std::vector<TrackedStack>{});
		}

		void RememberInstanceDataFailure(std::string a_key)
		{
			instanceDataFailures_.insert(std::move(a_key));
		}

		[[nodiscard]] bool HasInstanceDataFailure(const std::string& a_key) const
		{
			return instanceDataFailures_.contains(a_key);
		}

		std::size_t ForgetInstanceDataFailures()
		{
			const auto forgotten = instanceDataFailures_.size();
			instanceDataFailures_.clear();
			return forgotten;
		}

		[[nodiscard]] std::size_t Size() const
		{
			return stacks_.size();
		}

	private:
		std::vector<TrackedStack> stacks_;
		std::set<std::string> instanceDataFailures_;
	};

	// Constraint: only one tracked stack per form survives retention; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	[[nodiscard]] inline bool HasTrackedForm(const std::vector<TrackedStack>& a_stacks, std::uint32_t a_formId)
	{
		for (const auto& entry : a_stacks) {
			if (entry.formId == a_formId) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline bool HasTrackedStack(const std::vector<TrackedStack>& a_stacks, const TrackedStack& a_candidate)
	{
		for (const auto& entry : a_stacks) {
			if (MatchesTrackedStack(entry, a_candidate.formId, a_candidate.stackId, a_candidate.extra)) {
				return true;
			}
		}
		return false;
	}

	// Constraint: a tracked stack survives only while its form is still wanted and its live recipe matches; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	[[nodiscard]] inline bool KeepTrackedStack(bool a_wantedForm, bool a_stackFound, bool a_modsMatch)
	{
		return a_wantedForm && a_stackFound && a_modsMatch;
	}

	// Constraint: only a stack that is still there but could not be removed survives a failed removal; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	[[nodiscard]] inline bool RetryTrackedStack(StackRemoval a_removal)
	{
		return a_removal == StackRemoval::kBlocked;
	}

	// Constraint: a slot is re-equipped only when the base form differs or its live recipe does; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	[[nodiscard]] inline bool SlotNeedsWork(std::uint32_t a_currentFormId, std::uint32_t a_wantedFormId, bool a_modsMatch)
	{
		return a_currentFormId != a_wantedFormId || (a_currentFormId != 0 && !a_modsMatch);
	}
}
