// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcps_server.hpp>
#include <arknet/http/wss_session.hpp>
namespace arknet::detail {
template<class Derived, class Session>
class wss_server_impl_t : public websocket_server_impl<Derived, tcps_server_impl_t<Derived, Session>, Session> {
public:
    template<class... Construction> explicit wss_server_impl_t(Construction&&... construction)
        : websocket_server_impl<Derived, tcps_server_impl_t<Derived, Session>, Session>(std::forward<Construction>(construction)...) {}
};
}
namespace arknet {
template<class Derived, class Session> using wss_server_impl_t = detail::wss_server_impl_t<Derived, Session>;
template<class Session> class wss_server_t : public detail::wss_server_impl_t<wss_server_t<Session>, Session> {
public: using detail::wss_server_impl_t<wss_server_t<Session>, Session>::wss_server_impl_t;
};
using wss_server = wss_server_t<wss_session>;
}
#endif
