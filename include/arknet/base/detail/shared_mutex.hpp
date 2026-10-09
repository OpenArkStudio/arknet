// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <mutex>
#include <shared_mutex>

namespace arknet
{
class shared_mutexer : public std::shared_mutex
{
public:
    std::shared_mutex& native_handle() noexcept { return *this; }
};
using shared_locker = std::shared_lock<shared_mutexer>;
using unique_locker = std::unique_lock<shared_mutexer>;
}

#define ARKNET_GUARDED_BY(...)
#define ARKNET_NO_THREAD_SAFETY_ANALYSIS
