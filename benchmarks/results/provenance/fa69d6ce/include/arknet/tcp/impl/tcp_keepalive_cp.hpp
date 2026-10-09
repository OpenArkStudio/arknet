// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/base/detail/keepalive_options.hpp>

namespace arknet::detail {
template<class Derived, class Args>
class tcp_keepalive_cp {
public:
    bool set_keep_alive_options(bool enabled = true, unsigned idle = 60,
        unsigned interval = 3, unsigned probes = 3) noexcept {
        return set_keepalive_options(static_cast<Derived&>(*this).socket(), enabled, idle, interval, probes);
    }
};
}
