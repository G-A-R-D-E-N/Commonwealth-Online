#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::SurvivalNeeds
{
    inline constexpr double kGameSecondsPerRealSecond = 900.0;
    inline constexpr double kAwakeGameSecondsPerRealSecond = 20.0;
    inline constexpr double kMaximumSleepGameSeconds = 24.0 * 3600.0;
    inline constexpr double kProofDeadlineSeconds = 30.0;
    inline constexpr std::size_t kMaximumConsumedOperations = 64;
    inline constexpr std::size_t kMaximumOutstandingClaims = 4;

    struct State
    {
        double hunger{};
        double thirst{};
        double sleep{};
    };

    struct Rates
    {
        double hungerPerSecond{};
        double thirstPerSecond{};
        double sleepPerSecond{};
        double restPerSecond{};
    };

    inline constexpr Rates kDefaultRates{ 1.0 / 86400.0, 1.0 / 64800.0, 1.0 / 57600.0, 1.0 / 28800.0 };

    enum class Status : std::uint8_t
    {
        Applied,
        Refused
    };

    enum class Admission : std::uint8_t
    {
        Started,
        Pending,
        Replayed,
        Busy,
        Missing,
        Unavailable
    };

    struct Claim
    {
        std::string operationId;
        std::string kind;
        std::string itemKey;
        std::uint32_t baseCount{};
        double deadline{};
    };

    struct Record
    {
        State state{};
        std::uint64_t revision{};
        std::vector<Claim> claims;
        std::vector<std::pair<std::string, Status>> consumed;
    };

    using Settled = std::vector<std::pair<std::string, Status>>;
    using SettleCounts = std::map<std::string, std::optional<std::uint32_t>>;

    inline bool Valid(const State& a_state) noexcept
    {
        const auto valid = [](double a_value) { return std::isfinite(a_value) && a_value >= 0.0 && a_value <= 1.0; };
        return valid(a_state.hunger) && valid(a_state.thirst) && valid(a_state.sleep);
    }

    inline std::optional<State> Advance(State a_state, double a_elapsedSeconds, const Rates& a_rates) noexcept
    {
        const auto validRate = [](double a_value) { return std::isfinite(a_value) && a_value >= 0.0; };
        if (!Valid(a_state) || !validRate(a_elapsedSeconds) || !validRate(a_rates.hungerPerSecond) ||
            !validRate(a_rates.thirstPerSecond) || !validRate(a_rates.sleepPerSecond)) return std::nullopt;
        a_state.hunger = std::min(1.0, a_state.hunger + a_elapsedSeconds * a_rates.hungerPerSecond);
        a_state.thirst = std::min(1.0, a_state.thirst + a_elapsedSeconds * a_rates.thirstPerSecond);
        a_state.sleep = std::min(1.0, a_state.sleep + a_elapsedSeconds * a_rates.sleepPerSecond);
        return a_state;
    }

    inline std::optional<State> Sleep(State a_state, double a_sleptGameSeconds, const Rates& a_rates) noexcept
    {
        if (!Valid(a_state) || !std::isfinite(a_sleptGameSeconds) || a_sleptGameSeconds < 0.0 ||
            !std::isfinite(a_rates.hungerPerSecond) || !std::isfinite(a_rates.thirstPerSecond) || !std::isfinite(a_rates.restPerSecond) ||
            a_rates.hungerPerSecond < 0.0 || a_rates.thirstPerSecond < 0.0 || a_rates.restPerSecond < 0.0) return std::nullopt;
        const double awakeEquivalentSeconds = a_sleptGameSeconds / kAwakeGameSecondsPerRealSecond;
        a_state.hunger = std::min(1.0, a_state.hunger + awakeEquivalentSeconds * a_rates.hungerPerSecond);
        a_state.thirst = std::min(1.0, a_state.thirst + awakeEquivalentSeconds * a_rates.thirstPerSecond);
        a_state.sleep = std::max(0.0, a_state.sleep - a_sleptGameSeconds * a_rates.restPerSecond);
        return a_state;
    }

    inline std::optional<State> Relieve(State a_state, const State& a_relief) noexcept
    {
        if (!Valid(a_state) || !Valid(a_relief)) return std::nullopt;
        a_state.hunger = std::max(0.0, a_state.hunger - a_relief.hunger);
        a_state.thirst = std::max(0.0, a_state.thirst - a_relief.thirst);
        a_state.sleep = std::max(0.0, a_state.sleep - a_relief.sleep);
        return a_state;
    }

    inline std::optional<State> ReliefFor(std::string_view a_kind) noexcept
    {
        if (a_kind == "food") return State{ 0.4, 0.0, 0.0 };
        if (a_kind == "drink") return State{ 0.0, 0.4, 0.0 };
        return std::nullopt;
    }

    inline std::optional<double> SleepCredit(double a_realSeconds) noexcept
    {
        if (!std::isfinite(a_realSeconds) || a_realSeconds < 0.0) return std::nullopt;
        return std::min(kMaximumSleepGameSeconds, a_realSeconds * kGameSecondsPerRealSecond);
    }

    inline std::optional<Status> FindConsumed(const Record& a_record, std::string_view a_operationId)
    {
        for (const auto& [operationId, status] : a_record.consumed) {
            if (operationId == a_operationId) return status;
        }
        return std::nullopt;
    }

    inline void RememberConsumed(Record& a_record, std::string a_operationId, Status a_status)
    {
        a_record.consumed.emplace_back(std::move(a_operationId), a_status);
        if (a_record.consumed.size() > kMaximumConsumedOperations) {
            a_record.consumed.erase(a_record.consumed.begin());
        }
    }

    inline Settled Settle(Record& a_record, const SettleCounts& a_counts, double a_now)
    {
        std::vector<bool> proven(a_record.claims.size(), false);
        std::map<std::string, std::uint64_t> matched;
        for (std::size_t index = a_record.claims.size(); index-- > 0;) {
            const Claim& claim = a_record.claims[index];
            const auto counted = a_counts.find(claim.itemKey);
            if (a_now >= claim.deadline || counted == a_counts.end() || !counted->second) continue;
            const std::uint64_t drops = *counted->second < claim.baseCount ? claim.baseCount - *counted->second : 0;
            auto& taken = matched[claim.itemKey];
            if (taken < drops) {
                ++taken;
                proven[index] = true;
            }
        }
        Settled settled;
        std::vector<Claim> remaining;
        for (std::size_t index = 0; index < a_record.claims.size(); ++index) {
            const Claim claim = a_record.claims[index];
            if (!proven[index] && a_now < claim.deadline) {
                remaining.push_back(claim);
                continue;
            }
            const auto relief = proven[index] ? ReliefFor(claim.kind) : std::nullopt;
            const auto next = relief ? Relieve(a_record.state, *relief) : std::nullopt;
            if (next) a_record.state = *next;
            const auto status = next ? Status::Applied : Status::Refused;
            RememberConsumed(a_record, claim.operationId, status);
            settled.emplace_back(claim.operationId, status);
        }
        a_record.claims = std::move(remaining);
        return settled;
    }
}
