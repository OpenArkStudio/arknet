// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
namespace arknet::detail
{
template<class Derived, class Args> class connect_time_cp
{
public:
    auto get_connect_time() const noexcept { return connect_time_; }
    Derived& reset_connect_time() noexcept { connect_time_ = std::chrono::system_clock::now(); return static_cast<Derived&>(*this); }
    auto get_connect_duration() const noexcept { return std::chrono::system_clock::now() - connect_time_; }
protected:
    std::chrono::system_clock::time_point connect_time_ = std::chrono::system_clock::now();
};
}
