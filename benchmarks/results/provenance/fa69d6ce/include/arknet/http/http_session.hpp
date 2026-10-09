// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_session.hpp>
#include <arknet/http/impl/http_endpoint.hpp>

namespace arknet
{
template<class Derived, class Args = tcp_session_args>
using http_session_impl_t = detail::http_endpoint_impl_t<Derived, detail::tcp_session_impl_t, Args>;
template<class Derived>
class http_session_t : public http_session_impl_t<Derived>
{
public:
    using http_session_impl_t<Derived>::http_session_impl_t;
};
class http_session : public http_session_t<http_session>
{
public:
    using http_session_t<http_session>::http_session_t;
};
}
