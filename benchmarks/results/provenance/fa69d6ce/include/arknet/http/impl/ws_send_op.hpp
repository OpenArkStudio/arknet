// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/external/beast.hpp>
#include <arknet/base/error.hpp>

namespace arknet::detail {
template<class Derived, class Args>
class ws_send_op {
protected:
    template<class Data, class Completion>
    bool _ws_send(Data& payload, Completion&& completion) {
        auto& owner = static_cast<Derived&>(*this);
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
        owner.ws_stream().async_write(asio::buffer(payload), make_allocator(owner.wallocator(),
            [&owner, lifetime = owner.selfptr(), completion = std::forward<Completion>(completion)]
            (error_code ec, std::size_t size) mutable {
#ifndef NDEBUG
                owner.post_send_counter_.fetch_sub(1);
#endif
                set_last_error(ec);
                completion(ec, size);
                if (ec && owner.state_ == state_t::started)
                    owner._do_disconnect(ec, owner.selfptr());
            }));
        return true;
    }
};
}
