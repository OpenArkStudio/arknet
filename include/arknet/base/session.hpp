// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/define.hpp>
#include <arknet/base/detail/object.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/buffer_wrap.hpp>
#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/session_mgr.hpp>
#include <arknet/base/impl/io_context_cp.hpp>
#include <arknet/base/impl/thread_id_cp.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
#include <arknet/base/impl/user_data_cp.hpp>
#include <arknet/base/impl/connect_time_cp.hpp>
#include <arknet/base/impl/alive_time_cp.hpp>
#include <arknet/base/impl/socket_cp.hpp>
#include <arknet/base/impl/connect_cp.hpp>
#include <arknet/base/impl/shutdown_cp.hpp>
#include <arknet/base/impl/close_cp.hpp>
#include <arknet/base/impl/disconnect_cp.hpp>
#include <arknet/base/impl/user_timer_cp.hpp>
#include <arknet/base/impl/connect_timeout_cp.hpp>
#include <arknet/base/impl/send_cp.hpp>
#include <arknet/base/impl/post_cp.hpp>
#include <arknet/base/impl/condition_event_cp.hpp>
#include <arknet/base/impl/silence_timer_cp.hpp>
namespace arknet
{
class session
{
public:
    static constexpr bool is_client() noexcept { return false; }
    static constexpr bool is_session() noexcept { return true; }
    static constexpr bool is_server() noexcept { return false; }
};
}
namespace arknet::detail
{
template <class Derived, class Args>
class session_impl_t : public arknet::session,
                       public object_t<Derived>,
                       public io_context_cp<Derived, Args>,
                       public thread_id_cp<Derived, Args>,
                       public event_queue_cp<Derived, Args>,
                       public user_data_cp<Derived, Args>,
                       public connect_time_cp<Derived, Args>,
                       public alive_time_cp<Derived, Args>,
                       public socket_cp<Derived, Args>,
                       public connect_cp<Derived, Args>,
                       public shutdown_cp<Derived, Args>,
                       public close_cp<Derived, Args>,
                       public disconnect_cp<Derived, Args>,
                       public user_timer_cp<Derived, Args>,
                       public connect_timeout_cp<Derived, Args>,
                       public send_cp<Derived, Args>,
                       public post_cp<Derived, Args>,
                       public condition_event_cp<Derived, Args>,
                       public silence_timer_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;

public:
    using super = object_t<Derived>;
    using self = session_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;
    using send_cp<Derived, Args>::send;
    using send_cp<Derived, Args>::async_send;
    template <class... SocketArgs>
    session_impl_t(session_mgr_t<Derived>& sessions, listener_t& listener, std::shared_ptr<io_t> lane,
                   std::size_t initial, std::size_t maximum, SocketArgs&&... socket_args)
        : io_context_cp<Derived, Args>(std::move(lane)),
          socket_cp<Derived, Args>(std::forward<SocketArgs>(socket_args)...), sessions_(sessions), listener_(listener),
          buffer_(initial, maximum)
    {
    }
    void stop()
    {
        this->derived().dispatch(
            [this]
            {
                this->_stop_silence_timer();
                this->_stop_connect_timeout_timer();
                this->_dispatch_stop_all_timers();
                this->_dispatch_stop_all_timed_events();
                this->notify_all_condition_events();
                buffer_.consume(buffer_.size());
                this->clear_user_data();
                ecs_.reset();
                counter_ptr_.reset();
            });
    }
    void destroy()
    {
        this->socket_.reset();
        this->io_.reset();
    }
    bool is_started() const { return state_.load() == state_t::started && this->socket().is_open(); }
    bool is_stopped() const { return state_.load() == state_t::stopped && !this->socket().is_open(); }
    buffer_wrap<buffer_type>& buffer() noexcept { return buffer_; }

protected:
    void start()
    {
        this->derived().dispatch(
            [this]
            {
                this->io_->init_thread_id();
                this->_make_connect_timeout_timer(this->selfptr(), this->connect_timeout_);
            });
    }
    session_mgr_t<Derived>& sessions() noexcept { return sessions_; }
    listener_t& listener() noexcept { return listener_; }
    std::atomic<state_t>& state() noexcept { return state_; }
    constexpr bool life_id() const noexcept { return true; }
    constexpr void reset_life_id() noexcept {}
    session_mgr_t<Derived>& sessions_;
    listener_t& listener_;
    buffer_wrap<buffer_type> buffer_;
    std::atomic<state_t> state_ = state_t::stopped;
    std::shared_ptr<void> counter_ptr_;
    std::shared_ptr<ecs_base> ecs_;
    bool reading_ = false;
#ifndef NDEBUG
    std::atomic<int> post_send_counter_ = 0;
    std::atomic<int> post_recv_counter_ = 0;
#endif
};
}
