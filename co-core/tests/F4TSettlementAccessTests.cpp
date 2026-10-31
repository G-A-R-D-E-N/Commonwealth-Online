#include "F4TSettlementAccess.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

int main()
{
	using namespace F4T::SettlementAccess;

	assert(ValidProfileId("profile-owner"));
	assert(ValidProfileId("abcdefgh"));
	assert(!ValidProfileId(""));
	assert(!ValidProfileId("abcdefg"));
	assert(!ValidProfileId(" profile-owner"));
	assert(!ValidProfileId("profile owner"));

	{
		Table table;
		assert(table.CanBuild(9, "profile-anyone"));
		assert(table.SetOwner(0, "profile-owner") == ApplyStatus::InvalidRequest);
		assert(table.SetOwner(9, "bad id") == ApplyStatus::InvalidRequest);
		assert(table.SetOwner(9, "profile-owner") == ApplyStatus::Committed);
		assert(table.CanBuild(9, "profile-owner"));
		assert(!table.CanBuild(9, "profile-guest"));

		assert(table.Grant(9, "profile-guest", "profile-friend") == ApplyStatus::NotOwner);
		assert(table.Grant(9, "profile-owner", "profile-owner") == ApplyStatus::InvalidRequest);
		assert(table.Grant(9, "profile-owner", "bad id") == ApplyStatus::InvalidRequest);
		assert(table.Grant(9, "profile-owner", "profile-friend") == ApplyStatus::Committed);
		assert(table.CanBuild(9, "profile-friend"));
		assert(table.Grant(9, "profile-owner", "profile-friend") == ApplyStatus::AlreadyAllowed);

		assert(table.Revoke(9, "profile-friend", "profile-friend") == ApplyStatus::NotOwner);
		assert(table.Revoke(9, "profile-owner", "profile-stranger") == ApplyStatus::NotAllowed);
		assert(table.Revoke(9, "profile-owner", "profile-friend") == ApplyStatus::Committed);
		assert(!table.CanBuild(9, "profile-friend"));

		assert(table.Grant(10, "profile-owner", "profile-friend") == ApplyStatus::NotOwner);
		assert(table.Grant(9, "profile-owner", "profile-friend") == ApplyStatus::Committed);
		assert(table.SetOwner(9, "profile-new-owner") == ApplyStatus::Committed);
		assert(table.CanBuild(9, "profile-new-owner"));
		assert(!table.CanBuild(9, "profile-friend"));

		assert(table.SetOwner(9, "") == ApplyStatus::Committed);
		assert(!table.Find(9).has_value());
		assert(table.CanBuild(9, "profile-friend"));
	}

	{
		Table table;
		assert(table.SetOwner(1, "profile-owner") == ApplyStatus::Committed);
		for (std::size_t index = 0; index < MaximumAllowedProfiles; ++index) {
			assert(table.Grant(1, "profile-owner", "profile-guest-" + std::to_string(index)) == ApplyStatus::Committed);
		}
		assert(table.Grant(1, "profile-owner", "profile-guest-late") == ApplyStatus::Full);
		assert(table.Find(1)->allowedProfileIds.size() == MaximumAllowedProfiles);
	}

	{
		Table table;
		for (F4T::Workshop::BuildZoneId zone = 1; zone <= MaximumSettlements; ++zone) {
			assert(table.SetOwner(zone, "profile-owner") == ApplyStatus::Committed);
		}
		assert(table.SetOwner(MaximumSettlements + 1, "profile-owner") == ApplyStatus::SettlementLimit);
		assert(table.SetOwner(1, "profile-owner") == ApplyStatus::Committed);
		assert(table.SetOwner(1, "") == ApplyStatus::Committed);
		assert(table.SetOwner(MaximumSettlements + 1, "profile-owner") == ApplyStatus::Committed);
	}

	{
		Table table;
		assert(table.SetOwner(4, "profile-owner") == ApplyStatus::Committed);
		assert(table.Grant(4, "profile-owner", "profile-friend") == ApplyStatus::Committed);
		const auto snapshot = table.Snapshot();
		assert(snapshot.size() == 1);
		assert(snapshot.front().buildZoneId == 4);
		assert(snapshot.front().ownerProfileId == "profile-owner");
		assert(snapshot.front().allowedProfileIds == std::vector<std::string>{ "profile-friend" });

		assert(!table.Restore({ Settlement{ 5, "profile-owner", {} }, Settlement{ 5, "profile-other", {} } }));
		assert(!table.Restore({ Settlement{ 6, "profile-owner", { "profile-owner" } } }));
		assert(!table.Restore({ Settlement{ 6, "bad id", {} } }));
		assert(!table.Restore({ Settlement{ 0, "profile-owner", {} } }));

		std::vector<Settlement> tooMany{ Settlement{ 7, "profile-owner", {} } };
		for (std::size_t index = 0; index <= MaximumAllowedProfiles; ++index) {
			tooMany.front().allowedProfileIds.push_back("profile-guest-" + std::to_string(index));
		}
		assert(!table.Restore(tooMany));
		assert(table.Find(4).has_value());
		assert(!table.Find(5).has_value());

		assert(table.Restore({ Settlement{ 7, "profile-owner", { "profile-friend" } } }));
		assert(!table.Find(4).has_value());
		assert(table.CanBuild(7, "profile-friend"));
		assert(!table.CanBuild(7, "profile-guest"));
		assert(table.Restore(table.Snapshot()));
		assert(table.Snapshot().size() == 1);
		assert(table.Restore({}));
		assert(table.CanBuild(7, "profile-guest"));
	}
	return 0;
}
