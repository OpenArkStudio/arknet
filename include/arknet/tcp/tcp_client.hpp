// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/base/client.hpp>
#include <arknet/tcp/impl/tcp_keepalive_cp.hpp>
#include <arknet/tcp/impl/tcp_send_op.hpp>
#include <arknet/tcp/impl/tcp_recv_op.hpp>

namespace arknet::detail
{
struct template_args_tcp_client : tcp_tag
{
    static constexpr bool is_session = false, is_client = true, is_server = false;
    using socket_t = asio::ip::tcp::socket;
    using buffer_t = asio::streambuf;
    using send_data_t = std::string_view;
    using recv_data_t = std::string_view;
};
ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_CLIENT;

template <class Derived, class Args = template_args_tcp_client>
class tcp_client_impl_t : public client_impl_t<Derived, Args>,
                          public tcp_keepalive_cp<Derived, Args>,
                          public tcp_send_op<Derived, Args>,
                          public tcp_recv_op<Derived, Args>,
                          public tcp_tag
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_CLIENT;

public:
    using super = client_impl_t<Derived, Args>;
    using self = tcp_client_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;
    using send_data_t = typename Args::send_data_t;
    using recv_data_t = typename Args::recv_data_t;

    explicit tcp_client_impl_t(std::size_t initial = tcp_frame_size, std::size_t maximum = max_buffer_size,
                               std::size_t workers = 1)
        : super(initial, maximum, workers)
    {
        configure();
    }
    template <class Scheduler>
        requires(!std::is_integral_v<std::remove_cvref_t<Scheduler>>)
    explicit tcp_client_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : super(initial, maximum, std::forward<Scheduler>(scheduler))
    {
        configure();
    }
    template <class Scheduler>
        requires(!std::is_integral_v<std::remove_cvref_t<Scheduler>>)
    explicit tcp_client_impl_t(Scheduler&& scheduler)
        : tcp_client_impl_t(tcp_frame_size, max_buffer_size, std::forward<Scheduler>(scheduler))
    {
    }
    ~tcp_client_impl_t() { stop(); }

    template <class Host, class Port, class... Options> bool start(Host&& host, Port&& port, Options&&... options)
    {
        return this->derived().template _do_connect<false>(
            std::forward<Host>(host), std::forward<Port>(port),
            ecs_helper::make_ecs(asio::transfer_at_least(1), std::forward<Options>(options)...));
    }
    template <class Host, class Port, class... Options> bool async_start(Host&& host, Port&& port, Options&&... options)
    {
        return this->derived().template _do_connect<true>(
            std::forward<Host>(host), std::forward<Port>(port),
            ecs_helper::make_ecs(asio::transfer_at_least(1), std::forward<Options>(options)...));
    }

    void stop()
    {
        if (this->is_iopool_stopped())
            return;
        if (this->io_->context().stopped() && super::is_stopped())
        {
            this->stop_iopool();
            return;
        }
        auto& owner = this->derived();
        owner.io_->unregobj(&owner);
        owner.dispatch([&owner, lifetime = owner.selfptr()] { owner._arm_stop_deadline(); });
        auto completion = std::make_shared<std::promise<void>>();
        auto result = completion->get_future();
        owner.post_event(
            [&owner, lifetime = owner.selfptr(), completion](event_queue_guard<Derived> guard) mutable
            {
                owner._stop_reconnect_timer();
                owner._do_disconnect(asio::error::operation_aborted, lifetime,
                                     defer_event{[&owner, lifetime, completion](event_queue_guard<Derived> next) mutable
                                                 {
                                                     owner._do_stop(asio::error::operation_aborted, std::move(lifetime),
                                                                    defer_event{[completion](event_queue_guard<Derived>)
                                                                                { completion->set_value(); },
                                                                                std::move(next)});
                                                 },
                                                 std::move(guard)});
            });
        if (!this->iopool().running_in_threads())
            result.get();
        this->stop_iopool();
    }

    template <class F, class... Bound> Derived& bind_recv(F&& callback, Bound&&... bound)
    {
        return bind<std::string_view>(event_type::recv, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template <class F, class... Bound> Derived& bind_connect(F&& callback, Bound&&... bound)
    {
        return bind<>(event_type::connect, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template <class F, class... Bound> Derived& bind_disconnect(F&& callback, Bound&&... bound)
    {
        return bind<>(event_type::disconnect, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template <class F, class... Bound> Derived& bind_init(F&& callback, Bound&&... bound)
    {
        return bind<>(event_type::init, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }

protected:
    template <bool Async, class Host, class Port, class Condition>
    bool _do_connect(Host&& host, Port&& port, std::shared_ptr<ecs_t<Condition>> policy)
    {
        const auto hostname = detail::to_string(std::forward<Host>(host));
        const auto service = detail::to_string(std::forward<Port>(port));
        this->start_iopool();
        if (!this->is_iopool_started())
        {
            set_last_error(asio::error::operation_aborted);
            return false;
        }
        auto completion = std::make_shared<std::promise<error_code>>();
        auto result = completion->get_future();
        auto& owner = this->derived();
        owner.post_event(
            [this, lifetime = owner.selfptr(), policy = std::move(policy), hostname, service,
             completion](event_queue_guard<Derived> guard) mutable
            {
                auto& owner = this->derived();
                defer_event finish{[completion](event_queue_guard<Derived>)
                                   { completion->set_value(get_last_error()); }, std::move(guard)};
                auto expected = state_t::stopped;
                if (!owner.state_.compare_exchange_strong(expected, state_t::starting))
                {
                    set_last_error(asio::error::already_started);
                    return;
                }
                owner.io_->init_thread_id();
                owner.io_->regobj(&owner);
                owner.ecs_ = policy;
                owner.host_ = hostname;
                owner.port_ = service;
#ifndef NDEBUG
                this->is_stop_reconnect_timer_called_ = false;
                this->is_post_reconnect_timer_called_ = false;
                this->is_stop_connect_timeout_timer_called_ = false;
                is_disconnect_called_ = false;
#endif
                clear_last_error();
                super::start();
                owner._do_init(policy);
                owner.template _start_connect<Async>(std::move(lifetime), std::move(policy), std::move(finish));
            });
        if constexpr (!Async)
        {
            if (!this->iopool().running_in_threads())
            {
                set_last_error(result.get());
                return !get_last_error();
            }
        }
        set_last_error(asio::error::in_progress);
        return Async || owner.is_started();
    }

    template <class Condition> void _do_init(std::shared_ptr<ecs_t<Condition>>&) noexcept
    {
        dgram_ = std::is_same_v<typename ecs_t<Condition>::condition_lowest_type, use_dgram_t>;
    }
    template <class Condition, class Chain>
    void _do_start(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        this->derived().reset_connect_time();
        this->derived().update_alive_time();
        this->derived()._start_recv(std::move(lifetime), std::move(policy), std::move(chain));
    }
    template <class Chain> void _handle_disconnect(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        close_socket();
        super::_handle_disconnect(ec, std::move(lifetime), std::move(chain));
    }
    template <class Chain> void _do_stop(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        this->derived()._post_stop(ec, std::move(lifetime), std::move(chain));
    }
    template <class Chain> void _post_stop(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        this->derived().disp_event(
            [this, ec, lifetime = std::move(lifetime),
             event = chain.move_event()](event_queue_guard<Derived> guard) mutable
            {
                set_last_error(ec);
                super::stop();
                this->derived()._handle_stop(ec, std::move(lifetime), defer_event{std::move(event), std::move(guard)});
            },
            chain.move_guard());
    }
    template <class Chain> void _handle_stop(const error_code&, std::shared_ptr<Derived>, Chain)
    {
        ARKNET_ASSERT(this->state_ == state_t::stopped);
    }
    template <class Condition, class Chain>
    void _start_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        this->derived().dispatch(
            [this, lifetime = std::move(lifetime), policy = std::move(policy), chain = std::move(chain)]() mutable
            {
                if constexpr (!std::is_same_v<typename ecs_t<Condition>::condition_lowest_type, hook_buffer_t>)
                    this->buffer().consume(this->buffer().size());
                this->derived()._post_recv(std::move(lifetime), std::move(policy));
            });
    }
    template <class Data, class F> bool _do_send(Data& data, F&& completion)
    {
        return this->derived()._tcp_send(data, std::forward<F>(completion));
    }
    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy)
    {
        this->derived()._tcp_post_recv(std::move(lifetime), std::move(policy));
    }
    template <class Condition>
    void _handle_recv(const error_code& ec, std::size_t size, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<ecs_t<Condition>> policy)
    {
        this->derived()._tcp_handle_recv(ec, size, std::move(lifetime), std::move(policy));
    }
    void _fire_init()
    {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
        this->listener_.notify(event_type::init);
    }
    template <class Condition>
    void _fire_recv(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&, std::string_view data)
    {
        this->listener_.notify(event_type::recv, detail::call_data_filter_before_recv(this->derived(), data));
    }
    template <class Condition> void _fire_connect(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&)
    {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
#ifndef NDEBUG
        ARKNET_ASSERT(!is_disconnect_called_);
#endif
        this->listener_.notify(event_type::connect);
    }
    void _fire_disconnect(std::shared_ptr<Derived>&)
    {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
#ifndef NDEBUG
        is_disconnect_called_ = true;
#endif
        this->listener_.notify(event_type::disconnect);
    }
    bool dgram_ = false;
#ifndef NDEBUG
    bool is_disconnect_called_ = false;
#endif
private:
    void configure() { this->set_connect_timeout(std::chrono::milliseconds(tcp_connect_timeout)); }
    template <class... Signature, class F, class... Bound>
    Derived& bind(event_type event, F&& callback, Bound&&... bound)
    {
        this->listener_.bind(event, observer_t<Signature...>{std::forward<F>(callback), std::forward<Bound>(bound)...});
        return this->derived();
    }
    void close_socket()
    {
        error_code ignored, option_error;
        asio::socket_base::linger linger;
        auto& socket = this->socket();
        if (!socket.is_open())
            return;
        socket.lowest_layer().get_option(linger, option_error);
        if (!option_error && !(linger.enabled() && linger.timeout() == 0))
            socket.shutdown(asio::socket_base::shutdown_both, ignored);
        socket.cancel(ignored);
        socket.close(ignored);
    }
};
}

namespace arknet
{
using tcp_client_args = detail::template_args_tcp_client;
template <class Derived, class Args> using tcp_client_impl_t = detail::tcp_client_impl_t<Derived, Args>;
template <class Derived> class tcp_client_t : public detail::tcp_client_impl_t<Derived>
{
public:
    using detail::tcp_client_impl_t<Derived>::tcp_client_impl_t;
};
class tcp_client : public tcp_client_t<tcp_client>
{
public:
    using tcp_client_t::tcp_client_t;
};
}
#if defined(ARKNET_INCLUDE_RATE_LIMIT)
#include <arknet/tcp/tcp_stream.hpp>
namespace arknet
{
struct tcp_rate_client_args : tcp_client_args
{
    using socket_t = tcp_stream<simple_rate_policy>;
};
template <class Derived> class tcp_rate_client_t : public tcp_client_impl_t<Derived, tcp_rate_client_args>
{
public:
    using tcp_client_impl_t<Derived, tcp_rate_client_args>::tcp_client_impl_t;
};
class tcp_rate_client : public tcp_rate_client_t<tcp_rate_client>
{
public:
    using tcp_rate_client_t::tcp_rate_client_t;
};
}
#endif
