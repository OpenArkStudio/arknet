// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

inline void benchmark_work(std::size_t iterations, std::string_view data)
{
    std::uint64_t value = data.empty() ? 1 : static_cast<unsigned char>(data.back()) + 1;
    for (std::size_t i = 0; i < iterations; ++i)
    {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    // Keep simulated handler CPU work observable without a shared synchronization point.
    thread_local volatile std::uint64_t sink;
    sink = value;
}
