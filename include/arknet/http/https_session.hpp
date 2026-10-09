// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/config.hpp>
#if defined(ARKNET_ENABLE_SSL)
#include <arknet/tcp/tcps_session.hpp>
#include <arknet/http/impl/http_endpoint.hpp>

namespace arknet
{
template <class Derived, class Args = tcp_session_args>
using https_session_impl_t = detail::http_endpoint_impl_t<Derived, detail::tcps_session_impl_t, Args>;
template <class Derived> class https_session_t : public https_session_impl_t<Derived>
{
public:
    using https_session_impl_t<Derived>::https_session_impl_t;
};
class https_session : public https_session_t<https_session>
{
public:
    using https_session_t<https_session>::https_session_t;
};
}
#endif
