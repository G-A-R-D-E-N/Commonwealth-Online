#include "F4TPlayerVitals.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>

int main()
{
	using namespace F4T::PlayerVitals;

	assert(ComputeHealthPercent(50.0F, 100.0F) == 50);
	assert(ComputeHealthPercent(100.0F, 100.0F) == 100);
	assert(ComputeHealthPercent(0.0F, 100.0F) == 0);
	assert(ComputeHealthPercent(0.4F, 100.0F) == 0);
	assert(ComputeHealthPercent(0.6F, 100.0F) == 1);
	assert(ComputeHealthPercent(-25.0F, 100.0F) == 0);
	assert(ComputeHealthPercent(500.0F, 100.0F) == 100);
	assert(ComputeHealthPercent(85.0F, 85.0F) == 100);
	assert(ComputeHealthPercent(1.0F, 3.0F) == 33);
	assert(!ComputeHealthPercent(50.0F, 0.0F).has_value());
	assert(!ComputeHealthPercent(50.0F, -1.0F).has_value());
	assert(!ComputeHealthPercent(std::numeric_limits<float>::quiet_NaN(), 100.0F).has_value());
	assert(!ComputeHealthPercent(50.0F, std::numeric_limits<float>::quiet_NaN()).has_value());
	assert(!ComputeHealthPercent(50.0F, std::numeric_limits<float>::infinity()).has_value());

	const Vitals full{ 87, false, false, 42 };
	const auto encodedFull = Encode(full);
	assert(encodedFull.has_value());
	assert(encodedFull->size() == 4);
	assert(encodedFull->at("healthPercent") == 87);
	assert(encodedFull->at("isDowned") == false);
	assert(encodedFull->at("isDead") == false);
	assert(encodedFull->at("level") == 42);
	assert(Decode(*encodedFull) == full);

	const Vitals minimal{ 0, true, true, std::nullopt };
	const auto encodedMinimal = Encode(minimal);
	assert(encodedMinimal.has_value());
	assert(encodedMinimal->size() == 3);
	assert(!encodedMinimal->contains("level"));
	assert(Decode(*encodedMinimal) == minimal);

	const Vitals noLevel{ 100, false, false, std::nullopt };
	const Vitals maxLevel{ 100, false, false, 65535 };
	const Vitals minLevel{ 100, false, false, 1 };
	const auto percentOnly = nlohmann::json{ { "healthPercent", 100 } };
	const auto percentMaxLevel = nlohmann::json{ { "healthPercent", 100 }, { "level", 65535 } };
	const auto percentMinLevel = nlohmann::json{ { "healthPercent", 100 }, { "level", 1 } };
	assert(Decode(percentOnly) == noLevel);
	assert(Decode(percentMaxLevel) == maxLevel);
	assert(Decode(percentMinLevel) == minLevel);

	const Vitals outOfRangeHigh{ 101, false, false, std::nullopt };
	const Vitals outOfRangeLow{ -1, false, false, std::nullopt };
	assert(!Encode(outOfRangeHigh).has_value());
	assert(!Encode(outOfRangeLow).has_value());

	const Vitals levelZero{ 50, false, false, 0 };
	const Vitals levelTooHigh{ 50, false, false, 65536 };
	const Vitals percentOnly50{ 50, false, false, std::nullopt };
	const auto encodedLevelZero = Encode(levelZero);
	assert(encodedLevelZero.has_value());
	assert(!encodedLevelZero->contains("level"));
	assert(Decode(*encodedLevelZero) == percentOnly50);
	const auto encodedLevelTooHigh = Encode(levelTooHigh);
	assert(encodedLevelTooHigh.has_value());
	assert(!encodedLevelTooHigh->contains("level"));
	assert(Decode(*encodedLevelTooHigh) == percentOnly50);

	assert(NormalizeLevel(1) == 1);
	assert(NormalizeLevel(42) == 42);
	assert(NormalizeLevel(65535) == 65535);
	assert(!NormalizeLevel(0).has_value());
	assert(!NormalizeLevel(-3).has_value());
	assert(!NormalizeLevel(65536).has_value());
	assert(IsLevelInRange(1));
	assert(IsLevelInRange(65535));
	assert(!IsLevelInRange(0));
	assert(!IsLevelInRange(65536));

	const auto noPercent = nlohmann::json{ { "isDead", true } };
	const auto percentOutOfRange = nlohmann::json{ { "healthPercent", 150 } };
	const auto percentAsText = nlohmann::json{ { "healthPercent", "50" } };
	const auto flagAsNumber = nlohmann::json{ { "healthPercent", 50 }, { "isDead", 1 } };
	const auto levelAsFloat = nlohmann::json{ { "healthPercent", 50 }, { "level", 1.5 } };
	assert(!Decode(nlohmann::json::array()).has_value());
	assert(!Decode(noPercent).has_value());
	assert(!Decode(percentOutOfRange).has_value());
	assert(!Decode(percentAsText).has_value());
	assert(!Decode(flagAsNumber).has_value());
	assert(!Decode(levelAsFloat).has_value());

	PublishState gate;
	const Vitals dropped{ 86, false, false, 42 };
	assert(ShouldPublish(gate, full, 0.0));
	assert(!ShouldPublish(gate, full, 0.1));
	assert(!ShouldPublish(gate, full, 0.4));
	assert(!ShouldPublish(gate, dropped, 0.4));
	assert(ShouldPublish(gate, dropped, 0.5));
	assert(!ShouldPublish(gate, dropped, 0.6));
	assert(!ShouldPublish(gate, full, 0.2));
	assert(!ShouldPublish(gate, full, std::numeric_limits<double>::quiet_NaN()));
	assert(ShouldPublish(gate, full, 1.0));
	assert(!ShouldPublish(gate, full, 1.4));
	assert(!ShouldPublish(gate, full, 1.9));
	assert(ShouldPublish(gate, full, 3.0));

	PublishState refresh;
	assert(ShouldPublish(refresh, full, 0.0));
	assert(!ShouldPublish(refresh, full, 1.9));
	assert(ShouldPublish(refresh, full, 2.0));
	assert(!ShouldPublish(refresh, full, 3.9));
	assert(ShouldPublish(refresh, full, 4.0));

	PublishState flags;
	const Vitals levelOne{ 50, false, false, 1 };
	const Vitals levelTwo{ 50, false, false, 2 };
	const Vitals downed{ 50, true, false, 2 };
	const Vitals dead{ 0, true, true, 2 };
	assert(ShouldPublish(flags, levelOne, 0.0));
	assert(ShouldPublish(flags, levelTwo, 0.5));
	assert(ShouldPublish(flags, downed, 1.0));
	assert(ShouldPublish(flags, dead, 1.5));
	assert(!ShouldPublish(flags, dead, 1.9));

	PublishState window;
	int published = 0;
	for (int step = 0; step <= 20; ++step) {
		const Vitals changing{ 100 - step, false, false, std::nullopt };
		if (ShouldPublish(window, changing, static_cast<double>(step) * 0.5)) {
			++published;
		}
	}
	assert(published == 21);

	PublishState burst;
	published = 0;
	for (int step = 0; step <= 20; ++step) {
		const Vitals changing{ 100 - step, false, false, std::nullopt };
		if (ShouldPublish(burst, changing, static_cast<double>(step) * 0.1)) {
			++published;
		}
	}
	assert(published == 5);

	return 0;
}
