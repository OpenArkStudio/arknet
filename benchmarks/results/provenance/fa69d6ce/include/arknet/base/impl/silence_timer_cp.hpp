// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
namespace arknet::detail
{
template<class Derived, class Args> class silence_timer_cp
{
public:
    auto get_silence_timeout() const noexcept { return silence_timeout_; }
    template<class Rep, class Period> Derived& set_silence_timeout(std::chrono::duration<Rep, Period> duration) noexcept { silence_timeout_ = to_steady_duration(duration); return static_cast<Derived&>(*this); }
protected:
    template<class Rep, class Period> void _post_silence_timer(std::chrono::duration<Rep, Period> delay, std::shared_ptr<Derived> owner)
    {
        static_cast<Derived&>(*this).dispatch([this, owner = std::move(owner), delay]() mutable
        {
            if (delay <= delay.zero()) return;
            if (silence_timer_) silence_timer_->cancel();
            auto timer = std::make_shared<safe_timer>(static_cast<Derived&>(*this).io_->executor());
            silence_timer_ = timer;
            timer->timer.expires_after(to_steady_duration(delay));
            timer->timer.async_wait([this, owner = std::move(owner), timer](const error_code& error) mutable
            {
                if (timer != silence_timer_ || timer->canceled.test(std::memory_order_acquire)) return;
                _handle_silence_timer(error, std::move(owner));
            });
        });
    }
    void _handle_silence_timer(const error_code& error, std::shared_ptr<Derived> owner)
    {
        if (error) { silence_timer_.reset(); return; }
        auto& object = static_cast<Derived&>(*this);
        const auto silent = object.get_silence_duration();
        if (silent < silence_timeout_) _post_silence_timer(silence_timeout_ - silent, std::move(owner));
        else { silence_timer_.reset(); object._do_disconnect(asio::error::timed_out, std::move(owner)); }
    }
    void _stop_silence_timer()
    {
        static_cast<Derived&>(*this).dispatch([this]
        {
#ifndef NDEBUG
            is_stop_silence_timer_called_ = true;
#endif
            if (silence_timer_) { silence_timer_->cancel(); silence_timer_.reset(); }
        });
    }
    std::shared_ptr<safe_timer> silence_timer_;
    std::chrono::steady_clock::duration silence_timeout_ = std::chrono::milliseconds(tcp_silence_timeout);
#ifndef NDEBUG
    bool is_stop_silence_timer_called_ = false;
#endif
};
}
