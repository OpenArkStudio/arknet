// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/io_pool.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/detail/linear_buffer.hpp>
#include <arknet/base/detail/object.hpp>
#include <arknet/base/impl/io_context_cp.hpp>
#include <arknet/base/impl/thread_id_cp.hpp>
#include <arknet/base/impl/alive_time_cp.hpp>
#include <arknet/base/impl/user_data_cp.hpp>
#include <arknet/base/impl/socket_cp.hpp>
#include <arknet/base/impl/user_timer_cp.hpp>
#include <arknet/base/impl/post_cp.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
#include <arknet/base/impl/condition_event_cp.hpp>
#include <arknet/base/impl/connect_cp.hpp>
#include <arknet/udp/impl/udp_send_cp.hpp>
#include <arknet/udp/impl/udp_send_op.hpp>

namespace arknet::detail
{
struct template_args_udp_cast : udp_tag
{
    using socket_t = asio::ip::udp::socket;
    using buffer_t = arknet::linear_buffer;
    static constexpr std::size_t function_storage_size = 88;
    static constexpr std::size_t allocator_storage_size = 256;
};

ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_CLIENT;

template <class Derived, class Args = template_args_udp_cast>
class udp_cast_impl_t : public object_t<Derived>,
                        public iopool_cp<Derived, Args>,
                        public io_context_cp<Derived, Args>,
                        public thread_id_cp<Derived, Args>,
                        public event_queue_cp<Derived, Args>,
                        public user_data_cp<Derived, Args>,
                        public alive_time_cp<Derived, Args>,
                        public socket_cp<Derived, Args>,
                        public user_timer_cp<Derived, Args>,
                        public post_cp<Derived, Args>,
                        public condition_event_cp<Derived, Args>,
                        public udp_send_cp<Derived, Args>,
                        public udp_send_op<Derived, Args>,
                        public connect_cp_member_variables<Derived, Args, false>,
                        public udp_tag,
                        public cast_tag
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_CLIENT;
    using scheduler_base = iopool_cp<Derived, Args>;

public:
    using super = object_t<Derived>;
    using self = udp_cast_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;

    explicit udp_cast_impl_t(std::size_t initial = udp_frame_size, std::size_t maximum = max_buffer_size,
                             std::size_t workers = 1)
        : scheduler_base(workers), io_context_cp<Derived, Args>(scheduler_base::_get_io(0)),
          socket_cp<Derived, Args>(scheduler_base::_get_io(0)->executor()), buffer_(initial, maximum)
    {
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    udp_cast_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : scheduler_base(std::forward<Scheduler>(scheduler)), io_context_cp<Derived, Args>(scheduler_base::_get_io(0)),
          socket_cp<Derived, Args>(scheduler_base::_get_io(0)->executor()), buffer_(initial, maximum)
    {
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    explicit udp_cast_impl_t(Scheduler&& scheduler)
        : udp_cast_impl_t(udp_frame_size, max_buffer_size, std::forward<Scheduler>(scheduler))
    {
    }
    ~udp_cast_impl_t() { stop(); }

    template <class Host, class Service, class... Options>
    bool start(Host&& host, Service&& service, Options&&... options)
    {
        return this->derived().template _do_start<false>(std::forward<Host>(host), std::forward<Service>(service),
                                                         ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }
    template <class Host, class Service, class... Options>
    bool async_start(Host&& host, Service&& service, Options&&... options)
    {
        return this->derived().template _do_start<true>(std::forward<Host>(host), std::forward<Service>(service),
                                                        ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }

    void stop()
    {
        if (is_stopped())
        {
            this->stop_iopool();
            return;
        }
        if (this->is_iopool_stopped())
            return;
        auto& owner = this->derived();
        owner.io_->unregobj(&owner);
        auto promise = std::make_shared<std::promise<void>>();
        auto completion = promise->get_future();
        owner.post_event(
            [&owner, lifetime = owner.selfptr(), promise](event_queue_guard<Derived> guard) mutable
            {
                if (owner.is_stopped())
                {
                    promise->set_value();
                    return;
                }
                owner.stop_waiters_.push_back(promise);
                owner.stop_guard_.emplace(std::move(guard));
                owner._do_stop(asio::error::operation_aborted, std::move(lifetime));
            });
        if (!this->iopool().running_in_threads())
            completion.get();
        this->stop_iopool();
    }
    void request_stop()
    {
        if (this->derived().is_stopped())
            return;
        this->derived().post([this, lifetime = this->selfptr()] { this->derived().stop(); });
    }
    bool wait_stopped()
    {
        if (this->iopool().running_in_threads())
        {
            set_last_error(asio::error::operation_not_supported);
            return false;
        }
        this->derived().stop();
        return is_stopped();
    }
    bool is_started() const { return state_ == state_t::started && this->socket().is_open(); }
    bool is_stopped() const { return state_ == state_t::stopped; }
    void destroy()
    {
        this->socket_.reset();
        this->io_.reset();
        listener_.clear();
        this->destroy_iopool();
    }

    template <class Function, class... Object> Derived& bind_recv(Function&& function, Object&&... object)
    {
        listener_.bind(event_type::recv, observer_t<asio::ip::udp::endpoint&, std::string_view>(
                                             std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }
    template <class Function, class... Object> Derived& bind_init(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::init, std::forward<Function>(function), std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_start(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::start, std::forward<Function>(function), std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_stop(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::stop, std::forward<Function>(function), std::forward<Object>(object)...);
    }
    buffer_wrap<buffer_type>& buffer() noexcept { return buffer_; }

protected:
    template <bool Async, class Host, class Service, class Condition>
    bool _do_start(Host&& host, Service&& service, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (buffer_.max_size() < udp_frame_size)
        {
            set_last_error(asio::error::message_size);
            return false;
        }
        auto host_text = detail::to_string(std::forward<Host>(host));
        auto service_text = detail::to_string(std::forward<Service>(service));
        this->start_iopool();
        if (!this->is_iopool_started())
        {
            set_last_error(asio::error::operation_aborted);
            return false;
        }
        auto promise = std::make_shared<std::promise<error_code>>();
        auto completion = promise->get_future();
        auto& owner = this->derived();
        owner.post_event(
            [&owner, lifetime = owner.selfptr(), condition = std::move(condition), promise, host = std::move(host_text),
             service = std::move(service_text)](event_queue_guard<Derived>) mutable
            {
                if (owner.state_ != state_t::stopped)
                {
                    promise->set_value(asio::error::already_started);
                    return;
                }
                owner.io_->init_thread_id();
                owner.io_->regobj(&owner);
                owner.state_ = state_t::starting;
                owner.host_ = std::move(host);
                owner.port_ = std::move(service);
                owner.ecs_ = condition;
                error_code error;
                asio::ip::udp::resolver resolver(owner.io_->executor());
                auto endpoints = resolver.resolve(owner.host_, owner.port_, asio::ip::resolver_base::passive, error);
                if (!error && endpoints.empty())
                    error = asio::error::host_not_found;
                if (!error)
                {
                    auto endpoint = endpoints.begin()->endpoint();
                    owner.socket().open(endpoint.protocol(), error);
                    if (!error)
                    {
                        error_code ignored;
                        owner.socket().set_option(asio::socket_base::reuse_address(true), ignored);
                        clear_last_error();
                        owner._fire_init();
                        owner.socket().bind(endpoint, error);
                    }
                }
                owner._handle_start(error, std::move(lifetime), std::move(condition));
                promise->set_value(error);
            });
        if constexpr (Async)
        {
            set_last_error(asio::error::in_progress);
            return true;
        }
        if (this->iopool().running_in_threads())
        {
            set_last_error(asio::error::in_progress);
            return owner.is_started();
        }
        set_last_error(completion.get());
        return !get_last_error();
    }
    template <class Condition>
    void _handle_start(const error_code& error, std::shared_ptr<Derived> lifetime,
                       std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (!error)
        {
            reset_life_id();
            state_ = state_t::started;
        }
        set_last_error(error);
        this->derived()._fire_start();
        if (error)
            this->derived()._do_stop(error, std::move(lifetime));
        else
            this->derived()._post_recv(std::move(lifetime), std::move(condition));
    }
    template <class Chain = defer_event<void, Derived>>
    void _do_disconnect(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain = {})
    {
        this->derived().dispatch([this, error, lifetime = std::move(lifetime), chain = std::move(chain)]() mutable
                                 { this->derived()._do_stop(error, std::move(lifetime)); });
    }
    void _do_stop(const error_code& error, std::shared_ptr<Derived> lifetime)
    {
        if (state_ == state_t::stopping || state_ == state_t::stopped)
            return;
        state_ = state_t::stopping;
        stop_error_ = error;
        this->_dispatch_stop_all_timers();
        this->_dispatch_stop_all_timed_events();
        this->notify_all_condition_events();
        error_code ignored;
        this->socket().cancel(ignored);
        this->socket().close(ignored);
        if (!read_pending_)
            this->derived()._handle_stop(error, std::move(lifetime));
    }
    void _handle_stop(const error_code& error, std::shared_ptr<Derived>)
    {
        auto queued_stop = std::move(stop_guard_);
        stop_guard_.reset();
        state_ = state_t::stopped;
        reset_life_id();
        buffer_.consume(buffer_.size());
        this->user_data_.reset();
        ecs_.reset();
        set_last_error(error);
        this->derived()._fire_stop();
        auto waiters = std::exchange(stop_waiters_, {});
        for (const auto& waiter : waiters)
            waiter->set_value();
    }

    template <class Data, class Completion>
    bool _do_send(const asio::ip::udp::endpoint& target, Data& data, Completion&& completion)
    {
        return this->derived()._udp_send_to(target, data, std::forward<Completion>(completion));
    }
    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (!is_started())
            return;
        read_pending_ = true;
        this->socket().async_receive_from(
            buffer_.prepare(udp_frame_size), remote_endpoint_,
            make_allocator(read_memory_,
                           [this, lifetime = std::move(lifetime),
                            condition = std::move(condition)](const error_code& error, std::size_t count) mutable
                           {
                               read_pending_ = false;
                               this->derived()._handle_recv(error, count, std::move(lifetime), std::move(condition));
                           }));
    }
    template <class Condition>
    void _handle_recv(const error_code& error, std::size_t count, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (state_ == state_t::stopping)
        {
            this->derived()._handle_stop(stop_error_, std::move(lifetime));
            return;
        }
        set_last_error(error);
        if (!is_started())
            return;
        if (error == asio::error::operation_aborted)
        {
            this->derived()._do_stop(error, std::move(lifetime));
            return;
        }
        if (!error)
        {
            buffer_.commit(count);
            this->update_alive_time();
            auto bytes = buffer_.data();
            this->derived()._fire_recv(lifetime, condition,
                                       std::string_view(static_cast<const char*>(bytes.data()), count));
            buffer_.consume(buffer_.size());
        }
        this->derived()._post_recv(std::move(lifetime), std::move(condition));
    }
    template <class Condition>
    void _fire_recv(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&, std::string_view bytes)
    {
        listener_.notify(event_type::recv, remote_endpoint_,
                         detail::call_data_filter_before_recv(this->derived(), bytes));
    }
    void _fire_init() { listener_.notify(event_type::init); }
    void _fire_start() { listener_.notify(event_type::start); }
    void _fire_stop() { listener_.notify(event_type::stop); }
    auto& rallocator() noexcept { return read_memory_; }
    auto& wallocator() noexcept { return write_memory_; }
    std::uint64_t life_id() const noexcept { return life_id_.load(); }
    void reset_life_id() noexcept { ++life_id_; }

    handler_memory<std::true_type, assizer<Args>> read_memory_;
    handler_memory<std::false_type, assizer<Args>> write_memory_;
    listener_t listener_;
    buffer_wrap<buffer_type> buffer_;
    std::atomic<state_t> state_{state_t::stopped};
    std::shared_ptr<ecs_base> ecs_;
    std::atomic<std::uint64_t> life_id_{0};
    asio::ip::udp::endpoint remote_endpoint_;
    bool read_pending_ = false;
    error_code stop_error_;
    std::vector<std::shared_ptr<std::promise<void>>> stop_waiters_;
    std::optional<event_queue_guard<Derived>> stop_guard_;

private:
    template <class Function, class... Object>
    Derived& bind_notification(event_type event, Function&& function, Object&&... object)
    {
        listener_.bind(event, observer_t<>(std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }
};
}

namespace arknet
{
using udp_cast_args = detail::template_args_udp_cast;
template <class Derived, class Args> using udp_cast_impl_t = detail::udp_cast_impl_t<Derived, Args>;
template <class Derived> class udp_cast_t : public detail::udp_cast_impl_t<Derived>
{
public:
    using detail::udp_cast_impl_t<Derived>::udp_cast_impl_t;
};
class udp_cast : public udp_cast_t<udp_cast>
{
public:
    using udp_cast_t<udp_cast>::udp_cast_t;
};
}
