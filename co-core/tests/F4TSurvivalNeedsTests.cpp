#include "F4TSurvivalNeeds.h"

#include <cassert>
#include <limits>

int main()
{
    using namespace F4T::SurvivalNeeds;
    const Rates rates{ 0.1, 0.2, 0.05, 0.5 };
    const auto advanced = Advance(State{}, 2.0, rates);
    assert(advanced && advanced->hunger == 0.2 && advanced->thirst == 0.4 && advanced->sleep == 0.1);
    const auto capped = Advance(*advanced, 100.0, rates);
    assert(capped && capped->hunger == 1.0 && capped->thirst == 1.0 && capped->sleep == 1.0);
    assert(!Advance(State{}, -1.0, rates));
    assert(!Advance(State{}, std::numeric_limits<double>::infinity(), rates));
    assert(!Advance(State{}, 1.0, Rates{ -1.0, 0.0, 0.0, 0.0 }));
    assert(!Advance(State{ 0.0, std::numeric_limits<double>::infinity(), 0.0 }, 1.0, rates));
    assert(!Advance(State{}, 1.0, Rates{ 0.0, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }));

    const auto slept = Sleep(State{ 0.0, 0.0, 0.5 }, kAwakeGameSecondsPerRealSecond, rates);
    assert(slept && slept->hunger == 0.1 && slept->thirst == 0.2 && slept->sleep == 0.0);
    const auto awake = Advance(State{}, 1.0, kDefaultRates);
    const auto asleep = Sleep(State{}, kAwakeGameSecondsPerRealSecond, kDefaultRates);
    assert(awake && asleep && awake->hunger == asleep->hunger && awake->thirst == asleep->thirst);
    assert(!Sleep(State{}, -1.0, rates));
    assert(!Sleep(State{}, 1.0, Rates{ 0.1, 0.1, 0.1, -1.0 }));

    const auto relieved = Relieve(State{ 0.5, 0.1, 0.0 }, State{ 0.4, 0.4, 0.0 });
    assert(relieved && relieved->hunger > 0.09 && relieved->hunger < 0.11 && relieved->thirst == 0.0);
    assert(!Relieve(State{}, State{ std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }));
    assert(!Relieve(State{ 2.0, 0.0, 0.0 }, State{}));

    assert(ReliefFor("food") && ReliefFor("food")->hunger > 0.0 && ReliefFor("food")->thirst == 0.0);
    assert(ReliefFor("drink") && ReliefFor("drink")->thirst > 0.0 && ReliefFor("drink")->hunger == 0.0);
    assert(!ReliefFor("stimpak"));

    assert(SleepCredit(1.0) && *SleepCredit(1.0) == 900.0);
    assert(SleepCredit(1000.0) && *SleepCredit(1000.0) == kMaximumSleepGameSeconds);
    assert(!SleepCredit(-1.0) && !SleepCredit(std::numeric_limits<double>::infinity()));

    Record record;
    record.state = State{ 0.5, 0.5, 0.5 };
    record.claims = { Claim{ "op-1", "food", "fo4:00000801", 3, 100.0 } };
    const auto unproven = Settle(record, SettleCounts{ { "fo4:00000801", std::nullopt } }, 50.0);
    const auto notDecreased = Settle(record, SettleCounts{ { "fo4:00000801", 3u } }, 50.0);
    assert(unproven.empty() && notDecreased.empty() && record.claims.size() == 1);
    const auto applied = Settle(record, SettleCounts{ { "fo4:00000801", 2u } }, 50.0);
    assert(applied.size() == 1 && applied.front().first == "op-1" && applied.front().second == Status::Applied && record.claims.empty());
    assert(record.state.hunger > 0.09 && record.state.hunger < 0.11 && record.state.thirst == 0.5);
    assert(FindConsumed(record, "op-1") && *FindConsumed(record, "op-1") == Status::Applied);
    assert(!FindConsumed(record, "op-2"));

    Record late;
    late.state = State{ 0.5, 0.5, 0.5 };
    late.claims = { Claim{ "op-late", "drink", "fo4:00000802", 2, 100.0 } };
    const auto refused = Settle(late, SettleCounts{ { "fo4:00000802", 1u } }, 100.0);
    assert(refused.size() == 1 && refused.front().second == Status::Refused && late.claims.empty() && late.state.thirst == 0.5);
    assert(FindConsumed(late, "op-late") && *FindConsumed(late, "op-late") == Status::Refused);

    Record unknownKind;
    unknownKind.claims = { Claim{ "op-kind", "stimpak", "fo4:00000803", 2, 100.0 } };
    const auto unknownResult = Settle(unknownKind, SettleCounts{ { "fo4:00000803", 1u } }, 10.0);
    assert(unknownResult.size() == 1 && unknownResult.front().second == Status::Refused && unknownKind.state.hunger == 0.0);

    Record sameItem;
    sameItem.state = State{ 0.9, 0.0, 0.0 };
    sameItem.claims = { Claim{ "op-a", "food", "fo4:00000804", 5, 100.0 }, Claim{ "op-b", "food", "fo4:00000804", 5, 100.0 } };
    const auto oneDrop = Settle(sameItem, SettleCounts{ { "fo4:00000804", 4u } }, 50.0);
    assert(oneDrop.size() == 1 && oneDrop.front().first == "op-b" && oneDrop.front().second == Status::Applied);
    assert(sameItem.claims.size() == 1 && sameItem.claims.front().operationId == "op-a");
    assert(sameItem.state.hunger > 0.49 && sameItem.state.hunger < 0.51);
    const auto twoDrops = Settle(sameItem, SettleCounts{ { "fo4:00000804", 3u } }, 50.0);
    assert(twoDrops.size() == 1 && twoDrops.front().first == "op-a" && twoDrops.front().second == Status::Applied);
    assert(sameItem.claims.empty() && sameItem.state.hunger > 0.09 && sameItem.state.hunger < 0.11);

    Record staggered;
    staggered.state = State{ 0.9, 0.0, 0.0 };
    staggered.claims = { Claim{ "op-c", "food", "fo4:00000805", 5, 100.0 }, Claim{ "op-d", "food", "fo4:00000805", 4, 100.0 } };
    const auto bothProven = Settle(staggered, SettleCounts{ { "fo4:00000805", 3u } }, 50.0);
    assert(bothProven.size() == 2 && staggered.claims.empty() && staggered.state.hunger < 0.11);

    Record bounded;
    for (std::size_t index = 0; index < kMaximumConsumedOperations + 5; ++index) {
        RememberConsumed(bounded, "op-" + std::to_string(index), Status::Applied);
    }
    assert(bounded.consumed.size() == kMaximumConsumedOperations && !FindConsumed(bounded, "op-0") &&
        FindConsumed(bounded, "op-" + std::to_string(kMaximumConsumedOperations + 4)));
    return 0;
}
