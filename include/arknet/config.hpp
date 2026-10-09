// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#if defined(_MSVC_LANG)
#if _MSVC_LANG < 202002L
#error "arknet requires C++20 or newer"
#endif
#elif __cplusplus < 202002L
#error "arknet requires C++20 or newer"
#endif

#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
#if defined(ASIO_STANDALONE)
#error "Select one Asio provider per program"
#endif
#else
#define ARKNET_HEADER_ONLY
#endif
