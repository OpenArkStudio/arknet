// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <future>
#include <ios>
#include <system_error>
#include <type_traits>
#include <arknet/external/asio.hpp>
#include <arknet/external/assert.hpp>
namespace arknet
{
inline error_code& get_last_error() noexcept
{
    thread_local error_code value;
    return value;
}
inline void set_last_error(const error_code& error) noexcept
{
    get_last_error() = error;
}
inline void set_last_error(const system_error& error) noexcept
{
    set_last_error(error.code());
}
template <class Category> void set_last_error(int value, const Category& category) noexcept
{
    get_last_error().assign(value, category);
}
template <class Error> void set_last_error(Error value) noexcept
{
    if constexpr (std::is_integral_v<Error>)
        get_last_error().assign(static_cast<int>(value), asio::error::get_system_category());
    else if constexpr (std::is_same_v<Error, std::errc> || std::is_same_v<Error, std::io_errc> ||
                       std::is_same_v<Error, std::future_errc>)
    {
#ifdef ASIO_STANDALONE
        get_last_error() = std::make_error_code(value);
#else
        get_last_error().assign(static_cast<int>(value), asio::error::get_system_category());
#endif
    }
    else
        get_last_error() = value;
}
inline void clear_last_error() noexcept
{
    get_last_error().clear();
}
inline int get_last_error_val() noexcept
{
    return get_last_error().value();
}
inline int last_error_val() noexcept
{
    return get_last_error_val();
}
inline std::string get_last_error_msg()
{
    return get_last_error().message();
}
inline std::string last_error_msg()
{
    return get_last_error_msg();
}
}
