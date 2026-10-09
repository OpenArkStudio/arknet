#pragma once

#include "vendor/doctest/doctest.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

#define check(value, message) DOCTEST_REQUIRE_MESSAGE((value), (message))

class messages
{
public:
    void push(std::string_view data)
    {
        std::lock_guard lock(mutex_);
        values_.emplace_back(data);
        ready_.notify_all();
    }

    std::vector<std::string> wait(std::size_t count)
    {
        std::unique_lock lock(mutex_);
        check(ready_.wait_for(lock, 5s, [&] { return values_.size() >= count; }), "receive timeout");
        return values_;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<std::string> values_;
};

template <class Future> inline auto await(Future& future)
{
    check(future.wait_for(5s) == std::future_status::ready, "completion timeout");
    return future.get();
}
