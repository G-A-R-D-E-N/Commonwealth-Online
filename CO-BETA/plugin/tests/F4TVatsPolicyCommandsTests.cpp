#include "F4TVatsPolicyCommands.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{
	using F4T::WorldPolicy::Vats;
	using F4T::VatsPolicy::ApplyCommands;
	using F4T::VatsPolicy::FormatGsFloat;
	using F4T::VatsPolicy::RestoreCommands;
	using F4T::VatsPolicy::Snapshot;

	Snapshot SampleSnapshot()
	{
		return Snapshot{
			.maxEngageDistance = 4500.0F,
			.enterMult = 0.3F,
			.exitMult = 0.35F,
			.targetSelectMult = 0.25F,
			.magicSlowdownMult = 0.5F,
			.movementEvents = false,
			.valid = true,
		};
	}

	void TestDisabledBlocksVats()
	{
		const auto commands = ApplyCommands(Vats::Disabled, Snapshot{});
		assert(commands.size() == 1);
		assert(commands[0] == "setgs fVATSMaxEngageDistance 0");
	}

	void TestRealTimeRemovesSlowMotion()
	{
		const auto commands = ApplyCommands(Vats::RealTime, SampleSnapshot());
		assert(commands.size() == 6);
		assert(commands[0] == "setgs fVATSMaxEngageDistance " + FormatGsFloat(4500.0F));
		assert(commands[1] == "setgs fVATSTimeMultEnter 1");
		assert(commands[2] == "setgs fVATSTimeMultExit 1");
		assert(commands[3] == "setgs fVATSTimeMultTargetSelect 1");
		assert(commands[4] == "setgs fVATSPlayerMagicTimeSlowdownMult 1");
		assert(commands[5] == "setgs bVATSMovementEvents 1");
	}

	void TestRealTimeWithoutSnapshotUsesVanillaDistance()
	{
		const auto commands = ApplyCommands(Vats::RealTime, Snapshot{});
		assert(commands.size() == 6);
		assert(commands[0] == "setgs fVATSMaxEngageDistance 5000");
	}

	void TestVanillaRestoresDistanceOnly()
	{
		const auto commands = ApplyCommands(Vats::Vanilla, SampleSnapshot());
		assert(commands.size() == 1);
		assert(commands[0] == "setgs fVATSMaxEngageDistance " + FormatGsFloat(4500.0F));
	}

	void TestRestoreReturnsSnapshotValues()
	{
		const auto commands = RestoreCommands(SampleSnapshot());
		assert(commands.size() == 6);
		assert(commands[0] == "setgs fVATSMaxEngageDistance " + FormatGsFloat(4500.0F));
		assert(commands[1] == "setgs fVATSTimeMultEnter " + FormatGsFloat(0.3F));
		assert(commands[2] == "setgs fVATSTimeMultExit " + FormatGsFloat(0.35F));
		assert(commands[3] == "setgs fVATSTimeMultTargetSelect " + FormatGsFloat(0.25F));
		assert(commands[4] == "setgs fVATSPlayerMagicTimeSlowdownMult " + FormatGsFloat(0.5F));
		assert(commands[5] == "setgs bVATSMovementEvents 0");
	}

	void TestRestoreWithoutSnapshotUsesVanillaDistance()
	{
		const auto commands = RestoreCommands(Snapshot{});
		assert(commands.size() == 1);
		assert(commands[0] == "setgs fVATSMaxEngageDistance 5000");
	}

	void TestFloatFormattingRoundTrips()
	{
		assert(FormatGsFloat(1.0F) == "1");
		assert(!FormatGsFloat(0.3F).empty());
		assert(FormatGsFloat(0.3F) == FormatGsFloat(0.3F));
	}
}

int main()
{
	TestDisabledBlocksVats();
	TestRealTimeRemovesSlowMotion();
	TestRealTimeWithoutSnapshotUsesVanillaDistance();
	TestVanillaRestoresDistanceOnly();
	TestRestoreReturnsSnapshotValues();
	TestRestoreWithoutSnapshotUsesVanillaDistance();
	TestFloatFormattingRoundTrips();
	return 0;
}
