#include "PersistenceGate.h"
#include "ProcessResources.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace CommonwealthOnline::Server;
using CommonwealthOnline::Benchmarks::Resources;
using CommonwealthOnline::Benchmarks::ResourceDelta;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

int Number(const char* text, int minimum, int maximum)
{
    int value{};
    const std::string_view input(text);
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    Check(error == std::errc{} && end == input.data() + input.size() && value >= minimum && value <= maximum,
        "argument outside allowed range");
    return value;
}

double Timed(auto&& operation)
{
    const auto start = Clock::now();
    operation();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void WaitForCommitsBounded(DurableStateFileStore& store)
{
    auto pending = std::async(std::launch::async, [&] { store.WaitForCommits(); });
    if (pending.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        std::cerr << "timed out waiting for durable commits\n";
        std::_Exit(1);
    }
    pending.get();
}

Json Summary(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    auto percentile = [&](double fraction) { return values.at(static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1); };
    return { {"count", values.size()}, {"p50_ms", percentile(.50)}, {"p95_ms", percentile(.95)},
        {"p99_ms", percentile(.99)}, {"max_ms", values.back()} };
}

OutboundFrame Ready(std::uint32_t connection, const std::string& profile)
{
    const auto payload = Json{{"type", "sessionReady"}, {"playerId", connection}, {"profileId", profile}}.dump();
    return {connection, Delivery::ReliableOrdered, {payload.begin(), payload.end()}};
}

Json Run(const std::filesystem::path& directory, int players, int samples, int interval)
{
    Check(std::filesystem::create_directory(directory), "output directory must be new");
    const auto path = directory / "durable-state.json";
    const std::string instance = "0123456789abcdef0123456789abcdef";
    double now = 1000;
    std::atomic<std::size_t> commits{0};
    std::vector<std::string> profiles;
    std::vector<Json> states;
    Json result{{"profiles", players}, {"samples", samples}, {"interval_ms", interval}};
    {
        DurableStateFileStore store(path, [&] { return now; }, [&] { return instance; }, [&] { ++commits; });
        PersistenceGate gate(store);
        F4T::Identity::TokenHash hash{};
        hash.fill(1);
        for (int player = 0; player < players; ++player) {
            profiles.push_back("bench-profile-" + std::to_string(player));
            states.push_back(Json{{"type", "playerState"}, {"characterName", "Benchmark " + std::to_string(player)},
                {"appearance", Json{{"version", 1}}}});
            store.SaveResumeCredential(profiles.back(), hash, std::nullopt, true, 100000);
            Check(gate.SessionReadyDelivered(Ready(player + 1, profiles.back()), now).has_value(), "initial profile binding failed");
        }
        result["initial_bytes"] = std::filesystem::file_size(path);
        for (bool changed : {true, false}) {
            std::vector<double> batches, flushes, commitTimes;
            const auto before = commits.load();
            const auto resources = Resources();
            const auto start = Clock::now();
            for (int sample = 0; sample < samples; ++sample) {
                now += interval / 1000.0;
                if (changed) for (auto& state : states) state["appearance"]["version"] = 2 + sample % 2;
                double flush{};
                batches.push_back(Timed([&] {
                    for (int player = 0; player < players; ++player)
                        Check(gate.QueueAcceptedPlayerState(player + 1, states[player]), "queue rejected state");
                    flush = Timed([&] { Check(gate.FlushPendingPlayerStates() == changed, "unexpected flush result"); });
                }));
                flushes.push_back(flush);
                commitTimes.push_back(Timed([&] { WaitForCommitsBounded(store); }));
                std::this_thread::sleep_until(start + std::chrono::milliseconds((sample + 1) * interval));
            }
            const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            WaitForCommitsBounded(store);
            const auto used = ResourceDelta(resources);
            Check(commits.load() - before == (changed ? static_cast<std::size_t>(samples) : 0), "unexpected durable commit count");
            result[changed ? "changed" : "unchanged"] = Json{
                {"batch", Summary(batches)}, {"flush", Summary(flushes)}, {"batch_samples_ms", batches},
                {"flush_samples_ms", flushes}, {"commit", Summary(commitTimes)}, {"commit_samples_ms", commitTimes},
                {"commits", commits.load() - before}, {"elapsed_seconds", elapsed},
                {"updates_per_second", players * samples / elapsed},
                {"process_resources", used}};
        }
        const auto beforeWorld = commits.load();
        const auto worldResources = Resources();
        std::vector<double> world, reconnect;
        for (int sample = 0; sample < samples; ++sample)
            world.push_back(Timed([&] { store.UpdateServerWorldState(Json{{"timeHHmm", "1800"}, {"weatherFormId", "001CA7E4"}}); }));
        WaitForCommitsBounded(store);
        const auto worldUsed = ResourceDelta(worldResources);
        result["identical_world_burst"] = Json{{"write", Summary(world)}, {"samples_ms", world},
            {"commits", commits.load() - beforeWorld}, {"process_resources", worldUsed}};
        const auto rotationResources = Resources();
        hash.fill(2);
        for (int player = 0; player < players; ++player) {
            gate.ConnectionClosed(player + 1);
            Check(!gate.ProfileForConnection(player + 1), "closed connection remains bound");
            reconnect.push_back(Timed([&] { store.SaveResumeCredential(profiles[player], hash, std::nullopt, true, 100000); }));
            const auto replay = gate.SessionReadyDelivered(Ready(player + 1001, profiles[player]), now);
            Check(replay && replay->profileId == profiles[player] && replay->playerState == states[player], "reconnect replay mismatch");
        }
        WaitForCommitsBounded(store);
        const auto rotationUsed = ResourceDelta(rotationResources);
        result["credential_rotation"] = Json{{"write", Summary(reconnect)}, {"samples_ms", reconnect},
            {"process_resources", rotationUsed}};
        Check(!gate.FlushPendingPlayerStates(), "unexpected pending state");
        WaitForCommitsBounded(store);
        Check(!store.TakePersistenceWarning(), "persistence warning");
        result["final_bytes"] = std::filesystem::file_size(path);
        result["backup_bytes"] = std::filesystem::file_size(path.string() + ".bak");
    }
    for (bool backup : {false, true}) {
        double reopen{};
        reopen = Timed([&] {
            DurableStateFileStore store(backup ? std::filesystem::path(path.string() + ".bak") : path,
                [&] { return now; }, [&] { return instance; });
            Check(store.ServerInstanceId() == instance && !store.RecoveryNotice(), "restart identity or recovery mismatch");
            Check(store.GetServerWorldState().at("timeHHmm") == "1800", "restart world mismatch");
            for (int player = 0; player < players; ++player) {
                Check(store.GetPlayerState(profiles[player]) == states[player], "restart state mismatch");
                const auto credential = store.GetResumeCredential(profiles[player]);
                Check(credential && credential->expiresAt == 100000, "restart credential missing");
                F4T::Identity::TokenHash expected{};
                expected.fill(backup && player == players - 1 ? 1 : 2);
                Check(credential->tokenHash == expected, "restart credential mismatch");
            }
        });
        result[backup ? "backup_reopen_and_verify_ms" : "primary_reopen_and_verify_ms"] = reopen;
    }
    result["checks_passed"] = true;
    return result;
}
}

int main(int argc, char** argv)
{
    try {
        Check(argc == 5, "usage: PersistenceBenchmark NEW_DIRECTORY PLAYERS SAMPLES INTERVAL_MS");
        std::cout << Run(argv[1], Number(argv[2], 1, 256), Number(argv[3], 2, 10000), Number(argv[4], 0, 1000)).dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
