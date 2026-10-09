// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/http/http_session.hpp>

namespace arknet
{
template<class Derived, class Session>
using http_server_impl_t = detail::http_server_impl_t<Derived, Session, detail::tcp_server_impl_t>;
template<class Session>
class http_server_t : public http_server_impl_t<http_server_t<Session>, Session>
{
public:
    using http_server_impl_t<http_server_t<Session>, Session>::http_server_impl_t;
};
using http_server = http_server_t<http_session>;
}
