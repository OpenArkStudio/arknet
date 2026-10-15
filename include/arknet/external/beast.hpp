// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/external/asio.hpp>

#if !ARKNET_USE_BOOST_ASIO
#error "arknet HTTP, WebSocket and rate-limited streams require Boost.Asio and Boost.Beast; set ARKNET_USE_BOOST_ASIO=1"
#else
#ifndef BOOST_BEAST_USE_STD_STRING_VIEW
#define BOOST_BEAST_USE_STD_STRING_VIEW
#endif
#include <boost/beast.hpp>

#if defined(ARKNET_ENABLE_SSL)
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>

#endif
namespace beast = boost::beast;
#endif

namespace http = beast::http;
namespace websocket = beast::websocket;

namespace arknet {

namespace http = beast::http;
namespace websocket = beast::websocket;
} // namespace arknet
