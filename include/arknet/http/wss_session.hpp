// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcps_session.hpp>
#include <arknet/http/ws_session.hpp>
namespace arknet::detail
{
struct template_args_wss_session : template_args_ws_session
{
    using stream_t = websocket::stream<asio::ssl::stream<socket_t&>&>;
};
template <class Derived, class Args = template_args_wss_session>
class wss_session_impl_t : public websocket_endpoint_impl<Derived, tcps_session_impl_t<Derived, Args>, Args>
{
public:
    template <class... Construction>
    explicit wss_session_impl_t(Construction&&... construction)
        : websocket_endpoint_impl<Derived, tcps_session_impl_t<Derived, Args>, Args>(
              std::forward<Construction>(construction)...)
    {
    }
};
}
namespace arknet
{
using wss_session_args = detail::template_args_wss_session;
template <class Derived, class Args> using wss_session_impl_t = detail::wss_session_impl_t<Derived, Args>;
template <class Derived> class wss_session_t : public detail::wss_session_impl_t<Derived>
{
public:
    using detail::wss_session_impl_t<Derived>::wss_session_impl_t;
};
class wss_session : public wss_session_t<wss_session>
{
public:
    using wss_session_t::wss_session_t;
};
}
#endif
