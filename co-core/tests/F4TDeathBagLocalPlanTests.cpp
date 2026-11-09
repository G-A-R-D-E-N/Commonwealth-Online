#include "F4TDeathBagLocalPlan.h"
#include <cassert>
using namespace F4T::DeathBagLocal;
int main()
{
	assert(Enabled);
	assert(PipboyArmorItemKey == "fallout4.esm:00021B3B" && IsPipboyArmor(VanillaPipboyGlove) && !IsPipboyArmor(10));
	const auto plan = Plan({ { VanillaPipboyGlove, 0, 1 }, { 10, 0, 2 }, { 10, 1, 3 } });
	assert(plan && plan->size() == 2 && plan->at(0).count == 2 && plan->at(1).index == 1);
	const Counts before{ { VanillaPipboyGlove, 1 }, { 10, 5 } };
	const Counts after{ { VanillaPipboyGlove, 1 } };
	assert(MatchesSource({ { VanillaPipboyGlove, 0, 1 }, { 10, 0, 2 }, { 10, 1, 3 } }, before));
	assert(!MatchesSource({ { VanillaPipboyGlove, 0, 1 }, { 10, 0, 2 }, { 10, 0, 3 } }, before));
	assert(!MatchesSource({ { VanillaPipboyGlove, 0, 1 }, { 10, 0, 4 } }, before));
	assert(CountsMatch(before, after, Counts{ { 10, 5 } }, *plan));
	assert(!CountsMatch(before, after, Counts{ { 10, 4 } }, *plan));
	assert(!CountsMatch(before, Counts{}, Counts{ { 10, 5 } }, *plan));
	assert(!CountsMatch(Counts{}, Counts{}, Counts{ { 10, 5 } }, *plan));
	assert(!Plan({ { 0, 0, 1 } }));
	assert(!Plan({ { 1, 0, 0 } }));
	assert(!Plan({ { 1, 0, 2147483648u } }));
	assert(!Plan(std::vector<Stack>(4097, Stack{ 1, 0, 1 })));
	const auto questPlan = Plan({ { 10, 0, 2, true }, { 10, 1, 3 }, { VanillaPipboyGlove, 0, 1 } });
	assert(questPlan && questPlan->size() == 1 && questPlan->at(0).index == 1 && questPlan->at(0).count == 3);
	assert(IsRetained(Stack{ 10, 0, 2, true }) && IsRetained(Stack{ VanillaPipboyGlove, 0, 1 }) && !IsRetained(Stack{ 10, 1, 3 }));
	assert(IsPipboyArmor(PipboyDustyBase) && IsPipboyArmor(PipboyVault81Base) && !IsPipboyArmor(138044));
	const auto allPipboy = Plan({ { VanillaPipboyGlove, 0, 1 }, { PipboyDustyBase, 0, 1 }, { PipboyVault81Base, 0, 1 }, { 138044, 0, 4 } });
	assert(allPipboy && allPipboy->size() == 1 && allPipboy->at(0).base == 138044 && allPipboy->at(0).count == 4);
	assert(PipboyArmorKey(VanillaPipboyGlove) == PipboyArmorItemKey && PipboyArmorKey(PipboyVault81Base) == "fallout4.esm:001B9B87");
}
