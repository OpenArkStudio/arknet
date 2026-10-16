// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include "scoped_allocation_failure.hpp"

#if defined(_MSC_VER)
// MSVC Asio calls the CRT directly, bypassing replaceable operator new. Prime
// every dependency before remapping that one call inside Asio's memory header.
#include <cstdlib>
#include <memory>
#include <new>

#include <malloc.h>

#if defined(ASIO_DETAIL_MEMORY_HPP) || defined(BOOST_ASIO_DETAIL_MEMORY_HPP)
#error "Include the allocation-failure preload before any Asio or arknet header"
#endif

#if !defined(ARKNET_USE_BOOST_ASIO) || ARKNET_USE_BOOST_ASIO
#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/cstdint.hpp>
#include <boost/asio/detail/throw_exception.hpp>

#else
#include <asio/detail/config.hpp>
#include <asio/detail/cstdint.hpp>
#include <asio/detail/throw_exception.hpp>

#endif

namespace scoped_allocation_failure {

inline void* asio_aligned_malloc(std::size_t size, std::size_t alignment) noexcept {
    if (armed)
    {
        disarm();
        ++injections;
        // Let Asio's actual allocation path throw its normal bad_alloc.
        return nullptr;
    }
    return ::_aligned_malloc(size, alignment);
}

} // namespace scoped_allocation_failure

#define _aligned_malloc ::scoped_allocation_failure::asio_aligned_malloc
#if !defined(ARKNET_USE_BOOST_ASIO) || ARKNET_USE_BOOST_ASIO
#include <boost/asio/detail/memory.hpp>

#else
#include <asio/detail/memory.hpp>

#endif
#undef _aligned_malloc
#endif
