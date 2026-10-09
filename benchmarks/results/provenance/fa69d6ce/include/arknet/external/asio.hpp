// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/config.hpp>
#include <concepts>
#include <string_view>
#include <system_error>
#include <type_traits>

#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
#include <boost/asio.hpp>
#if BOOST_ASIO_VERSION < 103800
#error "arknet requires Boost.Asio 1.38.0 or newer (Boost 1.90+) for the kqueue descriptor publication fix"
#endif
#include <boost/system/system_error.hpp>
#if defined(ARKNET_ENABLE_SSL)
#include <boost/asio/ssl.hpp>
#endif
namespace boost::asio
{
using error_code = boost::system::error_code;
using error_category = boost::system::error_category;
using error_condition = boost::system::error_condition;
using system_error = boost::system::system_error;
}
namespace asio = boost::asio;
namespace bho = boost;
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#if ASIO_VERSION < 103800
#error "arknet requires Standalone Asio 1.38.0 or newer for the kqueue descriptor publication fix"
#endif
#if defined(ARKNET_ENABLE_SSL)
#include <asio/ssl.hpp>
#endif
namespace asio { using error_condition = std::error_condition; }
#endif

namespace arknet
{
using error_code = asio::error_code;
using error_category = asio::error_category;
using error_condition = asio::error_condition;
using system_error = asio::system_error;
namespace detail
{
template<class T>
concept character = std::same_as<T, char> || std::same_as<T, wchar_t> ||
    std::same_as<T, char8_t> || std::same_as<T, char16_t> || std::same_as<T, char32_t>;
}
}

#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
namespace boost::asio
#else
namespace asio
#endif
{
// Pointer buffers describe text; explicit pointer/count sends describe binary data.
template<class Pointer>
requires std::is_pointer_v<std::remove_cvref_t<Pointer>> &&
    arknet::detail::character<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<Pointer>>>>
const_buffer buffer(Pointer&& text) noexcept
{
    using char_type = std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<Pointer>>>;
    return text ? buffer(std::basic_string_view<char_type>(text)) : const_buffer{};
}
}
