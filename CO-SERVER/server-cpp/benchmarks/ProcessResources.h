#pragma once

#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/resource.h>
#endif

namespace CommonwealthOnline::Benchmarks
{
inline nlohmann::json Resources()
{
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    IO_COUNTERS io{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0)
        throw std::runtime_error("CPU counters unavailable");
    if (GetProcessIoCounters(GetCurrentProcess(), &io) == 0)
        throw std::runtime_error("I/O counters unavailable");
    auto milliseconds = [](FILETIME time) {
        return ((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10000.0;
    };
    return {{"cpu_ms", milliseconds(kernel) + milliseconds(user)},
        {"io_read_bytes", io.ReadTransferCount}, {"io_write_bytes", io.WriteTransferCount},
        {"io_read_operations", io.ReadOperationCount}, {"io_write_operations", io.WriteOperationCount},
        {"io_other_bytes", io.OtherTransferCount}, {"io_other_operations", io.OtherOperationCount}};
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("process counters unavailable");
    auto milliseconds = [](timeval time) { return time.tv_sec * 1000.0 + time.tv_usec / 1000.0; };
    return {{"cpu_ms", milliseconds(usage.ru_utime) + milliseconds(usage.ru_stime)},
        {"io_input_blocks", static_cast<std::uint64_t>(usage.ru_inblock)},
        {"io_output_blocks", static_cast<std::uint64_t>(usage.ru_oublock)}};
#endif
}

inline nlohmann::json ResourceDelta(const nlohmann::json& before)
{
    auto after = Resources();
    for (auto& [name, value] : after.items()) {
        if (value < before.at(name)) throw std::runtime_error("process counters moved backwards");
        if (value.is_number_unsigned()) value = value.get<std::uint64_t>() - before.at(name).get<std::uint64_t>();
        else value = value.get<double>() - before.at(name).get<double>();
    }
    return after;
}
}
