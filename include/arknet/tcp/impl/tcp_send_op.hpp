// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <arknet/base/error.hpp>
#include <arknet/base/detail/buffer_wrap.hpp>

namespace arknet::detail
{
template <class Derived, class Args> class tcp_send_op
{
protected:
    template <class T, class = void> struct has_member_dgram : std::bool_constant < requires(T& value)
    {
        value.dgram_;
    } > {};

    template <class Data, class Completion> bool _tcp_send(Data& data, Completion&& completion)
    {
        auto& owner = static_cast<Derived&>(*this);
        if constexpr (requires { owner.dgram_; })
        {
            if (owner.dgram_)
                return owner._tcp_send_dgram(asio::buffer(data), std::forward<Completion>(completion));
        }
        return owner._tcp_send_general(asio::buffer(data), std::forward<Completion>(completion));
    }

    template <class Buffer, class Completion> bool _tcp_send_dgram(Buffer&& payload, Completion&& completion)
    {
        auto header = std::make_shared<std::array<std::uint8_t, 9>>();
        const auto size = payload.size();
        const std::size_t prefix = size < 254 ? 1 : size <= UINT16_MAX ? 3 : 9;
        (*header)[0] = prefix == 1 ? static_cast<std::uint8_t>(size) : prefix == 3 ? 254 : 255;
        for (std::size_t index = 1; index < prefix; ++index)
            (*header)[index] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(size) >> (8 * (index - 1)));
        std::array<asio::const_buffer, 2> buffers{asio::buffer(header->data(), prefix), payload};
        return write(buffers, prefix, std::move(header), std::forward<Completion>(completion));
    }

    template <class Buffer, class Completion> bool _tcp_send_general(Buffer&& payload, Completion&& completion)
    {
        return write(std::forward<Buffer>(payload), 0, std::shared_ptr<void>{}, std::forward<Completion>(completion));
    }

private:
    template <class Buffers, class Storage, class Completion>
    bool write(Buffers&& buffers, std::size_t prefix, Storage storage, Completion&& completion)
    {
        auto& owner = static_cast<Derived&>(*this);
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
        asio::async_write(owner.stream(), buffers,
                          make_allocator(owner.wallocator(),
                                         [&owner, lifetime = owner.selfptr(), storage = std::move(storage), prefix,
                                          completion = std::forward<Completion>(completion)](
                                             error_code ec, std::size_t transferred) mutable
                                         {
#ifndef NDEBUG
                                             owner.post_send_counter_.fetch_sub(1);
#endif
                                             set_last_error(ec);
                                             completion(ec, transferred > prefix ? transferred - prefix : 0);
                                             // A partially written frame cannot be retried on the same byte stream.
                                             if (ec && owner.state_ == state_t::started)
                                                 owner._do_disconnect(ec, owner.selfptr());
                                         }));
        return true;
    }
};
}
