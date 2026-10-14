// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/config.hpp>
#if defined(ARKNET_ENABLE_SSL)
#include <arknet/http/impl/http_endpoint.hpp>
#include <arknet/tcp/tcps_client.hpp>

namespace arknet {
template<class Derived, class Args = tcp_client_args>
using https_client_impl_t = detail::http_endpoint_impl_t<Derived, detail::tcps_client_impl_t, Args>;
template<class Derived>
class https_client_t : public https_client_impl_t<Derived>
{
public:
    using https_client_impl_t<Derived>::https_client_impl_t;
    using https_client_impl_t<Derived>::start;
    using https_client_impl_t<Derived>::async_start;

    template<class Host>
    bool start(Host&& host) {
        return https_client_impl_t<Derived>::start(std::forward<Host>(host), 443);
    }
    template<class Host>
    bool async_start(Host&& host) {
        return https_client_impl_t<Derived>::async_start(std::forward<Host>(host), 443);
    }
};
class https_client : public https_client_t<https_client>
{
public:
    using https_client_t<https_client>::https_client_t;
};
} // namespace arknet
#endif
