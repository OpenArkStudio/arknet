// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/tcp/tcps_session.hpp>
namespace arknet::detail
{
struct template_args_tcps_server
{
    static constexpr bool is_session = false, is_client = false, is_server = true;
};
template <class Derived, class Session>
class tcps_server_impl_t : public ssl_context_cp<Derived, template_args_tcps_server>,
                           public tcp_server_impl_t<Derived, Session>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;

public:
    using super = tcp_server_impl_t<Derived, Session>;
    using self = tcps_server_impl_t;
    using session_type = Session;
    template <class... Construction>
    explicit tcps_server_impl_t(asio::ssl::context::method method, Construction&&... construction)
        : ssl_context_cp<Derived, template_args_tcps_server>(method), super(std::forward<Construction>(construction)...)
    {
    }
    template <class... Construction>
    explicit tcps_server_impl_t(Construction&&... construction)
        : tcps_server_impl_t(ARKNET_DEFAULT_SSL_METHOD, std::forward<Construction>(construction)...)
    {
    }
    ~tcps_server_impl_t() { this->stop(); }
    template <class F, class... Bound> Derived& bind_handshake(F&& callback, Bound&&... bound)
    {
        this->listener_.bind(event_type::handshake, observer_t<std::shared_ptr<Session>&>{
                                                        std::forward<F>(callback), std::forward<Bound>(bound)...});
        return this->derived();
    }

protected:
    template <class Host, class Port, class Condition>
    bool _do_start(Host&& host, Port&& port, std::shared_ptr<ecs_t<Condition>> policy)
    {
        auto ec = this->identity_error_;
        if (!ec)
            ec = this->_check_tls_identity();
        if (ec)
        {
            set_last_error(ec);
            return false;
        }
        return super::_do_start(std::forward<Host>(host), std::forward<Port>(port), std::move(policy));
    }
    template <class... Construction> std::shared_ptr<Session> _make_session(Construction&&... construction)
    {
        return super::_make_session(std::forward<Construction>(construction)..., *this);
    }
};
}
namespace arknet
{
template <class Derived, class Session> using tcps_server_impl_t = detail::tcps_server_impl_t<Derived, Session>;
template <class Session> class tcps_server_t : public detail::tcps_server_impl_t<tcps_server_t<Session>, Session>
{
public:
    using detail::tcps_server_impl_t<tcps_server_t<Session>, Session>::tcps_server_impl_t;
};
using tcps_server = tcps_server_t<tcps_session>;
}
#endif
