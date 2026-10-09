// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_session.hpp>
#include <arknet/http/impl/websocket_endpoint.hpp>
namespace arknet::detail {
struct template_args_ws_session : template_args_tcp_session {
    using stream_t = websocket::stream<socket_t&>;
    using body_t = http::string_body;
    using buffer_t = beast::flat_buffer;
};
template<class Derived, class Args = template_args_ws_session>
class ws_session_impl_t : public websocket_endpoint_impl<Derived, tcp_session_impl_t<Derived, Args>, Args> {
public:
    template<class... Construction> explicit ws_session_impl_t(Construction&&... construction)
        : websocket_endpoint_impl<Derived, tcp_session_impl_t<Derived, Args>, Args>(std::forward<Construction>(construction)...) {}
};
}
namespace arknet {
using ws_session_args = detail::template_args_ws_session;
template<class Derived, class Args> using ws_session_impl_t = detail::ws_session_impl_t<Derived, Args>;
template<class Derived> class ws_session_t : public detail::ws_session_impl_t<Derived> {
public: using detail::ws_session_impl_t<Derived>::ws_session_impl_t;
};
class ws_session : public ws_session_t<ws_session> { public: using ws_session_t::ws_session_t; };
}
