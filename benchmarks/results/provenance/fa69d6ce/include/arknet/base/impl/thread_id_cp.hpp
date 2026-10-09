// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <thread>
namespace arknet::detail
{
template<class Derived, class Args = void> class thread_id_cp
{
public:
    bool running_in_this_thread() const noexcept { return static_cast<const Derived&>(*this).io_->running_in_this_thread(); }
    std::thread::id get_thread_id() const noexcept { return static_cast<const Derived&>(*this).io_->get_thread_id(); }
};
}
