// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <chrono>
#include <limits>
#include <optional>

namespace arknet::detail {

// Convert before narrowing: integral duration_cast can overflow even when the
// requested milliseconds value itself is positive and representable.
inline std::optional<std::chrono::steady_clock::time_point> checked_deadline(
    std::chrono::steady_clock::time_point base, std::chrono::milliseconds timeout) noexcept {
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using rep = duration::rep;
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::nullopt;
    }
    const auto ticks = std::chrono::duration<long double, duration::period>(timeout).count();
    // The floating representation of max may round upward. Rejecting equality
    // keeps the following float-to-integer conversion within range on that ABI.
    if (ticks >= static_cast<long double>((std::numeric_limits<rep>::max)()))
    {
        return std::nullopt;
    }
    const duration delta(static_cast<rep>(ticks));
    if (base.time_since_epoch() > duration::max() - delta)
    {
        return std::nullopt;
    }
    return base + delta;
}

} // namespace arknet::detail
