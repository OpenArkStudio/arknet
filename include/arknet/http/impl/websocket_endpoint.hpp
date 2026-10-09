// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/http/impl/ws_stream_cp.hpp>
#include <arknet/http/impl/ws_send_op.hpp>
namespace arknet::detail
{
template <class Derived, class Transport, class Args>
class websocket_endpoint_impl : public Transport, public ws_stream_cp<Derived, Args>, public ws_send_op<Derived, Args>
{
    static constexpr bool secure = std::is_base_of_v<ssl_stream_tag, Transport>;
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_CLIENT;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION;

public:
    using super = Transport;
    using args_type = Args;
    using body_type = typename Args::body_t;
    using buffer_type = typename Args::buffer_t;
    using ws_stream_comp = ws_stream_cp<Derived, Args>;
    template <class... Construction>
    explicit websocket_endpoint_impl(Construction&&... construction)
        : Transport(std::forward<Construction>(construction)...)
    {
    }
    websocket_endpoint_impl(const websocket_endpoint_impl&) = delete;
    websocket_endpoint_impl& operator=(const websocket_endpoint_impl&) = delete;
    ~websocket_endpoint_impl()
    {
        if constexpr (Args::is_client)
            this->stop();
    }
    auto& stream() noexcept { return this->ws_stream(); }
    const auto& stream() const noexcept { return this->ws_stream(); }
    void destroy()
    {
        this->ws_stream_.reset();
        Transport::destroy();
    }
    auto& get_upgrade_request() noexcept
        requires(Args::is_session)
    {
        return upgrade_message_;
    }
    const auto& get_upgrade_request() const noexcept
        requires(Args::is_session)
    {
        return upgrade_message_;
    }
    auto& get_upgrade_response() noexcept
        requires(Args::is_client)
    {
        return upgrade_message_;
    }
    const auto& get_upgrade_response() const noexcept
        requires(Args::is_client)
    {
        return upgrade_message_;
    }
    const std::string& get_upgrade_target() const noexcept
        requires(Args::is_client)
    {
        return upgrade_target_;
    }
    template <class Target>
    Derived& set_upgrade_target(Target&& target)
        requires(Args::is_client)
    {
        upgrade_target_ = detail::to_string(std::forward<Target>(target));
        return this->derived();
    }
    template <class Host, class Port, class... Options>
    bool start(Host&& host, Port&& port, Options&&... options)
        requires(Args::is_client)
    {
        return connect<false>(std::forward<Host>(host), std::forward<Port>(port), std::forward<Options>(options)...);
    }
    template <class Host, class Port, class... Options>
    bool async_start(Host&& host, Port&& port, Options&&... options)
        requires(Args::is_client)
    {
        return connect<true>(std::forward<Host>(host), std::forward<Port>(port), std::forward<Options>(options)...);
    }
    template <class F, class... Bound>
    Derived& bind_upgrade(F&& callback, Bound&&... bound)
        requires(Args::is_client)
    {
        this->listener_.bind(event_type::upgrade,
                             observer_t<>{std::forward<F>(callback), std::forward<Bound>(bound)...});
        return this->derived();
    }

protected:
    using Transport::start;
    auto& upgrade_stream() noexcept { return transport(); }
    const auto& upgrade_stream() const noexcept { return transport(); }
    template <class Condition>
    void _do_init(std::shared_ptr<ecs_t<Condition>>& policy)
        requires(Args::is_client)
    {
        Transport::_do_init(policy);
        this->_ws_init(policy, transport());
    }
    template <class Condition>
    void _do_init(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>& policy)
        requires(Args::is_session)
    {
        Transport::_do_init(lifetime, policy);
        this->_ws_init(policy, transport());
    }
    template <class Chain> void _post_shutdown(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        this->_ws_stop(
            lifetime,
            defer_event{[this, ec, lifetime, event = chain.move_event()](event_queue_guard<Derived> guard) mutable {
                            Transport::_post_shutdown(ec, std::move(lifetime),
                                                      defer_event{std::move(event), std::move(guard)});
                        },
                        chain.move_guard()});
    }
    template <class Condition, class Chain>
    void _handle_connect(const error_code& ec, std::shared_ptr<Derived> lifetime,
                         std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        if constexpr (secure)
            Transport::_handle_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
        else
        {
            set_last_error(ec);
            if (ec)
                this->derived()._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
            else
                begin_upgrade(std::move(lifetime), std::move(policy), std::move(chain));
        }
    }
    template <class Condition, class Chain>
    void _handle_handshake(const error_code& ec, std::shared_ptr<Derived> lifetime,
                           std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
        requires(secure)
    {
        auto notify =
            [this, ec, lifetime = std::move(lifetime), policy = std::move(policy), chain = std::move(chain)]() mutable
        {
            set_last_error(ec);
            this->derived()._fire_handshake(lifetime);
            if (ec)
            {
                if constexpr (Args::is_session)
                    this->derived()._do_disconnect(ec, std::move(lifetime), std::move(chain));
                else
                    this->derived()._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
            }
            else
                begin_upgrade(std::move(lifetime), std::move(policy), std::move(chain));
        };
        if constexpr (Args::is_session)
            this->sessions_.dispatch(std::move(notify));
        else
            notify();
    }
    template <class Data, class Completion> bool _do_send(Data& data, Completion&& completion)
    {
        return this->derived()._ws_send(data, std::forward<Completion>(completion));
    }
    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy)
    {
        this->derived()._ws_post_recv(std::move(lifetime), std::move(policy));
    }
    template <class Condition>
    void _handle_recv(const error_code& ec, std::size_t size, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<ecs_t<Condition>> policy)
    {
        this->derived()._ws_handle_recv(ec, size, std::move(lifetime), std::move(policy));
    }
    void _fire_upgrade(std::shared_ptr<Derived>& lifetime)
    {
        if constexpr (Args::is_session)
        {
            ARKNET_ASSERT(this->sessions_.io().running_in_this_thread());
            this->listener_.notify(event_type::upgrade, lifetime);
        }
        else
        {
            ARKNET_ASSERT(this->io_->running_in_this_thread());
            this->listener_.notify(event_type::upgrade);
        }
    }

private:
    auto& transport() noexcept
    {
        if constexpr (secure)
            return this->ssl_stream();
        else
            return this->socket();
    }
    const auto& transport() const noexcept
    {
        if constexpr (secure)
            return this->ssl_stream();
        else
            return this->socket();
    }
    template <bool Async, class Host, class Port> bool connect(Host&& host, Port&& port)
    {
        return this->derived().template _do_connect<Async>(std::forward<Host>(host), std::forward<Port>(port),
                                                           ecs_helper::make_ecs('0'));
    }
    template <bool Async, class Host, class Port, class First, class... Options>
    bool connect(Host&& host, Port&& port, First&& first, Options&&... options)
    {
        if constexpr (detail::can_convert_to_string<std::remove_cvref_t<First>>::value)
        {
            this->derived().set_upgrade_target(std::forward<First>(first));
            return this->derived().template _do_connect<Async>(
                std::forward<Host>(host), std::forward<Port>(port),
                ecs_helper::make_ecs('0', std::forward<Options>(options)...));
        }
        else
            return this->derived().template _do_connect<Async>(
                std::forward<Host>(host), std::forward<Port>(port),
                ecs_helper::make_ecs('0', std::forward<First>(first), std::forward<Options>(options)...));
    }
    template <class Condition, class Chain>
    void begin_upgrade(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        this->derived().dispatch(
            [this, lifetime = std::move(lifetime), policy = std::move(policy), chain = std::move(chain)]() mutable
            {
                this->_ws_start(lifetime, policy, transport());
                if constexpr (Args::is_session)
                    this->derived()._post_read_upgrade_request(std::move(lifetime), std::move(policy),
                                                               std::move(chain));
                else
                {
                    this->derived()._post_control_callback(lifetime, policy);
                    this->derived()._post_upgrade(std::move(lifetime), std::move(policy), upgrade_message_,
                                                  std::move(chain));
                }
            });
    }
    std::conditional_t<Args::is_session, websocket::request_type, websocket::response_type> upgrade_message_;
    std::string upgrade_target_ = "/";
};

template <class Derived, class Transport, class Session> class websocket_server_impl : public Transport
{
public:
    template <class... Construction>
    explicit websocket_server_impl(Construction&&... construction)
        : Transport(std::forward<Construction>(construction)...)
    {
    }
    ~websocket_server_impl() { this->stop(); }
    template <class Host, class Port, class... Options> bool start(Host&& host, Port&& port, Options&&... options)
    {
        return this->derived()._do_start(std::forward<Host>(host), std::forward<Port>(port),
                                         ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }
    template <class F, class... Bound> Derived& bind_upgrade(F&& callback, Bound&&... bound)
    {
        this->listener_.bind(event_type::upgrade, observer_t<std::shared_ptr<Session>&>{std::forward<F>(callback),
                                                                                        std::forward<Bound>(bound)...});
        return this->derived();
    }
};
}
