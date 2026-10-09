// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcps_client.hpp>
#include <arknet/http/ws_client.hpp>
namespace arknet::detail {
struct template_args_wss_client : template_args_ws_client { using stream_t = websocket::stream<asio::ssl::stream<socket_t&>&>; };
template<class Derived, class Args = template_args_wss_client>
class wss_client_impl_t : public websocket_endpoint_impl<Derived, tcps_client_impl_t<Derived, Args>, Args> {
public:
    template<class... Construction> explicit wss_client_impl_t(Construction&&... construction)
        : websocket_endpoint_impl<Derived, tcps_client_impl_t<Derived, Args>, Args>(std::forward<Construction>(construction)...) {}
};
}
namespace arknet {
using wss_client_args = detail::template_args_wss_client;
template<class Derived, class Args> using wss_client_impl_t = detail::wss_client_impl_t<Derived, Args>;
template<class Derived> class wss_client_t : public detail::wss_client_impl_t<Derived> {
public: using detail::wss_client_impl_t<Derived>::wss_client_impl_t;
};
class wss_client : public wss_client_t<wss_client> { public: using wss_client_t::wss_client_t; };
}
#endif
