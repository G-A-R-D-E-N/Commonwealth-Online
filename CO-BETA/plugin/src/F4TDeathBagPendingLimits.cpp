#include "pch.h"

#include "F4TDeathBagPendingLimits.h"
#include "F4TDeathBagGrantFiles.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDeathBagPlaceRecovery.h"
#include <algorithm>
#include <vector>

namespace F4T::DeathBagPendingLimits
{
	void Prune(std::size_t a_room)
	{
		try {
			const auto now = F4T::DeathBagGrantFiles::NowSeconds();
			std::vector<F4T::DeathBagPlaceRecovery::PendingAge> ages;
			for (const auto& pending : F4T::DeathBagGrantLedger::PendingPlaces()) {
				ages.push_back({ pending.deathId, pending.createdAt });
			}
			const auto expired = F4T::DeathBagPlaceRecovery::Expired(ages, now);
			std::erase_if(ages, [&expired](const auto& a_age) {
				return std::find(expired.begin(), expired.end(), a_age.deathId) != expired.end();
			});
			const auto overflow = F4T::DeathBagPlaceRecovery::Overflow(ages, a_room);
			for (const auto& deathId : expired) {
				REX::WARN("Death bag placement record older than 48 hours was dropped.");
				F4T::DeathBagGrantLedger::ForgetPendingPlace(deathId);
			}
			for (const auto& deathId : overflow) {
				REX::WARN("Death bag placement records exceeded the limit of 8; the oldest record was dropped.");
				F4T::DeathBagGrantLedger::ForgetPendingPlace(deathId);
			}
		} catch (...) {
			REX::WARN("Death bag placement records could not be pruned; they are kept.");
		}
	}
}
