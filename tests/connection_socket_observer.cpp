// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <cerrno>
#include <sstream>
#include <string>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_socket_observer.hpp"

using connection_load::socket_observer;

DOCTEST_TEST_CASE("socket observer distinguishes unavailable from a valid zero") {
    const auto invalid = connection_load::observe_socket(-1);
    DOCTEST_CHECK(invalid.readable_bytes == -1);
    DOCTEST_CHECK(invalid.peek_bytes == -1);
#if !defined(_WIN32)
    DOCTEST_CHECK(invalid.readable_error == EBADF);
    DOCTEST_CHECK(invalid.peek_error == EBADF);
#endif
    socket_observer disabled(false);
    DOCTEST_CHECK(disabled.publish(-1, {}).generation == 0);
    disabled.sample(1, 0);
    std::ostringstream out;
    disabled.write_json(out, "client");
    DOCTEST_CHECK(out.str().empty());
}

DOCTEST_TEST_CASE("socket observation anchors bracket the wall query without losing JSON precision") {
    const auto before = connection_load::observation_steady_ns();
    const auto anchor = connection_load::observation_anchor();
    const auto after = connection_load::observation_steady_ns();
    DOCTEST_CHECK(before <= anchor.steady_before_ns);
    DOCTEST_CHECK(anchor.steady_before_ns <= anchor.steady_after_ns);
    DOCTEST_CHECK(anchor.steady_after_ns <= after);
    DOCTEST_CHECK(anchor.unix_ns > 0);
}

#if !defined(_WIN32)
DOCTEST_TEST_CASE("peek preserves bytes and reports EAGAIN before data arrives") {
    int pair[2];
    DOCTEST_REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    const auto empty = connection_load::observe_socket(pair[1]);
    DOCTEST_CHECK(empty.readable_bytes == 0);
    DOCTEST_CHECK(empty.peek_bytes == -1);
    DOCTEST_CHECK((empty.peek_error == EAGAIN || empty.peek_error == EWOULDBLOCK));
    DOCTEST_REQUIRE(::send(pair[0], "abc", 3, 0) == 3);
    for (unsigned index = 0; index < 2; ++index)
    {
        const auto observed = connection_load::observe_socket(pair[1]);
        DOCTEST_CHECK(observed.poll_result == 1);
        DOCTEST_CHECK((observed.events & POLLIN) != 0);
        DOCTEST_CHECK(observed.readable_bytes == 3);
        DOCTEST_CHECK(observed.peek_bytes == 3);
        DOCTEST_CHECK(std::string(reinterpret_cast<const char*>(observed.peek.data()), 3) == "abc");
    }
    char consumed[3];
    DOCTEST_CHECK(::recv(pair[1], consumed, 3, MSG_DONTWAIT) == 3);
    DOCTEST_CHECK(std::string(consumed, 3) == "abc");
    ::close(pair[0]);
    ::close(pair[1]);
}

DOCTEST_TEST_CASE("observer pins identities and retires duplicate before close") {
    int pair[2];
    DOCTEST_REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    socket_observer observer(true);
    socket_observer::snapshot state;
    state.owner = {7, 1, 1};
    state.time_us = 1000000;
    const auto first = observer.publish(pair[1], state);
    DOCTEST_REQUIRE(first.generation != 0);
    // Reusing the original numeric descriptor cannot redirect the held duplicate.
    const int original = pair[1];
    ::close(original);
    int replacement[2];
    DOCTEST_REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, replacement) == 0);
    DOCTEST_REQUIRE((replacement[0] == original || replacement[1] == original));
    DOCTEST_REQUIRE(::send(pair[0], "old", 3, 0) == 3);
    observer.complete(first, 1000001);
    observer.sample(1000002, 1000000);
    observer.close(state.owner);
    char byte{};
    const auto closed = ::recv(pair[0], &byte, 1, MSG_DONTWAIT);
    DOCTEST_CHECK((closed == 0 || (closed < 0 && errno == ECONNRESET)));
    std::ostringstream out;
    observer.write_json(out, "client");
    DOCTEST_CHECK(out.str().find("\"event\":\"socket_observer_before_cancel\"") != std::string::npos);
    DOCTEST_CHECK(out.str().find("\"clock_anchor\":{\"steady_before_ns\":\"") != std::string::npos);
    DOCTEST_CHECK(out.str().find("\"query_completed_steady_ns\":\"") != std::string::npos);
    DOCTEST_CHECK(out.str().find("\"peek_hex\":\"6f6c64\"") != std::string::npos);
    DOCTEST_CHECK(out.str().find("\"strand_probe_completed_us\":1000001") != std::string::npos);
    ::close(pair[0]);
    ::close(replacement[0]);
    ::close(replacement[1]);
}

DOCTEST_TEST_CASE("recovered slots reject stale generation markers and bound observations") {
    int pair[2];
    DOCTEST_REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    socket_observer observer(true);
    socket_observer::snapshot state;
    state.owner = {7, 1, 1};
    const auto old = observer.publish(pair[1], state);
    observer.prune(state.owner, false, 0);
    const auto current = observer.publish(pair[1], state);
    DOCTEST_CHECK(current.generation > old.generation);
    observer.complete(old, 999);
    for (unsigned index = 0; index < 20; ++index)
    {
        observer.sample(index * socket_observer::interval_us, 1);
    }
    observer.close(state.owner);
    std::ostringstream out;
    observer.write_json(out, "client");
    DOCTEST_CHECK(out.str().find("\"strand_probe_completed_us\":999") == std::string::npos);
    DOCTEST_CHECK(out.str().find("\"sample\":7,") != std::string::npos);
    DOCTEST_CHECK(out.str().find("\"sample\":8,") == std::string::npos);
    ::close(pair[0]);
    ::close(pair[1]);
}
#endif

#if defined(__APPLE__) && defined(TCP_CONNECTION_INFO)
DOCTEST_TEST_CASE("short TCP responses never decode zero initialized counters as available") {
    tcp_connection_info raw{};
    const auto short_info =
        connection_load::decode_tcp_kernel_info(raw, connection_load::tcp_kernel_info_required_length - 1);
    DOCTEST_CHECK(short_info.result == connection_load::tcp_kernel_info::status::short_response);
    const auto full = connection_load::decode_tcp_kernel_info(raw, sizeof(raw));
    DOCTEST_CHECK(full.result == connection_load::tcp_kernel_info::status::available);
}
#endif
