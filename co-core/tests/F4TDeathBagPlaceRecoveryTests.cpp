#include "F4TDeathBagPlaceRecovery.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using F4T::DeathBagPlaceRecovery::Holding;
using F4T::DeathBagPlaceRecovery::PendingAge;
using F4T::DeathBagPlaceRecovery::Resolution;

namespace
{
	nlohmann::json Stack(const std::string& a_itemKey, std::uint64_t a_count, const nlohmann::json& a_metadata = nullptr)
	{
		return nlohmann::json{ { "itemKey", a_itemKey }, { "count", a_count }, { "metadata", a_metadata } };
	}
}

int main()
{
	const std::string first = "fallout4.esm:00000002";
	const std::string second = "fallout4.esm:00000003";
	const auto persisted = nlohmann::json::array({ Stack(first, 2), Stack(second, 1) });
	const auto empty = nlohmann::json::array();

	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, persisted) == Holding::kFull);
	assert(F4T::DeathBagPlaceRecovery::Exact(persisted, persisted));
	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, nlohmann::json::array({ Stack(second, 1), Stack(first, 2) })) == Holding::kFull);
	assert(F4T::DeathBagPlaceRecovery::Exact(persisted, nlohmann::json::array({ Stack(second, 1), Stack(first, 2) })));

	const auto surplus = nlohmann::json::array({ Stack(first, 3), Stack(second, 1) });
	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, surplus) == Holding::kFull);
	assert(!F4T::DeathBagPlaceRecovery::Exact(persisted, surplus));

	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, empty) == Holding::kNone);
	assert(!F4T::DeathBagPlaceRecovery::Exact(persisted, empty));

	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, nlohmann::json::array({ Stack(first, 1) })) == Holding::kPartial);
	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, nlohmann::json::array({ Stack(first, 2) })) == Holding::kPartial);
	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, nlohmann::json(nullptr)) == Holding::kPartial);
	assert(!F4T::DeathBagPlaceRecovery::Exact(persisted, nlohmann::json(nullptr)));

	assert(F4T::DeathBagPlaceRecovery::Classify(empty, empty) == Holding::kPartial);
	assert(F4T::DeathBagPlaceRecovery::Classify(empty, persisted) == Holding::kPartial);
	assert(!F4T::DeathBagPlaceRecovery::Exact(empty, empty));
	assert(F4T::DeathBagPlaceRecovery::Classify(nlohmann::json(nullptr), persisted) == Holding::kPartial);

	const auto conditioned = nlohmann::json::array({ Stack(first, 2, nlohmann::json{ { "condition", 50 } }), Stack(second, 1) });
	assert(F4T::DeathBagPlaceRecovery::Classify(persisted, conditioned) == Holding::kFull);
	assert(!F4T::DeathBagPlaceRecovery::Exact(persisted, conditioned));
	assert(F4T::DeathBagPlaceRecovery::Exact(conditioned, conditioned));

	assert(F4T::DeathBagPlaceRecovery::Resolve("pending", Holding::kFull, true) == Resolution::kRecordRestoredFailure);
	assert(F4T::DeathBagPlaceRecovery::Resolve("pending", Holding::kFull, false) == Resolution::kRecordRestoredFailure);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placing", Holding::kFull, true) == Resolution::kReplace);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placing", Holding::kFull, false) == Resolution::kKeep);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placed", Holding::kFull, true) == Resolution::kReplace);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placed", Holding::kFull, false) == Resolution::kKeep);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placing", Holding::kNone, false) == Resolution::kResendPlaced);
	assert(F4T::DeathBagPlaceRecovery::Resolve("placed", Holding::kNone, false) == Resolution::kResendPlaced);
	assert(F4T::DeathBagPlaceRecovery::Resolve("pending", Holding::kNone, false) == Resolution::kResendUnplaced);
	assert(F4T::DeathBagPlaceRecovery::Resolve("unplaced", Holding::kNone, false) == Resolution::kResendUnplaced);
	for (const auto* phase : { "pending", "placing", "placed", "unplaced" }) {
		assert(F4T::DeathBagPlaceRecovery::Resolve(phase, Holding::kPartial, true) == Resolution::kKeep);
		assert(F4T::DeathBagPlaceRecovery::Resolve(phase, Holding::kPartial, false) == Resolution::kKeep);
	}
	assert(F4T::DeathBagPlaceRecovery::MayBePlaced("placing") && F4T::DeathBagPlaceRecovery::MayBePlaced("placed"));
	assert(!F4T::DeathBagPlaceRecovery::MayBePlaced("pending") && !F4T::DeathBagPlaceRecovery::MayBePlaced("unplaced"));

	constexpr std::int64_t origin = 1000000;
	const std::vector<PendingAge> ages{ { "old", origin }, { "young", origin + 1 }, { "boundary", origin + 172800 - 1 } };
	const auto expired = F4T::DeathBagPlaceRecovery::Expired(ages, origin + 172800);
	assert(expired.size() == 1 && expired.front() == "old");
	assert(F4T::DeathBagPlaceRecovery::Expired(ages, origin + 172799).empty());
	assert(F4T::DeathBagPlaceRecovery::MaximumPendingAgeSeconds == 172800);
	assert(F4T::DeathBagPlaceRecovery::MaximumPendingPlaces == 8);

	std::vector<PendingAge> full;
	for (std::int64_t index = 0; index < 8; ++index) full.push_back({ "id" + std::to_string(index), origin + index });
	assert(F4T::DeathBagPlaceRecovery::Overflow(full, 0).empty());
	assert(F4T::DeathBagPlaceRecovery::Overflow(full, 1) == std::vector<std::string>{ "id0" });
	full.push_back({ "id8", origin + 8 });
	assert(F4T::DeathBagPlaceRecovery::Overflow(full, 0) == std::vector<std::string>{ "id0" });
	assert(F4T::DeathBagPlaceRecovery::Overflow(full, 2) == (std::vector<std::string>{ "id0", "id1", "id2" }));
	const std::vector<PendingAge> tied{ { "b", origin }, { "a", origin }, { "c", origin + 5 } };
	assert(F4T::DeathBagPlaceRecovery::Overflow(tied, 0).empty());
	assert(F4T::DeathBagPlaceRecovery::Overflow(tied, 6) == std::vector<std::string>{ "a" });
	assert(F4T::DeathBagPlaceRecovery::Overflow(tied, 7) == (std::vector<std::string>{ "a", "b" }));
	assert(F4T::DeathBagPlaceRecovery::Overflow(tied, 8) == (std::vector<std::string>{ "a", "b", "c" }));
	return 0;
}
