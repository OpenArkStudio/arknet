// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
namespace arknet::detail
{
template <class Derived, class Args> class reconnect_timer_cp
{
public:
    template <class = void> Derived& set_auto_reconnect(bool enabled) noexcept
    {
        reconnect_enable_ = enabled;
        return static_cast<Derived&>(*this);
    }
    template <class Rep, class Period>
    Derived& set_auto_reconnect(bool enabled, std::chrono::duration<Rep, Period> delay) noexcept
    {
        reconnect_delay_ = to_steady_duration(delay);
        return set_auto_reconnect(enabled);
    }
    template <class = void> Derived& auto_reconnect(bool enabled) noexcept { return set_auto_reconnect(enabled); }
    template <class Rep, class Period>
    Derived& auto_reconnect(bool enabled, std::chrono::duration<Rep, Period> delay) noexcept
    {
        return set_auto_reconnect(enabled, delay);
    }
    template <class = void> bool is_auto_reconnect() const noexcept { return reconnect_enable_; }
    template <class = void> auto get_auto_reconnect_delay() const noexcept { return reconnect_delay_; }

protected:
    template <class Match>
    void _make_reconnect_timer(std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options)
    {
        static_cast<Derived&>(*this).dispatch(
            [this, owner = std::move(owner), options = std::move(options)]() mutable
            {
                if (reconnect_timer_)
                    reconnect_timer_->cancel();
                reconnect_timer_ = std::make_shared<safe_timer>(static_cast<Derived&>(*this).io_->executor());
                _post_reconnect_timer(std::move(owner), std::move(options), reconnect_timer_,
                                      std::chrono::nanoseconds::max());
            });
    }
    template <class Rep, class Period, class Match>
    void _post_reconnect_timer(std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options,
                               std::shared_ptr<safe_timer> timer, std::chrono::duration<Rep, Period> delay)
    {
        if (timer != reconnect_timer_ || timer->canceled.test(std::memory_order_acquire))
            return;
#ifndef NDEBUG
        is_post_reconnect_timer_called_ = true;
#endif
        if (delay == delay.max())
            timer->timer.expires_at(asio::steady_timer::time_point::max());
        else
            timer->timer.expires_after(to_steady_duration(delay));
        auto* operation = &timer->timer;
        operation->async_wait(
            [this, owner = std::move(owner), options = std::move(options),
             timer = std::move(timer)](const error_code& error) mutable
            { _handle_reconnect_timer(error, std::move(owner), std::move(options), std::move(timer)); });
    }
    template <class Match>
    void _handle_reconnect_timer(const error_code& error, std::shared_ptr<Derived> owner,
                                 std::shared_ptr<ecs_t<Match>> options, std::shared_ptr<safe_timer> timer)
    {
        if (timer != reconnect_timer_)
            return;
        if (timer->canceled.test(std::memory_order_acquire))
        {
            reconnect_timer_.reset();
            return;
        }
        auto& object = static_cast<Derived&>(*this);
        if (error == asio::error::operation_aborted)
        {
            _post_reconnect_timer(std::move(owner), std::move(options), std::move(timer), reconnect_delay_);
            return;
        }
        if (!error && reconnect_enable_)
        {
            object.push_event(
                [&object, owner, options, timer](event_queue_guard<Derived> guard) mutable
                {
                    if (timer->canceled.test(std::memory_order_acquire))
                        return;
                    state_t expected = state_t::stopped;
                    if (object.state_.compare_exchange_strong(expected, state_t::starting))
                        object.template _start_connect<true>(std::move(owner), std::move(options),
                                                             defer_event(std::move(guard)));
                });
        }
        _post_reconnect_timer(std::move(owner), std::move(options), std::move(timer), std::chrono::nanoseconds::max());
    }
    void _stop_reconnect_timer()
    {
        static_cast<Derived&>(*this).dispatch(
            [this]
            {
#ifndef NDEBUG
                is_stop_reconnect_timer_called_ = true;
#endif
                if (reconnect_timer_)
                    reconnect_timer_->cancel();
            });
    }
    void _wake_reconnect_timer()
    {
        static_cast<Derived&>(*this).dispatch(
            [this]
            {
                if (reconnect_enable_ && reconnect_timer_)
                    cancel_timer(reconnect_timer_->timer);
            });
    }
    std::shared_ptr<safe_timer> reconnect_timer_;
    std::chrono::steady_clock::duration reconnect_delay_ = std::chrono::seconds(1);
    bool reconnect_enable_ = true;
#ifndef NDEBUG
    bool is_stop_reconnect_timer_called_ = false;
    bool is_post_reconnect_timer_called_ = false;
#endif
};
}
