// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/client.hpp>
#include <arknet/base/detail/linear_buffer.hpp>
#include <arknet/udp/impl/udp_recv_op.hpp>
#include <arknet/udp/impl/udp_send_op.hpp>

namespace arknet::detail
{
struct template_args_udp_client : udp_tag
{
    static constexpr bool is_session = false;
    static constexpr bool is_client = true;
    static constexpr bool is_server = false;
    using socket_t = asio::ip::udp::socket;
    using buffer_t = arknet::linear_buffer;
    using send_data_t = std::string_view;
    using recv_data_t = std::string_view;
    static constexpr std::size_t allocator_storage_size = 256;
};

ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_CLIENT;

template <class Derived, class Args = template_args_udp_client>
class udp_client_impl_t : public client_impl_t<Derived, Args>,
                          public udp_send_op<Derived, Args>,
                          public udp_recv_op<Derived, Args>,
                          public udp_tag
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_CLIENT;
    using base = client_impl_t<Derived, Args>;

public:
    using super = base;
    using self = udp_client_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;
    using send_data_t = typename Args::send_data_t;
    using recv_data_t = typename Args::recv_data_t;

    explicit udp_client_impl_t(std::size_t initial = udp_frame_size, std::size_t maximum = max_buffer_size,
                               std::size_t workers = 1)
        : base(initial, maximum, workers)
    {
        this->set_connect_timeout(std::chrono::milliseconds(udp_connect_timeout));
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    udp_client_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : base(initial, maximum, std::forward<Scheduler>(scheduler))
    {
        this->set_connect_timeout(std::chrono::milliseconds(udp_connect_timeout));
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    explicit udp_client_impl_t(Scheduler&& scheduler)
        : udp_client_impl_t(udp_frame_size, max_buffer_size, std::forward<Scheduler>(scheduler))
    {
    }

    ~udp_client_impl_t() { stop(); }

    template <class Host, class Service, class... Options>
    bool start(Host&& host, Service&& service, Options&&... options)
    {
        return this->derived().template _do_connect<false>(
            std::forward<Host>(host), std::forward<Service>(service),
            ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }

    template <class Host, class Service, class... Options>
    bool async_start(Host&& host, Service&& service, Options&&... options)
    {
        return this->derived().template _do_connect<true>(std::forward<Host>(host), std::forward<Service>(service),
                                                          ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }

    void stop()
    {
        if (base::is_stopped())
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
                owner._stop_reconnect_timer();
                owner._do_disconnect(asio::error::operation_aborted, lifetime,
                                     defer_event{[&owner, lifetime = std::move(lifetime),
                                                  promise](event_queue_guard<Derived> guard) mutable
                                                 {
                                                     owner._do_stop(asio::error::operation_aborted, std::move(lifetime),
                                                                    defer_event{[promise](event_queue_guard<Derived>)
                                                                                { promise->set_value(); },
                                                                                std::move(guard)});
                                                 },
                                                 std::move(guard)});
            });
        if (!this->iopool().running_in_threads())
            completion.get();
        this->stop_iopool();
    }

    template <class Function, class... Object> Derived& bind_recv(Function&& function, Object&&... object)
    {
        this->listener_.bind(event_type::recv, observer_t<std::string_view>(std::forward<Function>(function),
                                                                            std::forward<Object>(object)...));
        return this->derived();
    }

    template <class Function, class... Object> Derived& bind_connect(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::connect, std::forward<Function>(function),
                                 std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_disconnect(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::disconnect, std::forward<Function>(function),
                                 std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_init(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::init, std::forward<Function>(function), std::forward<Object>(object)...);
    }

protected:
    template <bool Async, class Host, class Service, class Condition>
    bool _do_connect(Host&& host, Service&& service, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (this->buffer_.max_size() < udp_frame_size)
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
            [&owner, lifetime = owner.selfptr(), condition = std::move(condition), host = std::move(host_text),
             service = std::move(service_text), promise](event_queue_guard<Derived> guard) mutable
            {
                defer_event chain{[promise](event_queue_guard<Derived>) { promise->set_value(get_last_error()); },
                                  std::move(guard)};
                if (owner.state_ != state_t::stopped)
                {
                    set_last_error(asio::error::already_started);
                    return;
                }
                owner.io_->init_thread_id();
                owner.io_->regobj(&owner);
                owner.state_ = state_t::starting;
                owner.ecs_ = condition;
                owner.host_ = std::move(host);
                owner.port_ = std::move(service);
                owner.base::start();
                owner.buffer_.pre_size(udp_frame_size);
                owner._do_init(condition);
                owner.template _start_connect<Async>(std::move(lifetime), std::move(condition), std::move(chain));
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

    template <class Condition> void _do_init(std::shared_ptr<ecs_t<Condition>>&) {}

    template <class Condition, class Chain>
    void _handle_connect(const error_code& error, std::shared_ptr<Derived> lifetime,
                         std::shared_ptr<ecs_t<Condition>> condition, Chain chain)
    {
        this->derived()._done_connect(error, std::move(lifetime), std::move(condition), std::move(chain));
    }

    template <class Condition, class Chain>
    void _do_start(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition, Chain chain)
    {
        this->update_alive_time();
        this->reset_connect_time();
        this->derived()._start_recv(std::move(lifetime), std::move(condition), std::move(chain));
    }

    template <class Condition, class Chain>
    void _start_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition, Chain)
    {
        this->buffer_.consume(this->buffer_.size());
        this->derived()._post_recv(std::move(lifetime), std::move(condition));
    }

    template <class Chain>
    void _handle_disconnect(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        error_code ignored;
        this->socket().cancel(ignored);
        this->socket().close(ignored);
        base::_handle_disconnect(error, std::move(lifetime), std::move(chain));
    }

    template <class Chain> void _do_stop(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        this->derived()._post_stop(error, std::move(lifetime), std::move(chain));
    }
    template <class Chain> void _post_stop(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        base::stop();
        this->derived()._handle_stop(error, std::move(lifetime), std::move(chain));
    }
    template <class Chain> void _handle_stop(const error_code&, std::shared_ptr<Derived>, Chain) {}

    template <class Data, class Completion> bool _do_send(Data& data, Completion&& completion)
    {
        return this->derived()._udp_send(data, std::forward<Completion>(completion));
    }
    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition)
    {
        this->derived()._udp_post_recv(std::move(lifetime), std::move(condition));
    }
    template <class Condition>
    void _handle_recv(const error_code& error, std::size_t count, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<ecs_t<Condition>> condition)
    {
        this->derived()._udp_handle_recv(error, count, std::move(lifetime), std::move(condition));
    }

    void _fire_init() { this->listener_.notify(event_type::init); }
    template <class Condition>
    void _fire_recv(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&, std::string_view bytes)
    {
        this->listener_.notify(event_type::recv, detail::call_data_filter_before_recv(this->derived(), bytes));
    }
    template <class Condition> void _fire_connect(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&)
    {
        is_disconnect_called_ = false;
        this->listener_.notify(event_type::connect);
    }
    void _fire_disconnect(std::shared_ptr<Derived>&)
    {
        is_disconnect_called_ = true;
        this->listener_.notify(event_type::disconnect);
    }

    bool is_disconnect_called_ = false;

private:
    template <class Function, class... Object>
    Derived& bind_notification(event_type event, Function&& function, Object&&... object)
    {
        this->listener_.bind(event, observer_t<>(std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }
};
}

namespace arknet
{
using udp_client_args = detail::template_args_udp_client;
template <class Derived, class Args> using udp_client_impl_t = detail::udp_client_impl_t<Derived, Args>;
template <class Derived> class udp_client_t : public detail::udp_client_impl_t<Derived>
{
public:
    using detail::udp_client_impl_t<Derived>::udp_client_impl_t;
};
class udp_client : public udp_client_t<udp_client>
{
public:
    using udp_client_t<udp_client>::udp_client_t;
};
}
