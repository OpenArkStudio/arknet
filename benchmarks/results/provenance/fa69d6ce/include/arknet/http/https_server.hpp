// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/config.hpp>
#if defined(ARKNET_ENABLE_SSL)
#include <arknet/tcp/tcps_server.hpp>
#include <arknet/http/https_session.hpp>

namespace arknet
{
template<class Derived, class Session>
using https_server_impl_t = detail::http_server_impl_t<Derived, Session, detail::tcps_server_impl_t>;
template<class Session>
class https_server_t : public https_server_impl_t<https_server_t<Session>, Session>
{
public:
    using https_server_impl_t<https_server_t<Session>, Session>::https_server_impl_t;
};
using https_server = https_server_t<https_session>;
}
#endif
