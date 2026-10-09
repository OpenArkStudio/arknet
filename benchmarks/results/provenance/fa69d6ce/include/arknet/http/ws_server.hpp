// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/http/ws_session.hpp>
namespace arknet::detail {
template<class Derived, class Session>
class ws_server_impl_t : public websocket_server_impl<Derived, tcp_server_impl_t<Derived, Session>, Session> {
public:
    template<class... Construction> explicit ws_server_impl_t(Construction&&... construction)
        : websocket_server_impl<Derived, tcp_server_impl_t<Derived, Session>, Session>(std::forward<Construction>(construction)...) {}
};
}
namespace arknet {
template<class Derived, class Session> using ws_server_impl_t = detail::ws_server_impl_t<Derived, Session>;
template<class Session> class ws_server_t : public detail::ws_server_impl_t<ws_server_t<Session>, Session> {
public: using detail::ws_server_impl_t<ws_server_t<Session>, Session>::ws_server_impl_t;
};
using ws_server = ws_server_t<ws_session>;
}
