// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/tcp/tcp_client.hpp>
#include <arknet/http/impl/http_endpoint.hpp>

namespace arknet
{
template<class Derived, class Args = tcp_client_args>
using http_client_impl_t = detail::http_endpoint_impl_t<Derived, detail::tcp_client_impl_t, Args>;
template<class Derived>
class http_client_t : public http_client_impl_t<Derived>
{
public:
    using http_client_impl_t<Derived>::http_client_impl_t;
};
class http_client : public http_client_t<http_client>
{
public:
    using http_client_t<http_client>::http_client_t;
};
}
