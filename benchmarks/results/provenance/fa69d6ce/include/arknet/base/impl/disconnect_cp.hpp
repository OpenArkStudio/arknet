// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
#include <memory>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
namespace arknet::detail
{
template<class Derived, class Args> class disconnect_cp
{
public:
    using self = disconnect_cp;
    auto get_disconnect_timeout() const noexcept { return disconnect_timeout_; }
    template<class Rep, class Period> Derived& set_disconnect_timeout(std::chrono::duration<Rep, Period> duration) noexcept { disconnect_timeout_ = to_steady_duration(duration); return static_cast<Derived&>(*this); }
protected:
    template<class Chain = defer_event<void, Derived>> void _do_disconnect(const error_code& error, std::shared_ptr<Derived> owner, Chain chain = {})
    {
        auto& object = static_cast<Derived&>(*this);
        object.dispatch([&object, error, owner = std::move(owner), chain = std::move(chain)]() mutable { object._do_shutdown(error, std::move(owner), std::move(chain)); });
    }
    template<class Chain> void _post_disconnect(const error_code& error, std::shared_ptr<Derived> owner, Chain chain) { static_cast<Derived&>(*this)._handle_disconnect(error, std::move(owner), std::move(chain)); }
    template<class Chain> void _handle_disconnect(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if (object.reading_) object._make_readend_timer(error, std::move(owner), std::move(chain));
        else object._handle_readend(error, std::move(owner), std::move(chain));
    }
    template<class Chain> void _handle_readend(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        object.disconnecting_ = false;
        if constexpr (Args::is_session) object._do_stop(error, std::move(owner), std::move(chain));
    }
    template<class Chain> void _make_readend_timer(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        if (readend_timer_) readend_timer_->cancel();
        auto& object = static_cast<Derived&>(*this);
        readend_timer_ = std::make_shared<safe_timer>(object.io_->executor());
        object._post_readend_timer(error, std::move(owner), std::move(chain), readend_timer_);
    }
    template<class Chain> void _post_readend_timer(const error_code& error, std::shared_ptr<Derived> owner, Chain chain, std::shared_ptr<safe_timer> timer)
    {
        if (timer != readend_timer_) return;
        auto& object = static_cast<Derived&>(*this);
        timer->timer.expires_after(object.get_disconnect_timeout());
        auto* operation = &timer->timer;
        operation->async_wait([&object, error, owner = std::move(owner), chain = std::move(chain), timer = std::move(timer)](const error_code& failure) mutable { object._handle_readend_timer(failure, error, std::move(owner), std::move(chain), std::move(timer)); });
    }
    template<class Chain> void _handle_readend_timer(const error_code&, const error_code& error, std::shared_ptr<Derived> owner, Chain chain, std::shared_ptr<safe_timer> timer)
    {
        if (timer != readend_timer_) return;
        readend_timer_.reset();
        static_cast<Derived&>(*this)._handle_readend(error, std::move(owner), std::move(chain));
    }
    void _stop_readend_timer(std::shared_ptr<Derived> owner) { static_cast<Derived&>(*this).dispatch([this, owner = std::move(owner)] { if (readend_timer_) readend_timer_->cancel(); }); }
    std::chrono::steady_clock::duration disconnect_timeout_ = std::chrono::seconds(30);
    bool disconnecting_ = false;
    std::shared_ptr<safe_timer> readend_timer_;
};
}
