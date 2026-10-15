// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#if defined(_MSVC_LANG)
#if _MSVC_LANG < 202002L
#error "arknet requires C++20 or newer"
#endif
#elif __cplusplus < 202002L
#error "arknet requires C++20 or newer"
#endif

#ifndef ARKNET_USE_BOOST_ASIO
#define ARKNET_USE_BOOST_ASIO 1
#endif

#if defined(ARKNET_INCLUDE_RATE_LIMIT) && !ARKNET_USE_BOOST_ASIO
#error "arknet rate-limited TCP streams require Boost.Asio and Boost.Beast"
#endif

#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
#if defined(ASIO_STANDALONE)
#error "Select one Asio provider per program"
#endif
#else
#define ARKNET_HEADER_ONLY
#endif
