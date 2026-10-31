#include "F4TPluginsTxt.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{
	using namespace F4T::PluginsTxt;

	void TestParse()
	{
		const auto lines = Parse("\xEF\xBB\xBF# header\r\n*A.esp\r\nB.esp\r\n\r\n*a.esp\r\n");
		assert(lines && lines->size() == 2);
		assert((*lines)[0].name == "A.esp" && (*lines)[0].active);
		assert((*lines)[1].name == "B.esp" && !(*lines)[1].active);
		assert(!Parse("*\r\n"));
		assert(!Parse(std::string("*A\x01.esp\r\n")));
		assert(!Parse(std::string(kMaximumFileBytes + 1, 'a')));
	}

	void TestActivationOrdersEnablesAndDisables()
	{
		const std::string current = "# This file is used by Fallout4 to keep track of your downloaded content.\r\n*Extra.esp\r\n*Unofficial Patch.esp\r\nDisabled.esp\r\n*ccBGSFO4001-PipBoy(Black).esl\r\n*Fallout4.esm\r\n";
		const std::vector<std::string> required{ "fallout4.esm", "dlccoast.esm", "ccbgsfo4001-pipboy(black).esl", "unofficial patch.esp", "mymod.esp", "disabled.esp" };
		const auto activation = Activate(current, required, { "ccBGSFO4001-PipBoy(Black).esl" }, { "MyMod.esp", "Unofficial Patch.esp", "Disabled.esp" });
		assert(activation);
		assert(activation->text ==
			"# This file is used by Fallout4 to keep track of your downloaded content.\r\n"
			"*Unofficial Patch.esp\r\n"
			"*MyMod.esp\r\n"
			"*Disabled.esp\r\n"
			"Extra.esp\r\n");
		assert((activation->activated == std::vector<std::string>{ "MyMod.esp", "Disabled.esp" }));
		assert((activation->deactivated == std::vector<std::string>{ "Extra.esp" }));
	}

	void TestReorderIsDetected()
	{
		const auto activation = Activate("*B.esp\r\n*A.esp\r\n", { "a.esp", "b.esp" }, {}, { "A.esp", "B.esp" });
		assert(activation && activation->reordered && activation->activated.empty() && activation->deactivated.empty());
		assert(activation->text == "*A.esp\r\n*B.esp\r\n");
		const auto same = Activate("*A.esp\r\n*B.esp\r\n", { "a.esp", "b.esp" }, {}, {});
		assert(same && !same->reordered);
	}

	void TestMissingFileAndBadNames()
	{
		const auto fresh = Activate("", { "Fallout4.esm", "New.esp" }, {}, {});
		assert(fresh && fresh->text == "*New.esp\r\n");
		assert(!Activate("", { "*Evil.esp" }, {}, {}));
		assert(!Activate("", { std::string("A\n*B.esp") }, {}, {}));
		assert(IsImplicitlyLoaded("DLCNUKAWORLD.ESM", {}));
		assert(!IsImplicitlyLoaded("DLCNukaWorldPatch.esp", {}));
	}
}

int main()
{
	TestParse();
	TestActivationOrdersEnablesAndDisables();
	TestReorderIsDetected();
	TestMissingFileAndBadNames();
	return 0;
}
