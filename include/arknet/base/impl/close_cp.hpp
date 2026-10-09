// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
namespace arknet::detail
{
template <class Derived, class Args> class close_cp
{
public:
    using self = close_cp;

protected:
    template <class Chain> void _do_close(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        object.disp_event(
            [&object, error, owner = std::move(owner),
             completion = chain.move_event()](event_queue_guard<Derived> guard) mutable
            {
                defer_event next(std::move(completion), std::move(guard));
                set_last_error(error);
                auto prior = object.state_.load();
                while (prior == state_t::started || prior == state_t::starting)
                {
                    if (object.state_.compare_exchange_weak(prior, state_t::stopping))
                    {
                        object._post_close(error, std::move(owner), prior, std::move(next));
                        return;
                    }
                }
                object.post([owner = std::move(owner), next = std::move(next)]() mutable {});
            },
            chain.move_guard());
    }
    template <class Chain>
    void _post_close(const error_code& error, std::shared_ptr<Derived> owner, state_t prior, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if constexpr (Args::is_session)
        {
            // Session map mutation and disconnect notification belong to the acceptor lane.
            object.post(
                [&object, error, prior, owner = std::move(owner), chain = std::move(chain)]() mutable
                {
                    object.sessions_.erase(
                        owner,
                        [&object, error, prior, owner, chain = std::move(chain)](bool erased) mutable
                        {
                            set_last_error(error);
                            auto expected = state_t::stopping;
                            if (object.state_.compare_exchange_strong(expected, state_t::stopped) && erased &&
                                prior == state_t::started)
                                object._fire_disconnect(owner);
                            object.dispatch(
                                [&object, error, owner = std::move(owner), chain = std::move(chain)]() mutable
                                { object._handle_close(error, std::move(owner), std::move(chain)); });
                        });
                });
        }
        else
        {
            set_last_error(error);
            auto expected = state_t::stopping;
            if (object.state_.compare_exchange_strong(expected, state_t::stopped) && prior == state_t::started)
                object._fire_disconnect(owner);
            if (!chain.empty())
                object._handle_close(error, std::move(owner), std::move(chain));
            else
            {
                auto guard = chain.move_guard();
                object._handle_close(error, owner,
                                     defer_event(
                                         [&object, owner](event_queue_guard<Derived> guard) mutable
                                         {
                                             object.disp_event(
                                                 [&object, owner = std::move(owner)](event_queue_guard<Derived>) mutable
                                                 {
                                                     if (object.reconnect_enable_)
                                                         object._wake_reconnect_timer();
                                                     else
                                                         object._stop_reconnect_timer();
                                                 },
                                                 std::move(guard));
                                         },
                                         std::move(guard)));
            }
        }
    }
    template <class Chain> void _handle_close(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        static_cast<Derived&>(*this)._post_disconnect(error, std::move(owner), std::move(chain));
    }
};
}
