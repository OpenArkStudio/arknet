// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_client.hpp>
#include <arknet/http/impl/websocket_endpoint.hpp>
namespace arknet::detail {
struct template_args_ws_client : template_args_tcp_client {
    using stream_t = websocket::stream<socket_t&>;
    using body_t = http::string_body;
    using buffer_t = beast::flat_buffer;
};
template<class Derived, class Args = template_args_ws_client>
class ws_client_impl_t : public websocket_endpoint_impl<Derived, tcp_client_impl_t<Derived, Args>, Args> {
public:
    template<class... Construction> explicit ws_client_impl_t(Construction&&... construction)
        : websocket_endpoint_impl<Derived, tcp_client_impl_t<Derived, Args>, Args>(std::forward<Construction>(construction)...) {}
};
}
namespace arknet {
using ws_client_args = detail::template_args_ws_client;
template<class Derived, class Args> using ws_client_impl_t = detail::ws_client_impl_t<Derived, Args>;
template<class Derived> class ws_client_t : public detail::ws_client_impl_t<Derived> {
public: using detail::ws_client_impl_t<Derived>::ws_client_impl_t;
};
class ws_client : public ws_client_t<ws_client> { public: using ws_client_t::ws_client_t; };
}
