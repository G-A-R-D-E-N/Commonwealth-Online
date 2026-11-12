#include <string_view>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include "F4TSurvivalPolicyClient.h"

namespace REX {
struct Version {
    int majorValue{1}, minorValue{10}, patchValue{163}, buildValue{0};
    int major() const { return majorValue; }
    int minor() const { return minorValue; }
    int patch() const { return patchValue; }
    int build() const { return buildValue; }
} version;
struct FModule {
    static FModule GetExecutingModule() { return {}; }
    Version GetFileVersion() const { return version; }
};
}
namespace RE {
enum class DifficultyLevel { kNormal = 2, kVeryHard = 4, kTrueSurvival = 6 };
struct PlayerCharacter {
    bool cell{true};
    DifficultyLevel difficulty{DifficultyLevel::kNormal};
    static PlayerCharacter* GetSingleton();
    bool GetParentCell() const { return cell; }
    DifficultyLevel GetDifficultyLevel() const { return difficulty; }
} player;
PlayerCharacter* currentPlayer = &player;
PlayerCharacter* PlayerCharacter::GetSingleton() { return currentPlayer; }
}
namespace REL {
    inline bool g_resolved{ true };
    inline void* g_objectValue{};
    inline int g_calls{};
    inline void* g_lastObject{};
    inline std::int32_t g_lastDifficulty{};
    inline bool g_lastForce{ true };
    inline bool g_lastApply{};
    inline bool g_applyOnTransition{};
    struct ID {
        std::uint64_t value;
        ID(std::uint64_t a_og, std::uint64_t, std::uint64_t) : value{ a_og } {}
        std::uint64_t address() const { return g_resolved ? value : std::uint64_t{}; }
    };
    template <class T> struct Relocation;
    template <> struct Relocation<void**> {
        explicit Relocation(const ID&) {}
        void*& operator*() const { return g_objectValue; }
    };
    template <> struct Relocation<void (*)(void*, std::int32_t, bool, bool)> {
        explicit Relocation(const ID&) {}
        void operator()(void* a_object, std::int32_t a_difficulty, bool a_force, bool a_apply) const
        {
            ++g_calls;
            g_lastObject = a_object;
            g_lastDifficulty = a_difficulty;
            g_lastForce = a_force;
            g_lastApply = a_apply;
            if (g_applyOnTransition) RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
        }
    };
}
namespace F4T::SurvivalEnforcer {
std::atomic<std::uint64_t> g_failureConnection{};
std::atomic<std::uint64_t> g_failureEpoch{};
int g_logged{};
void LogGateDecision(std::uint64_t, std::uint64_t, std::string_view, std::int32_t, std::int32_t) { ++g_logged; }
#include "survival-tick.h"
}
int main() {
    int work{};
    auto tick = [&] { return F4T::SurvivalEnforcer::Tick(1, 1, true, [&] { ++work; }); };
    auto& policy = F4T::SurvivalPolicy::g_clientState;
    auto reloadGame = [&] { assert(policy.CompleteLoad(policy.BeginLoad())); };
    policy.Welcome(1, true);
    assert(policy.CompleteLoad(policy.BeginLoad()));
    assert(!F4T::SurvivalEnforcer::Tick(1, 1, false, [&] { ++work; }) && work == 0);
    REX::version.majorValue = 2;
    assert(!tick() && work == 0);
    REX::version.majorValue = 1;
    REX::version.buildValue = 1;
    assert(!tick() && work == 0);
    REX::version.buildValue = 0;
    REX::version.patchValue = 999;
    assert(!tick() && work == 0);
    REX::version.patchValue = 163;
    F4T::SurvivalEnforcer::g_failureConnection = 0;
    F4T::SurvivalEnforcer::g_failureEpoch = 0;
    RE::currentPlayer = nullptr;
    assert(!tick() && work == 0 && F4T::SurvivalEnforcer::g_failureConnection == 0 && F4T::SurvivalEnforcer::g_failureEpoch == 0);
    RE::currentPlayer = &RE::player;
    RE::player.cell = false;
    assert(!tick() && work == 0 && F4T::SurvivalEnforcer::g_failureConnection == 0 && F4T::SurvivalEnforcer::g_failureEpoch == 0);
    RE::player.cell = true;
    assert(!tick() && work == 0 && F4T::SurvivalEnforcer::g_failureConnection == 0 && F4T::SurvivalEnforcer::g_failureEpoch == 0);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(tick() && work == 1);
    assert(tick() && work == 2);
    REX::version = {1, 10, 984, 0};
    assert(tick() && work == 3);
    REX::version = {1, 11, 240, 0};
    assert(tick() && work == 4);
    const auto load = policy.BeginLoad();
    assert(!tick() && !policy.ReadyRevision(1, 1) && work == 4);
    assert(policy.CompleteLoad(load));
    assert(tick() && work == 5);
    policy.Update(1, false);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(tick() && work == 6);
    policy.Update(1, true);
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    assert(!tick() && work == 6);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(tick() && work == 7);
    policy.Welcome(2, true);
    assert(!tick() && !policy.ReadyRevision(1, 1) && work == 7);
    assert(F4T::SurvivalEnforcer::Tick(2, 1, true, [&] { ++work; }) && work == 8);
    int marker{};
    auto enforce = [&](std::uint64_t a_connection, std::uint64_t a_load) { return F4T::SurvivalEnforcer::Tick(a_connection, a_load, true, [&] { ++work; }); };
    REL::g_objectValue = &marker;
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    assert(!enforce(2, 1) && work == 8 && REL::g_calls == 1 && REL::g_lastObject == &marker && REL::g_lastDifficulty == 4 && !REL::g_lastForce && REL::g_lastApply);
    assert(!enforce(2, 1) && work == 8 && REL::g_calls == 1);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(enforce(2, 1) && work == 9 && REL::g_calls == 1);
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    assert(!enforce(2, 1) && work == 9 && REL::g_calls == 2);
    REL::g_objectValue = nullptr;
    assert(!enforce(2, 2) && work == 9 && REL::g_calls == 2);
    reloadGame();
    REL::g_objectValue = &marker;
    assert(!enforce(2, 2) && work == 9 && REL::g_calls == 3);
    REL::g_resolved = false;
    reloadGame();
    F4T::SurvivalEnforcer::g_failureConnection = 0;
    assert(!enforce(2, 3) && work == 9 && REL::g_calls == 3 && F4T::SurvivalEnforcer::g_failureConnection == 2 && F4T::SurvivalEnforcer::g_failureEpoch == policy.LoadEpoch());
    REL::g_resolved = true;
    reloadGame();
    assert(!enforce(2, 3) && work == 9 && REL::g_calls == 4);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(enforce(2, 4) && work == 10 && REL::g_calls == 4);
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    REX::version = {1, 10, 984, 0};
    assert(!enforce(2, 5) && work == 10 && REL::g_calls == 5);
    reloadGame();
    REX::version = {1, 11, 240, 0};
    assert(!enforce(2, 6) && work == 10 && REL::g_calls == 6);
    REX::version = {1, 10, 999, 0};
    F4T::SurvivalEnforcer::g_failureConnection = 0;
    assert(!enforce(2, 7) && work == 10 && REL::g_calls == 6 && F4T::SurvivalEnforcer::g_failureConnection == 2 && F4T::SurvivalEnforcer::g_failureEpoch == policy.LoadEpoch());
    REX::version = {1, 11, 221, 0};
    F4T::SurvivalEnforcer::g_failureConnection = 0;
    assert(!enforce(2, 8) && work == 10 && REL::g_calls == 6 && F4T::SurvivalEnforcer::g_failureConnection == 2 && F4T::SurvivalEnforcer::g_failureEpoch == policy.LoadEpoch());
    REX::version = {1, 11, 240, 0};
    policy.Welcome(3, true);
    assert(!enforce(3, 1) && work == 10 && REL::g_calls == 7);
    policy.Update(3, false);
    assert(!enforce(3, 1) && work == 10 && REL::g_calls == 7);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(enforce(3, 1) && work == 11 && REL::g_calls == 7);
    policy.Update(3, true);
    const auto loading = policy.BeginLoad();
    assert(!enforce(3, 1) && work == 11 && REL::g_calls == 7);
    assert(policy.CompleteLoad(loading));
    policy.Welcome(4, true);
    RE::player.difficulty = RE::DifficultyLevel::kTrueSurvival;
    assert(!enforce(4, 1) && work == 11 && REL::g_calls == 8 && REL::g_lastDifficulty == 4);
    assert(F4T::SurvivalEnforcer::g_failureConnection == 4 && F4T::SurvivalEnforcer::g_failureEpoch == policy.LoadEpoch());
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    assert(!enforce(4, 1) && work == 11 && REL::g_calls == 8);
    assert(!enforce(4, 1) && work == 11 && REL::g_calls == 8);
    reloadGame();
    assert(!enforce(4, 1) && work == 11 && REL::g_calls == 9);
    REL::g_applyOnTransition = true;
    reloadGame();
    assert(enforce(4, 1) && work == 12 && REL::g_calls == 10 && RE::player.difficulty == RE::DifficultyLevel::kVeryHard);
    assert(F4T::SurvivalEnforcer::g_failureConnection == 0);
    policy.Welcome(5, false);
    RE::player.difficulty = RE::DifficultyLevel::kNormal;
    REL::g_applyOnTransition = false;
    assert(!enforce(5, 1) && work == 12 && REL::g_calls == 11 && REL::g_lastDifficulty == 4);
    RE::player.difficulty = RE::DifficultyLevel::kVeryHard;
    assert(enforce(5, 1) && work == 13 && REL::g_calls == 11);
    policy.Welcome(6, true);
    RE::player.difficulty = static_cast<RE::DifficultyLevel>(5);
    assert(!enforce(6, 1) && work == 13 && REL::g_calls == 12 && REL::g_lastDifficulty == 4);
    assert(F4T::SurvivalEnforcer::g_failureConnection == 6 && F4T::SurvivalEnforcer::g_failureEpoch == policy.LoadEpoch());
}
