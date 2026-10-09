// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/detail/allocator.hpp>
namespace arknet::detail
{
template <class Derived, class Args> class connect_timeout_cp
{
public:
    auto get_connect_timeout() const noexcept { return connect_timeout_; }
    template <class Rep, class Period> Derived& set_connect_timeout(std::chrono::duration<Rep, Period> value) noexcept
    {
        connect_timeout_ = to_steady_duration(value);
        return static_cast<Derived&>(*this);
    }

protected:
    template <class Rep, class Period>
    void _make_connect_timeout_timer(std::shared_ptr<Derived> owner, std::chrono::duration<Rep, Period> duration)
    {
        auto& object = static_cast<Derived&>(*this);
        object.dispatch(
            [this, owner = std::move(owner), duration]() mutable
            {
                if (connect_timeout_timer_)
                    connect_timeout_timer_->cancel();
                connect_timeout_timer_ = std::make_shared<safe_timer>(static_cast<Derived&>(*this).io_->executor());
                _post_connect_timeout_timer(std::move(owner), connect_timeout_timer_, duration);
            });
    }
    template <class Rep, class Period>
    void _post_connect_timeout_timer(std::shared_ptr<Derived> owner, std::shared_ptr<safe_timer> timer,
                                     std::chrono::duration<Rep, Period> duration)
    {
        if (timer != connect_timeout_timer_)
            return;
        timer->timer.expires_after(to_steady_duration(duration));
        auto* operation = &timer->timer;
        operation->async_wait(
            [this, owner = std::move(owner), timer = std::move(timer)](const error_code& error) mutable
            { _handle_connect_timeout_timer(error, std::move(owner), std::move(timer)); });
    }
    template <class Object = Derived>
    void _handle_connect_timeout_timer(const error_code& error, std::shared_ptr<Object> owner,
                                       std::shared_ptr<safe_timer> timer)
    {
        if (timer != connect_timeout_timer_)
            return;
        connect_timeout_timer_.reset();
        if (error || timer->canceled.test(std::memory_order_acquire))
            return;
        auto& object = static_cast<Derived&>(*this);
        if constexpr (Object::is_session())
            object._do_disconnect(asio::error::timed_out, std::move(owner));
        else
        {
            error_code ignored;
            object.socket().shutdown(asio::socket_base::shutdown_both, ignored);
            object.socket().cancel(ignored);
            object.socket().close(ignored);
        }
    }
    void _stop_connect_timeout_timer()
    {
        static_cast<Derived&>(*this).dispatch(
            [this]
            {
#ifndef NDEBUG
                is_stop_connect_timeout_timer_called_ = true;
#endif
                if (connect_timeout_timer_)
                    connect_timeout_timer_->cancel();
            });
    }
    std::shared_ptr<safe_timer> connect_timeout_timer_;
    std::chrono::steady_clock::duration connect_timeout_ = std::chrono::seconds(30);
#ifndef NDEBUG
    bool is_stop_connect_timeout_timer_called_ = false;
#endif
};
}
