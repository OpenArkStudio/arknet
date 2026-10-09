// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
namespace arknet::detail
{
template<class Derived, class Args> class shutdown_cp
{
public:
    using self = shutdown_cp;
protected:
    void _arm_stop_deadline()
    {
        auto& object = static_cast<Derived&>(*this);
        const auto state = object.state_.load();
        // A stop queued before close must not arm a fresh timer after shutdown.
        if (stop_deadline_timer_ || (state != state_t::starting && state != state_t::started)) return;
        auto timer = std::make_shared<safe_timer>(object.io_->executor());
        stop_deadline_timer_ = timer;
        timer->timer.expires_after(object.get_disconnect_timeout());
        timer->timer.async_wait([timer, socket = object.socket_](const error_code& error)
        {
            if (error || timer->canceled.test(std::memory_order_acquire)) return;
            error_code ignored;
            socket->cancel(ignored);
            socket->close(ignored);
        });
    }
    template<class Chain> void _do_shutdown(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if (shutdown_timer_ || object.disconnecting_) { object._stop_shutdown_timer(std::move(owner)); return; }
        object.disp_event([&object, error, owner = std::move(owner), completion = chain.move_event()](event_queue_guard<Derived> guard) mutable
        {
            defer_event next(std::move(completion), std::move(guard));
            set_last_error(error);
            const auto state = object.state_.load();
            if (state == state_t::started || state == state_t::starting)
            {
                object.disconnecting_ = true;
                object._post_shutdown(error, std::move(owner), std::move(next));
            }
            else object.post([owner = std::move(owner), next = std::move(next)]() mutable {});
        }, chain.move_guard());
    }
    template<class Chain> void _post_shutdown(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if constexpr (std::is_same_v<typename Derived::socket_type::protocol_type, asio::ip::tcp>) object._post_shutdown_tcp(error, std::move(owner), std::move(chain));
        else object._handle_shutdown(error, std::move(owner), std::move(chain));
    }
    template<class Chain> void _post_shutdown_tcp(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if (object.socket().is_open())
        {
            error_code option_error;
            asio::socket_base::linger linger;
            object.socket().lowest_layer().get_option(linger, option_error);
            if (!option_error && !(linger.enabled() && linger.timeout() == 0))
            {
                error_code shutdown_error;
                object.socket().shutdown(asio::socket_base::shutdown_send, shutdown_error);
                if (!shutdown_error && object.reading_) { object._make_shutdown_timer(error, std::move(owner), std::move(chain)); return; }
            }
        }
        object._handle_shutdown(error, std::move(owner), std::move(chain));
    }
    template<class Chain> void _handle_shutdown(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        shutdown_timer_.reset();
        if (stop_deadline_timer_) { stop_deadline_timer_->cancel(); stop_deadline_timer_.reset(); }
        static_cast<Derived&>(*this)._do_close(error, std::move(owner), std::move(chain));
    }
    template<class Chain> void _make_shutdown_timer(const error_code& error, std::shared_ptr<Derived> owner, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if (shutdown_timer_) shutdown_timer_->cancel();
        shutdown_timer_ = std::make_shared<safe_timer>(object.io_->executor());
        object._post_shutdown_timer(error, std::move(owner), std::move(chain), object.get_disconnect_timeout(), shutdown_timer_);
    }
    template<class Chain, class Rep, class Period> void _post_shutdown_timer(const error_code& error, std::shared_ptr<Derived> owner, Chain chain, std::chrono::duration<Rep, Period> delay, std::shared_ptr<safe_timer> timer)
    {
        if (timer != shutdown_timer_) return;
        timer->timer.expires_after(to_steady_duration(delay));
        auto* operation = &timer->timer;
        operation->async_wait([this, error, owner = std::move(owner), chain = std::move(chain), timer = std::move(timer)](const error_code& failure) mutable
        {
            static_cast<Derived&>(*this)._handle_shutdown_timer(failure, error, std::move(owner), std::move(chain), std::move(timer));
        });
    }
    template<class Chain> void _handle_shutdown_timer(const error_code& failure, const error_code& error, std::shared_ptr<Derived> owner, Chain chain, std::shared_ptr<safe_timer> timer)
    {
        if (timer != shutdown_timer_) return;
        auto& object = static_cast<Derived&>(*this);
#ifdef ARKNET_WAIT_FOR_READING_END
        if (!failure && !timer->canceled.test(std::memory_order_acquire))
        {
            const auto silent = object.get_silence_duration();
            if (silent < object.get_disconnect_timeout()) { object._post_shutdown_timer(error, std::move(owner), std::move(chain), object.get_disconnect_timeout() - silent, std::move(timer)); return; }
        }
#endif
        object._handle_shutdown(error, std::move(owner), std::move(chain));
    }
    void _stop_shutdown_timer(std::shared_ptr<Derived> owner)
    {
        static_cast<Derived&>(*this).dispatch([this, owner = std::move(owner)] { if (shutdown_timer_) shutdown_timer_->cancel(); });
    }
    std::shared_ptr<safe_timer> shutdown_timer_;
    std::shared_ptr<safe_timer> stop_deadline_timer_;
};
}
