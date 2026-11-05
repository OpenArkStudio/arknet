// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <charconv>
#include <cstdio>
#include <sstream>
#include <string>

#if defined(__APPLE__)
#include <sys/sysctl.h>

#endif

#include "process_resources.hpp"

namespace connection_load {

struct ephemeral_port_range
{
    unsigned first{}, last{};
};

struct port_resources
{
    arknet::benchmark::resource_metric<ephemeral_port_range> ephemeral_ports;
    std::string range_source = "unavailable", range_scope = "unavailable";
    arknet::benchmark::resource_metric<std::uint64_t> tcp_time_wait;
    std::string time_wait_source = "unavailable", time_wait_scope = "unavailable";
};

namespace port_resource_detail {

using arknet::benchmark::process_resource_detail::system_failure;
using arknet::benchmark::process_resource_detail::unavailable;

inline auto checked_range(std::int64_t first, std::int64_t last) {
    if (first < 1 || last > 65535 || first > last)
    {
        return unavailable<ephemeral_port_range>("Invalid ephemeral port range returned by OS query");
    }
    return arknet::benchmark::resource_metric<ephemeral_port_range>{
        ephemeral_port_range{static_cast<unsigned>(first), static_cast<unsigned>(last)}, {}};
}

inline auto parse_port_range(const std::string& text) {
    std::istringstream input(text);
    std::int64_t first{}, last{};
    std::string extra;
    if (!(input >> first >> last) || (input >> extra))
    {
        return unavailable<ephemeral_port_range>("Malformed ip_local_port_range query result");
    }
    return checked_range(first, last);
}

inline auto parse_time_wait(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    std::optional<std::uint64_t> count;
    bool found_tcp = false;
    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string protocol, key, value;
        fields >> protocol;
        if (protocol != "TCP:")
        {
            continue;
        }
        if (found_tcp)
        {
            return unavailable<std::uint64_t>("Duplicate TCP entry in /proc/net/sockstat");
        }
        found_tcp = true;
        while (fields >> key)
        {
            if (!(fields >> value))
            {
                return unavailable<std::uint64_t>("Truncated TCP entry in /proc/net/sockstat");
            }
            if (key == "tw")
            {
                std::uint64_t parsed{};
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (count || error != std::errc{} || end != value.data() + value.size())
                {
                    return unavailable<std::uint64_t>("Invalid TCP tw entry in /proc/net/sockstat");
                }
                count = parsed;
            }
        }
    }
    if (!count)
    {
        return unavailable<std::uint64_t>("TCP tw entry unavailable in /proc/net/sockstat");
    }
    return arknet::benchmark::resource_metric<std::uint64_t>{count, {}};
}

inline arknet::benchmark::resource_metric<std::string> read_query_file(const char* path) {
    const auto file = std::unique_ptr<std::FILE, decltype(&std::fclose)>(std::fopen(path, "rb"), &std::fclose);
    if (!file)
    {
        const auto error = errno;
        return unavailable<std::string>(system_failure(("fopen(" + std::string(path) + ")").c_str(), error));
    }
    char buffer[4096];
    const auto size = std::fread(buffer, 1, sizeof(buffer), file.get());
    if (std::ferror(file.get()))
    {
        const auto error = errno;
        return unavailable<std::string>(system_failure(("read(" + std::string(path) + ")").c_str(), error));
    }
    if (size == sizeof(buffer))
    {
        return unavailable<std::string>("OS query exceeded the 4096-byte diagnostic bound");
    }
    return {std::string(buffer, size), {}};
}

template<class Query>
auto read_macos_range(Query query) {
    int first{}, last{};
    for (const auto& [name, value] :
        {std::pair{"net.inet.ip.portrange.first", &first}, std::pair{"net.inet.ip.portrange.last", &last}})
    {
        std::size_t length = sizeof(*value);
        if (query(name, value, &length) != 0)
        {
            const auto error = errno;
            return unavailable<ephemeral_port_range>(
                system_failure(("sysctlbyname(" + std::string(name) + ")").c_str(), error));
        }
        if (length != sizeof(*value))
        {
            return unavailable<ephemeral_port_range>(
                "Unexpected integer size from sysctlbyname(" + std::string(name) + ")");
        }
    }
    return checked_range(first, last);
}

} // namespace port_resource_detail

inline port_resources read_port_resources() {
    using namespace port_resource_detail;
    port_resources result;
    result.ephemeral_ports =
        unavailable<ephemeral_port_range>("Native ephemeral TCP port-range query is not implemented on this platform");
    result.tcp_time_wait =
        unavailable<std::uint64_t>("An inexpensive native TCP TIME_WAIT count is unavailable on this platform");
#if defined(__APPLE__)
    result.range_source = "sysctl:net.inet.ip.portrange.first,last";
    result.range_scope = "host";
    result.ephemeral_ports = read_macos_range([](const char* name, int* value, std::size_t* length) {
        return sysctlbyname(name, value, length, nullptr, 0);
    });
#elif defined(__linux__)
    result.range_source = "/proc/sys/net/ipv4/ip_local_port_range";
    result.range_scope = "network_namespace";
    const auto range = read_query_file(result.range_source.c_str());
    result.ephemeral_ports =
        range.value ? parse_port_range(*range.value) : unavailable<ephemeral_port_range>(range.unavailable_reason);
    result.time_wait_source = "/proc/net/sockstat:TCP.tw";
    result.time_wait_scope = "network_namespace_all_processes";
    const auto sockets = read_query_file("/proc/net/sockstat");
    result.tcp_time_wait =
        sockets.value ? parse_time_wait(*sockets.value) : unavailable<std::uint64_t>(sockets.unavailable_reason);
#endif
    return result;
}

} // namespace connection_load
