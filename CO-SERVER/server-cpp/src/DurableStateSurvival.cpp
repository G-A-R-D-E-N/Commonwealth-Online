#include "DurableStateFileStore.h"
#include "DurableStateSections.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using DurableStateSections::kRecentInventoryDropWindowSeconds;

		std::vector<RecentInventoryDrop>::iterator FindRecentDrop(std::vector<RecentInventoryDrop>& a_drops, std::string_view a_itemKey, double a_now)
		{
			return std::find_if(a_drops.begin(), a_drops.end(), [&](const RecentInventoryDrop& a_drop) {
				return a_drop.itemKey == a_itemKey && a_now >= a_drop.at && a_now - a_drop.at <= kRecentInventoryDropWindowSeconds;
			});
		}

	}

	F4T::SurvivalNeeds::Record DurableStateFileStore::GetSurvivalNeeds(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::DurableState::GetSurvivalNeeds(_root, a_profileId);
	}

	F4T::SurvivalNeeds::Admission DurableStateFileStore::BeginSurvivalClaim(std::string_view a_profileId, F4T::SurvivalNeeds::Claim a_claim)
	{
		const std::scoped_lock lock(_gate);
		auto admission = F4T::SurvivalNeeds::Admission::Missing;
		const auto now = _clock();
		const auto recent = _recentInventoryDrops.find(std::string{ a_profileId });
		const auto drop = recent == _recentInventoryDrops.end() ? std::vector<RecentInventoryDrop>::iterator{} : FindRecentDrop(recent->second, a_claim.itemKey, now);
		const bool credited = recent != _recentInventoryDrops.end() && drop != recent->second.end();
		const auto committed = MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			admission = F4T::DurableState::BeginSurvivalClaim(a_root, a_profileId, std::move(a_claim), credited ? 1u : 0u, now);
			return admission == F4T::SurvivalNeeds::Admission::Started;
		}, true);
		if (admission == F4T::SurvivalNeeds::Admission::Started && !committed) {
			return F4T::SurvivalNeeds::Admission::Unavailable;
		}
		if (credited && admission == F4T::SurvivalNeeds::Admission::Started && --drop->count == 0) {
			recent->second.erase(drop);
			if (recent->second.empty()) _recentInventoryDrops.erase(recent);
		}
		return admission;
	}

	bool DurableStateFileStore::MutateSurvivalNeeds(const std::vector<std::string>& a_profileIds,
		const std::function<bool(std::string_view, F4T::SurvivalNeeds::Record&)>& a_mutate, bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked(a_profileIds, {}, [&](nlohmann::json& a_root) {
			bool changed = false;
			for (const auto& profileId : a_profileIds) {
				auto record = F4T::DurableState::GetSurvivalNeeds(a_root, profileId);
				if (!a_mutate(profileId, record)) {
					continue;
				}
				F4T::DurableState::PutSurvivalNeeds(a_root, profileId, std::move(record), _clock());
				changed = true;
			}
			return changed;
		}, a_awaitCommit);
	}

	F4T::SurvivalNeeds::Settled DurableStateFileStore::SettleSurvivalNeeds(std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		F4T::SurvivalNeeds::Settled settled;
		const auto committed = MutateProfilesAndPersistLocked({ std::string{ a_profileId } }, {}, [&](nlohmann::json& a_root) {
			settled = F4T::DurableState::SettleSurvivalNeeds(a_root, a_profileId, _clock());
			return !settled.empty();
		}, true);
		return committed ? settled : F4T::SurvivalNeeds::Settled{};
	}

}
