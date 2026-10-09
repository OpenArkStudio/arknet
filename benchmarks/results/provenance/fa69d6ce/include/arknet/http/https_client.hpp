// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/config.hpp>
#if defined(ARKNET_ENABLE_SSL)
#include <arknet/tcp/tcps_client.hpp>
#include <arknet/http/impl/http_endpoint.hpp>

namespace arknet
{
template<class Derived, class Args = tcp_client_args>
using https_client_impl_t = detail::http_endpoint_impl_t<Derived, detail::tcps_client_impl_t, Args>;
template<class Derived>
class https_client_t : public https_client_impl_t<Derived>
{
public:
    using https_client_impl_t<Derived>::https_client_impl_t;
};
class https_client : public https_client_t<https_client>
{
public:
    using https_client_t<https_client>::https_client_t;
};
}
#endif
