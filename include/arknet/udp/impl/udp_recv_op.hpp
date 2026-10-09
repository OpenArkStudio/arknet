// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/error.hpp>

namespace arknet::detail
{
template <class Derived, class Args> class udp_recv_op
{
protected:
    template <class Condition>
    void _udp_post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition)
    {
        auto& owner = static_cast<Derived&>(*this);
        if (!owner.is_started())
            return;
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        ARKNET_ASSERT(!owner.reading_);
        owner.reading_ = true;
        auto storage = owner.buffer().prepare(udp_frame_size);
        owner.socket().async_receive(
            storage, make_allocator(owner.rallocator(),
                                    [&owner, lifetime = std::move(lifetime), condition = std::move(condition)](
                                        const error_code& error, std::size_t count) mutable
                                    {
                                        owner.reading_ = false;
                                        owner._handle_recv(error, count, std::move(lifetime), std::move(condition));
                                    }));
    }

    template <class Condition>
    void _udp_handle_recv(const error_code& error, std::size_t count, std::shared_ptr<Derived> lifetime,
                          std::shared_ptr<ecs_t<Condition>> condition)
    {
        auto& owner = static_cast<Derived&>(*this);
        set_last_error(error);
        if (!owner.is_started())
        {
            owner._stop_readend_timer(std::move(lifetime));
            return;
        }
        if (error == asio::error::operation_aborted || error == asio::error::connection_refused)
        {
            owner._do_disconnect(error, lifetime);
            owner._stop_readend_timer(std::move(lifetime));
            return;
        }
        if (!error)
        {
            owner.buffer().commit(count);
            owner.update_alive_time();
            auto bytes = owner.buffer().data();
            owner._fire_recv(lifetime, condition, std::string_view(static_cast<const char*>(bytes.data()), count));
            owner.buffer().consume(owner.buffer().size());
        }
        owner._post_recv(std::move(lifetime), std::move(condition));
    }
};
}
