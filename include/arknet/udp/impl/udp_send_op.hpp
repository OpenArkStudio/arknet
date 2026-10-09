// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/error.hpp>
#include <arknet/base/detail/allocator.hpp>

namespace arknet::detail
{
template <class Derived, class Args> class udp_send_op
{
protected:
    template <class Data, class Completion> bool _udp_send(Data& data, Completion&& completion)
    {
        auto& owner = static_cast<Derived&>(*this);
        owner.stream().async_send(asio::buffer(data),
                                  make_allocator(owner.wallocator(),
                                                 [completion = std::forward<Completion>(completion)](
                                                     const error_code& error, std::size_t count) mutable
                                                 {
                                                     // Winsock may leave an invalid byte count on a failed datagram
                                                     // send.
                                                     completion(error, error ? 0 : count);
                                                 }));
        return true;
    }

    template <class Data, class Completion>
    bool _udp_send_to(const asio::ip::udp::endpoint& target, Data& data, Completion&& completion)
    {
        auto& owner = static_cast<Derived&>(*this);
        // All sessions of a UDP server use its socket executor.
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        owner.stream().async_send_to(asio::buffer(data), target,
                                     make_allocator(owner.wallocator(),
                                                    [completion = std::forward<Completion>(completion)](
                                                        const error_code& error, std::size_t count) mutable
                                                    { completion(error, error ? 0 : count); }));
        return true;
    }
};
}
