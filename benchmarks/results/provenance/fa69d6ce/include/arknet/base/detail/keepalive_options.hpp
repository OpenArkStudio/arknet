// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/base/error.hpp>
#include <concepts>
#include <limits>
#include <type_traits>
#if defined(_WIN32)
#include <Mstcpip.h>
#else
#include <cerrno>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace arknet::detail
{
template<class Socket>
bool set_keepalive_options(Socket& socket, bool enabled = true,
    unsigned idle = 60, unsigned interval = 3, unsigned count = 3) noexcept
{
    clear_last_error();
    if constexpr (!std::same_as<typename Socket::lowest_layer_type::protocol_type, asio::ip::tcp>)
        return true;
    else
    {
        if (!socket.is_open()) { set_last_error(asio::error::not_connected); return false; }
        constexpr auto limit = static_cast<unsigned>((std::numeric_limits<int>::max)());
        if (enabled && (!idle || !interval || !count || idle > limit || interval > limit || count > limit))
        {
            set_last_error(asio::error::invalid_argument);
            return false;
        }
    #if defined(_WIN32)
        constexpr auto millisecond_limit = (std::numeric_limits<ULONG>::max)() / 1000;
        if (enabled && (idle > millisecond_limit || interval > millisecond_limit))
        {
            set_last_error(asio::error::invalid_argument);
            return false;
        }
    #endif
        error_code ec;
        socket.set_option(asio::socket_base::keep_alive(enabled), ec);
        if (ec) { set_last_error(ec); return false; }
        if (!enabled) return true;
        auto fd = socket.native_handle();
        auto set_native = [&](int option, unsigned value)
        {
            int setting = static_cast<int>(value);
        #if defined(_WIN32)
            const int result = ::setsockopt(fd, IPPROTO_TCP, option,
                reinterpret_cast<const char*>(&setting), sizeof(setting));
            if (result != 0) set_last_error(::WSAGetLastError(), asio::error::get_system_category());
        #else
            const int result = ::setsockopt(fd, IPPROTO_TCP, option, &setting, sizeof(setting));
            if (result != 0) set_last_error(errno, asio::error::get_system_category());
        #endif
            return result == 0;
        };
    #if defined(_WIN32)
        tcp_keepalive options{1, idle * 1000UL, interval * 1000UL};
        DWORD written = 0;
        if (::WSAIoctl(fd, SIO_KEEPALIVE_VALS, &options, sizeof(options),
            nullptr, 0, &written, nullptr, nullptr) != 0)
        {
            set_last_error(::WSAGetLastError(), asio::error::get_system_category());
            return false;
        }
        #if defined(TCP_KEEPCNT)
        return set_native(TCP_KEEPCNT, count);
        #else
        return true;
        #endif
    #elif defined(TCP_KEEPIDLE)
        return set_native(TCP_KEEPIDLE, idle) && set_native(TCP_KEEPINTVL, interval) && set_native(TCP_KEEPCNT, count);
    #elif defined(TCP_KEEPALIVE)
        return set_native(TCP_KEEPALIVE, idle) && set_native(TCP_KEEPINTVL, interval) && set_native(TCP_KEEPCNT, count);
    #else
        set_last_error(asio::error::operation_not_supported);
        return false;
    #endif
    }
}
}
