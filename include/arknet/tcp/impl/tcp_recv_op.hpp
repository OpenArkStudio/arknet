// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once

#include <memory>
#include <string_view>
#include <type_traits>
#include <arknet/base/error.hpp>
#include <arknet/base/detail/ecs.hpp>

namespace arknet::detail
{
template <class Derived, class Args> class tcp_recv_op
{
protected:
    template <class T, class = void> struct has_member_dgram : std::bool_constant < requires(T& value)
    {
        value.dgram_;
    } > {};

    template <class Condition>
    void _tcp_post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy)
    {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        if (!owner.is_started())
        {
            if (owner.state_ == state_t::started)
                owner._do_disconnect(get_last_error(), std::move(lifetime));
            return;
        }
        ARKNET_ASSERT(!owner.reading_);
        owner.reading_ = true;
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_recv_counter_.fetch_add(1) == 0);
#endif
        auto completion =
            make_allocator(owner.rallocator(),
                           [&owner, lifetime = std::move(lifetime), policy](error_code ec, std::size_t size) mutable
                           {
                               owner.reading_ = false;
#ifndef NDEBUG
                               owner.post_recv_counter_.fetch_sub(1);
#endif
                               owner._handle_recv(ec, size, std::move(lifetime), std::move(policy));
                           });
        auto&& condition = policy->get_condition().lowest();
        if constexpr (std::is_invocable_v<decltype(condition), const error_code&, std::size_t>)
            asio::async_read(owner.stream(), owner.buffer().base(), condition, std::move(completion));
        else
            asio::async_read_until(owner.stream(), owner.buffer().base(), condition, std::move(completion));
    }

    template <class Condition>
    void _tcp_dgram_fire_recv(const error_code&, std::size_t size, std::shared_ptr<Derived>& lifetime,
                              std::shared_ptr<ecs_t<Condition>>& policy)
    {
        auto& owner = static_cast<Derived&>(*this);
        const auto* bytes = static_cast<const unsigned char*>(owner.buffer().data().data());
        const std::size_t prefix = bytes[0] < 254 ? 1 : bytes[0] == 254 ? 3 : 9;
        ARKNET_ASSERT(size >= prefix);
        owner._fire_recv(lifetime, policy,
                         std::string_view{reinterpret_cast<const char*>(bytes + prefix), size - prefix});
    }

    template <class Condition>
    void _tcp_handle_recv(const error_code& ec, std::size_t size, std::shared_ptr<Derived> lifetime,
                          std::shared_ptr<ecs_t<Condition>> policy)
    {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        set_last_error(ec);
        // A completion from an earlier connection must not stop a new connection attempt.
        if (!owner.is_started())
        {
            if (owner.state_ == state_t::started)
                owner._do_disconnect(ec, lifetime);
            owner._stop_readend_timer(std::move(lifetime));
            return;
        }
        if (ec)
        {
            owner._do_disconnect(ec, lifetime);
            owner._stop_readend_timer(std::move(lifetime));
            return;
        }
        using Mode = typename ecs_t<Condition>::condition_lowest_type;
        owner.update_alive_time();
        if constexpr (std::is_same_v<Mode, use_dgram_t>)
        {
            if (size == 0)
            {
                owner._do_disconnect(asio::error::no_data, lifetime);
                owner._stop_readend_timer(std::move(lifetime));
                return;
            }
            owner._tcp_dgram_fire_recv(ec, size, lifetime, policy);
        }
        else
        {
            const auto delivered = std::is_same_v<Mode, hook_buffer_t> ? owner.buffer().size() : size;
            owner._fire_recv(lifetime, policy,
                             std::string_view{static_cast<const char*>(owner.buffer().data().data()), delivered});
        }
        if constexpr (!std::is_same_v<Mode, hook_buffer_t>)
            owner.buffer().consume(size);
        owner._post_recv(std::move(lifetime), std::move(policy));
    }
};
}
