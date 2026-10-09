// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
namespace arknet
{
class spin_lock
{
public:
    bool try_lock() noexcept { return !v_.test_and_set(std::memory_order_acquire); }
    void lock() noexcept
    {
        while (!try_lock())
            v_.wait(true, std::memory_order_relaxed);
    }
    void unlock() noexcept
    {
        v_.clear(std::memory_order_release);
        v_.notify_one();
    }
    std::atomic_flag v_ = ATOMIC_FLAG_INIT;
};
}
