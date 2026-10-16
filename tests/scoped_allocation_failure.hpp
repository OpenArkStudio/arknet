// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <atomic>
#include <cstddef>

namespace scoped_allocation_failure {

inline std::atomic<std::size_t> injections{};
inline thread_local bool armed{};

inline void arm() noexcept {
    armed = true;
}

inline void disarm() noexcept {
    armed = false;
}

class scope
{
    bool previous_;

public:
    scope() noexcept
        : previous_(armed) {
        arm();
    }

    scope(const scope&) = delete;
    scope& operator=(const scope&) = delete;

    ~scope() {
        armed = previous_;
    }
};

} // namespace scoped_allocation_failure
