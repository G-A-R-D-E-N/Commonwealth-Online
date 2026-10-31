#include "F4TWorldPolicyClient.h"

#include <algorithm>

namespace F4T::WorldPolicyClient
{
	namespace
	{
		using F4T::WorldPolicy::Policy;

		bool SuppressesQuests(const Policy& a_policy) noexcept
		{
			return a_policy.suppressQuests;
		}
	}

	bool IsStoryQuestType(std::int32_t a_questType) noexcept
	{
		switch (a_questType) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 7:
		case 8:
		case 9:
		case 10:
			return true;
		default:
			return false;
		}
	}

	bool IsRemovalTarget(const Policy& a_policy, RefClass a_class, bool a_baseListed, bool a_refListed) noexcept
	{
		if (a_class == RefClass::Player || a_class == RefClass::ProxyActor || a_class == RefClass::VendorActor) {
			return false;
		}

		if (a_policy.disable.empty()) {
			return false;
		}

		return a_baseListed || a_refListed;
	}

	RefAction DecideRefAction(RefState a_state) noexcept
	{
		return a_state == RefState::Live ? RefAction::Disable : RefAction::Skip;
	}

	QuestAction DecideForQuest(const Policy& a_policy, bool a_running, std::int32_t a_questType) noexcept
	{
		if (!a_running || !SuppressesQuests(a_policy)) {
			return QuestAction::Keep;
		}

		return IsStoryQuestType(a_questType) ? QuestAction::Stop : QuestAction::Keep;
	}

	bool InboundPolicyState::Store(const Policy& a_policy, std::uint64_t a_generation) noexcept
	{
		const bool sameSession = stored_ && generation_ == a_generation && a_generation != kNoSessionGeneration && !rebound_;
		if (sameSession && a_policy.version <= policy_.version) {
			return false;
		}

		policy_ = a_policy;
		generation_ = a_generation;
		stored_ = true;
		dirty_ = true;
		rebound_ = false;
		return true;
	}

	void InboundPolicyState::OnSessionGeneration(std::uint64_t a_generation) noexcept
	{
		const bool fromEarlierSession =
			stored_ && generation_ != kNoSessionGeneration && generation_ != a_generation;
		if (a_generation == kNoSessionGeneration || fromEarlierSession) {
			stored_ = false;
			dirty_ = false;
			rebound_ = false;
		} else if (stored_ && generation_ == kNoSessionGeneration) {
			rebound_ = true;
		}
		generation_ = a_generation;
	}

	bool CanTrackDisabledRef(std::size_t a_tracked, std::size_t a_capacity, bool a_alreadyTracked) noexcept
	{
		return a_alreadyTracked || a_tracked < a_capacity;
	}

	bool InboundPolicyState::TakeAdoptable(std::uint64_t a_generation, Policy& a_out) noexcept
	{
		if (stored_ && generation_ == kNoSessionGeneration && a_generation != kNoSessionGeneration) {
			generation_ = a_generation;
			rebound_ = true;
		}

		if (a_generation == kNoSessionGeneration || !stored_ || !dirty_ || generation_ != a_generation) {
			return false;
		}

		a_out = policy_;
		dirty_ = false;
		return true;
	}

	void PendingCellQueue::Push(std::uint32_t a_cellFormId) noexcept
	{
		if (a_cellFormId == 0 || cells_.size() >= kMaximumPendingCells || Contains(a_cellFormId)) {
			return;
		}

		cells_.push_back(a_cellFormId);
	}

	void PendingCellQueue::Clear() noexcept
	{
		cells_.clear();
	}

	std::vector<std::uint32_t> PendingCellQueue::Drain()
	{
		std::vector<std::uint32_t> drained;
		drained.swap(cells_);
		return drained;
	}

	bool PendingCellQueue::Contains(std::uint32_t a_cellFormId) const noexcept
	{
		return std::find(cells_.begin(), cells_.end(), a_cellFormId) != cells_.end();
	}

	std::size_t PendingCellQueue::Size() const noexcept
	{
		return cells_.size();
	}
}
