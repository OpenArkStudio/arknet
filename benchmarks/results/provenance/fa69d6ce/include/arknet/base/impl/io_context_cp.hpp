// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/base/io_pool.hpp>
namespace arknet::detail
{
template<class Derived, class Args = void> class io_context_cp
{
public:
    explicit io_context_cp(std::shared_ptr<io_t> context) : io_(std::move(context)) {}
    io_t& io() noexcept { return *io_; }
    const io_t& io() const noexcept { return *io_; }
    std::shared_ptr<io_t> io_ptr() noexcept { return io_; }
protected:
    std::shared_ptr<io_t> io_;
};
}
