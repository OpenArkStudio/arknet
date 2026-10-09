// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
namespace arknet::detail
{
template<class Derived, class Args> class alive_time_cp
{
public:
    auto last_alive_time() const noexcept { return get_last_alive_time(); }
    auto get_last_alive_time() const noexcept { return last_alive_time_; }
    Derived& update_alive_time() noexcept { last_alive_time_ = std::chrono::system_clock::now(); return static_cast<Derived&>(*this); }
    auto get_silence_duration() const noexcept { return std::chrono::system_clock::now() - last_alive_time_; }
protected:
    std::chrono::system_clock::time_point last_alive_time_ = std::chrono::system_clock::now();
};
}
