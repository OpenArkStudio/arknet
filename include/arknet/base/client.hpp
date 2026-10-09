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
#include <arknet/base/impl/reconnect_timer_cp.hpp>
namespace arknet
{
class client
{
public:
    static constexpr bool is_client() noexcept { return true; }
    static constexpr bool is_session() noexcept { return false; }
    static constexpr bool is_server() noexcept { return false; }
};
}
namespace arknet::detail
{
template <class Derived, class Args>
class client_impl_t : public arknet::client,
                      public object_t<Derived>,
                      public iopool_cp<Derived, Args>,
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
                      public reconnect_timer_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;

public:
    using super = object_t<Derived>;
    using self = client_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;
    using send_cp<Derived, Args>::send;
    using send_cp<Derived, Args>::async_send;
    using key_type = std::size_t;
    using iopoolcp = iopool_cp<Derived, Args>;
    template <class Scheduler>
    client_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : iopoolcp(std::forward<Scheduler>(scheduler)), io_context_cp<Derived, Args>(iopoolcp::_get_io(0)),
          socket_cp<Derived, Args>(this->io_->executor()), buffer_(initial, maximum)
    {
    }
    bool start() noexcept
    {
        stopped_.store(false);
        return true;
    }
    bool async_start() noexcept { return start(); }
    void stop()
    {
        this->derived().dispatch(
            [this]
            {
                this->_stop_reconnect_timer();
                this->_stop_connect_timeout_timer();
                this->_dispatch_stop_all_timers();
                this->_dispatch_stop_all_timed_events();
                this->notify_all_condition_events();
                buffer_.consume(buffer_.size());
                this->clear_user_data();
                ecs_.reset();
                reset_life_id();
                stopped_.store(true);
            });
    }
    void destroy()
    {
        this->socket_.reset();
        this->io_.reset();
        listener_.clear();
        this->destroy_iopool();
    }
    bool is_started() const { return state_.load() == state_t::started && this->socket().is_open(); }
    bool is_stopped() const { return state_.load() == state_t::stopped && stopped_.load(); }
    void request_stop()
    {
        if (!this->derived().is_stopped())
            this->post([this] { this->derived().stop(); });
    }
    bool wait_stopped()
    {
        if (this->iopool().running_in_threads() || this->io_->context().get_executor().running_in_this_thread())
        {
            set_last_error(asio::error::operation_not_supported);
            return false;
        }
        this->derived().stop();
        return this->derived().is_stopped();
    }
    key_type hash_key() const noexcept { return reinterpret_cast<key_type>(this); }
    buffer_wrap<buffer_type>& buffer() noexcept { return buffer_; }

protected:
    auto& rallocator() noexcept { return rallocator_; }
    auto& wallocator() noexcept { return wallocator_; }
    listener_t& listener() noexcept { return listener_; }
    std::atomic<state_t>& state() noexcept { return state_; }
    std::uint64_t life_id() const noexcept { return life_id_.load(); }
    void reset_life_id() noexcept { life_id_.fetch_add(1); }
    handler_memory<std::true_type, assizer<Args>> rallocator_;
    handler_memory<std::false_type, assizer<Args>> wallocator_;
    listener_t listener_;
    buffer_wrap<buffer_type> buffer_;
    std::atomic<state_t> state_ = state_t::stopped;
    std::shared_ptr<ecs_base> ecs_;
    std::atomic_bool stopped_ = true;
    bool reading_ = false;
    std::atomic<std::uint64_t> life_id_ = 0;
#ifndef NDEBUG
    std::atomic<int> post_send_counter_ = 0;
    std::atomic<int> post_recv_counter_ = 0;
#endif
};
}
