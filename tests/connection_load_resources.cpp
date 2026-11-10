// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_resources.hpp"

namespace {

std::string output(const connection_load::resource_snapshot& snapshot, bool after_cleanup) {
    std::ostringstream stream;
    stream << "{\"event\":\"test\"";
    connection_load::write_resource_fields(stream, snapshot, after_cleanup);
    stream << '}';
    return stream.str();
}

void check_sample(const connection_load::resource_snapshot& snapshot) {
    DOCTEST_CHECK(std::isfinite(snapshot.elapsed_seconds));
    DOCTEST_CHECK(snapshot.elapsed_seconds >= 0);
    DOCTEST_CHECK(snapshot.cpu_interval_seconds.has_value() == snapshot.metrics.cpu_percent.value.has_value());
    if (snapshot.cpu_interval_seconds) {
        DOCTEST_CHECK(std::isfinite(*snapshot.cpu_interval_seconds));
        DOCTEST_CHECK(*snapshot.cpu_interval_seconds > 0);
        DOCTEST_CHECK(*snapshot.cpu_interval_seconds <= snapshot.elapsed_seconds);
    }
    if (snapshot.metrics.cpu_percent.value) {
        DOCTEST_CHECK(std::isfinite(*snapshot.metrics.cpu_percent.value));
        DOCTEST_CHECK(*snapshot.metrics.cpu_percent.value >= 0);
    } else {
        DOCTEST_CHECK_FALSE(snapshot.metrics.cpu_percent.unavailable_reason.empty());
    }
}

} // namespace

DOCTEST_TEST_CASE("Port diagnostics validate OS ranges and namespace-wide TIME_WAIT without estimating headroom") {
    using namespace connection_load::port_resource_detail;
    const auto range = parse_port_range("49152 65535\n");
    DOCTEST_REQUIRE(range.value);
    DOCTEST_CHECK(range.value->first == 49152);
    DOCTEST_CHECK(range.value->last == 65535);
    for (const auto text : {"", "0 65535", "49152 65536", "60000 50000", "-1 20", "1", "1 2 3", "1 x"}) {
        const auto invalid = parse_port_range(text);
        DOCTEST_CHECK_FALSE(invalid.value);
        DOCTEST_CHECK_FALSE(invalid.unavailable_reason.empty());
    }
    for (const auto text : {"TCP: inuse 5 orphan 0 tw 0 alloc 7 mem 1\n", "UDP: inuse 4\nTCP: tw 0\n"}) {
        const auto zero = parse_time_wait(text);
        DOCTEST_REQUIRE(zero.value);
        DOCTEST_CHECK(*zero.value == 0);
    }
    DOCTEST_CHECK(parse_time_wait("TCP: inuse 1 tw 15000 mem 3\n").value == 15000);
    for (const auto text : {"", "TCP6: tw 10", "TCP: inuse 5", "TCP: tw", "TCP: tw -1", "TCP: tw 1x", "TCP: tw 1 tw 2",
             "TCP: tw 1\nTCP: tw 2", "TCP: tw 18446744073709551616"}) {
        DOCTEST_CHECK_FALSE(parse_time_wait(text).value);
    }
    const auto missing = read_query_file("");
    DOCTEST_CHECK_FALSE(missing.value);
    DOCTEST_CHECK(missing.unavailable_reason.find("fopen() failed (OS error ") == 0);
}

DOCTEST_TEST_CASE("Native port queries retain their source scope and explicit OS errors") {
    using connection_load::port_resource_detail::read_macos_range;
    const auto range = read_macos_range([](const char* name, int* value, std::size_t*) {
        *value = std::string_view(name).ends_with("first") ? 49152 : 65535;
        return 0;
    });
    DOCTEST_REQUIRE(range.value);
    DOCTEST_CHECK(range.value->first == 49152);
    const auto failed = read_macos_range([](const char*, int*, std::size_t*) {
        errno = EACCES;
        return -1;
    });
    DOCTEST_CHECK_FALSE(failed.value);
    DOCTEST_CHECK(failed.unavailable_reason ==
                  "sysctlbyname(net.inet.ip.portrange.first) failed (OS error " + std::to_string(EACCES) + ")");
    const auto short_result = read_macos_range([](const char*, int*, std::size_t* length) {
        *length = 0;
        return 0;
    });
    DOCTEST_CHECK_FALSE(short_result.value);
    const auto actual = connection_load::read_port_resources();
    if (actual.ephemeral_ports.value) {
        DOCTEST_CHECK(actual.ephemeral_ports.value->first > 0);
        DOCTEST_CHECK(actual.ephemeral_ports.value->first <= actual.ephemeral_ports.value->last);
        DOCTEST_CHECK(actual.ephemeral_ports.value->last <= 65535);
    } else {
        DOCTEST_CHECK_FALSE(actual.ephemeral_ports.unavailable_reason.empty());
    }
#if defined(__linux__)
    DOCTEST_CHECK(actual.time_wait_scope == "network_namespace_all_processes");
#else
    DOCTEST_CHECK_FALSE(actual.tcp_time_wait.value);
    DOCTEST_CHECK_FALSE(actual.tcp_time_wait.unavailable_reason.empty());
#endif
}

DOCTEST_TEST_CASE("Connect failures retain their first pre-cleanup snapshot and error identity") {
    connection_load::connect_failure_observer observer;
    std::ostringstream empty;
    observer.write_fields(empty);
    DOCTEST_CHECK(empty.str() == ",\"connect_failure_resources\":null");
    std::ostringstream no_event;
    observer.write_event(no_event, "client", 42, 7);
    DOCTEST_CHECK(no_event.str().empty());
    unsigned queries = 0;
    observer.capture(49, "system", 1312, [&] {
        ++queries;
        auto resources = connection_load::read_port_resources();
        resources.ephemeral_ports = {connection_load::ephemeral_port_range{49152, 65535}, {}};
        resources.tcp_time_wait = {0, {}};
        return resources;
    });
    observer.capture(24, "later", 0, [&]() -> connection_load::port_resources {
        ++queries;
        throw std::runtime_error("must not query twice");
    });
    DOCTEST_CHECK(queries == 1);
    std::ostringstream text;
    observer.write_fields(text);
    DOCTEST_CHECK(text.str().find("\"phase\":\"first_connect_failure_before_cleanup\",\"established\":1312") !=
                  std::string::npos);
    DOCTEST_CHECK(text.str().find("\"error_code\":49,\"error_category\":\"system\"") != std::string::npos);
    DOCTEST_CHECK(text.str().find("\"ephemeral_ports\":{\"first\":49152,\"last\":65535}") != std::string::npos);
    DOCTEST_CHECK(text.str().find("\"tcp_time_wait\":0") != std::string::npos);
    DOCTEST_CHECK(text.str().find("free") == std::string::npos);
    std::ostringstream event;
    observer.write_event(event, "client", 42, 7, true);
    DOCTEST_CHECK(event.str().starts_with(
        "{\"event\":\"connect_resource_failure\",\"role\":\"client\",\"run_id\":\"42\",\"client_id\":\"7\","));
    DOCTEST_CHECK(event.str().find("\"established\":1312") != std::string::npos);
    DOCTEST_CHECK(event.str().ends_with("}\n"));
    connection_load::connect_failure_observer failed;
    failed.capture(49, "system", 0, []() -> connection_load::port_resources { throw std::runtime_error("query"); });
    std::ostringstream unavailable;
    failed.write_fields(unavailable);
    DOCTEST_CHECK(unavailable.str().find("Failure resource snapshot could not be captured") != std::string::npos);
}

DOCTEST_TEST_CASE("Connection resource JSON preserves numbers, real zero and explicit missing values") {
    connection_load::resource_snapshot sample;
    sample.elapsed_seconds = 2.5;
    sample.cpu_interval_seconds = 0.5;
    sample.metrics.cpu_percent = {175.25, {}};
    sample.metrics.current_rss_bytes = {0, {}};
    sample.metrics.peak_rss_bytes = {8192, {}};
    sample.metrics.fd_count = {0, {}};
    sample.metrics.nofile_soft = {arknet::benchmark::file_descriptor_limit{0, false}, {}};
    sample.metrics.nofile_hard = {arknet::benchmark::file_descriptor_limit{0, true}, {}};
    sample.metrics.handle_count = {std::nullopt, "Not a Windows process"};
    sample.metrics.thread_count = {4, {}};
    const auto text = output(sample, false);
    for (const auto field :
        {"\"cpu_percent\":175.25", "\"peak_rss_bytes\":8192", "\"current_rss_bytes\":0", "\"fd_count\":0",
            "\"thread_count\":4", "\"handle_count\":null", "\"resource_cpu_interval_s\":0.5", "\"nofile_soft\":0",
            "\"nofile_hard\":\"unlimited\"", "\"resource_sample_elapsed_s\":2.5", "\"resource_phase\":\"running\""}) {
        DOCTEST_CHECK(text.find(field) != std::string::npos);
    }
    DOCTEST_CHECK(
        text.find("\"resource_unavailable\":{\"handle_count\":\"Not a Windows process\"}") != std::string::npos);
    DOCTEST_CHECK(output(sample, true).find("\"resource_phase\":\"after_cleanup\"") != std::string::npos);
}

DOCTEST_TEST_CASE("Connection resource JSON explains absent CPU intervals and escapes diagnostic strings") {
    connection_load::resource_snapshot sample;
    sample.metrics.cpu_percent = {std::nullopt, "No prior CPU sample"};
    sample.metrics.current_rss_bytes = {std::nullopt, "Current RSS unavailable"};
    sample.metrics.peak_rss_bytes = {std::nullopt, "Peak RSS unavailable"};
    sample.metrics.fd_count = {std::nullopt, "FD query \"failed\"\\line\nnext"};
    sample.metrics.nofile_soft = {std::nullopt, "RLIMIT_NOFILE unavailable"};
    sample.metrics.nofile_hard = {std::nullopt, "RLIMIT_NOFILE unavailable"};
    sample.metrics.handle_count = {std::nullopt, "Not supported"};
    sample.metrics.thread_count = {std::nullopt, "Thread query unavailable"};
    const auto text = output(sample, true);
    for (const auto field : {"cpu_percent", "current_rss_bytes", "peak_rss_bytes", "fd_count", "handle_count",
             "thread_count", "nofile_soft", "nofile_hard", "resource_cpu_interval_s"}) {
        DOCTEST_CHECK(text.find('"' + std::string(field) + "\":null") != std::string::npos);
        DOCTEST_CHECK(text.find('"' + std::string(field) + "\":\"") != std::string::npos);
    }
    DOCTEST_CHECK(text.find("FD query \\\"failed\\\"\\\\line\\u000anext") != std::string::npos);
    for (unsigned char character : text) {
        DOCTEST_CHECK(character >= 32);
    }
    std::string controls;
    for (char character = 0; character < 32; ++character) {
        controls += character;
    }
    std::ostringstream escaped;
    connection_load::write_resource_string(escaped, controls);
    DOCTEST_CHECK(escaped.str().size() == 32 * 6 + 2);
    DOCTEST_CHECK(escaped.str().find("\\u0000") != std::string::npos);
    DOCTEST_CHECK(escaped.str().find("\\u001f") != std::string::npos);
}

DOCTEST_TEST_CASE("Connection resource limits preserve finite maximum values and startup identity") {
    connection_load::resource_snapshot sample;
    sample.metrics.nofile_soft = {
        arknet::benchmark::file_descriptor_limit{(std::numeric_limits<std::uint64_t>::max)(), false}, {}};
    sample.metrics.nofile_hard = {arknet::benchmark::file_descriptor_limit{0, true}, {}};
    std::ostringstream stream;
    connection_load::write_resource_startup(stream, sample, "client", 42, 7);
    const auto text = stream.str();
    DOCTEST_CHECK(
        text.starts_with("{\"event\":\"resource_startup\",\"role\":\"client\",\"run_id\":42,\"client_id\":7,"));
    DOCTEST_CHECK(text.find("\"nofile_soft\":18446744073709551615") != std::string::npos);
    DOCTEST_CHECK(text.find("\"nofile_hard\":\"unlimited\"") != std::string::npos);
    DOCTEST_CHECK(text.ends_with("}\n"));

    std::ostringstream string_ids;
    connection_load::write_resource_startup(string_ids, sample, "client", (std::numeric_limits<std::uint64_t>::max)(),
        (std::numeric_limits<std::uint64_t>::max)(), true);
    DOCTEST_CHECK(string_ids.str().starts_with("{\"event\":\"resource_startup\",\"role\":\"client\","
                                               "\"run_id\":\"18446744073709551615\","
                                               "\"client_id\":\"18446744073709551615\","));
}

DOCTEST_TEST_CASE("Process resource limits report the calling process without modifying its limits") {
#if !defined(_WIN32) && defined(RLIMIT_NOFILE)
    rlimit before{};
    DOCTEST_REQUIRE(getrlimit(RLIMIT_NOFILE, &before) == 0);
#endif
    arknet::benchmark::process_resource_sampler sampler;
    const auto sample = sampler.sample();
#if !defined(_WIN32) && defined(RLIMIT_NOFILE)
    rlimit actual{};
    DOCTEST_REQUIRE(getrlimit(RLIMIT_NOFILE, &actual) == 0);
    DOCTEST_CHECK(actual.rlim_cur == before.rlim_cur);
    DOCTEST_CHECK(actual.rlim_max == before.rlim_max);
    DOCTEST_REQUIRE(sample.nofile_soft.value);
    DOCTEST_REQUIRE(sample.nofile_hard.value);
    DOCTEST_CHECK(sample.nofile_soft.value->unlimited == (actual.rlim_cur == RLIM_INFINITY));
    DOCTEST_CHECK(sample.nofile_hard.value->unlimited == (actual.rlim_max == RLIM_INFINITY));
    if (actual.rlim_cur != RLIM_INFINITY) {
        DOCTEST_CHECK(sample.nofile_soft.value->descriptors == static_cast<std::uint64_t>(actual.rlim_cur));
    }
    if (actual.rlim_max != RLIM_INFINITY) {
        DOCTEST_CHECK(sample.nofile_hard.value->descriptors == static_cast<std::uint64_t>(actual.rlim_max));
    }
#else
    DOCTEST_CHECK_FALSE(sample.nofile_soft.value);
    DOCTEST_CHECK_FALSE(sample.nofile_hard.value);
    DOCTEST_CHECK_FALSE(sample.nofile_soft.unavailable_reason.empty());
    DOCTEST_CHECK_FALSE(sample.nofile_hard.unavailable_reason.empty());
#endif
}

#if !defined(_WIN32) && defined(RLIMIT_NOFILE)
DOCTEST_TEST_CASE("Process resource limits distinguish OS failure, finite zero and infinity") {
    arknet::benchmark::process_resources sample;
    arknet::benchmark::process_resource_detail::read_file_descriptor_limits(sample, [](rlimit* limits) {
        limits->rlim_cur = 0;
        limits->rlim_max = RLIM_INFINITY;
        return 0;
    });
    DOCTEST_REQUIRE(sample.nofile_soft.value);
    DOCTEST_CHECK(sample.nofile_soft.value->descriptors == 0);
    DOCTEST_CHECK_FALSE(sample.nofile_soft.value->unlimited);
    DOCTEST_REQUIRE(sample.nofile_hard.value);
    DOCTEST_CHECK(sample.nofile_hard.value->unlimited);
    arknet::benchmark::process_resource_detail::read_file_descriptor_limits(sample, [](rlimit*) {
        errno = EACCES;
        return -1;
    });
    DOCTEST_CHECK_FALSE(sample.nofile_soft.value);
    DOCTEST_CHECK_FALSE(sample.nofile_hard.value);
    const auto reason = "getrlimit(RLIMIT_NOFILE) failed (OS error " + std::to_string(EACCES) + ")";
    DOCTEST_CHECK(sample.nofile_soft.unavailable_reason == reason);
    DOCTEST_CHECK(sample.nofile_hard.unavailable_reason == reason);
}
#endif

DOCTEST_TEST_CASE("Connection resource periodic and whole-run samplers retain independent time windows") {
    const auto began = std::chrono::steady_clock::now();
    connection_load::resource_observer overall(began);
    connection_load::resource_observer periodic(began);
    const auto initial = periodic.initial_snapshot();
    DOCTEST_CHECK_FALSE(initial.metrics.cpu_percent.value);
    DOCTEST_CHECK_FALSE(initial.cpu_interval_seconds);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto first = periodic.sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto second = periodic.sample();
    const auto whole = overall.sample();
    check_sample(first);
    check_sample(second);
    check_sample(whole);
    DOCTEST_CHECK(first.elapsed_seconds <= second.elapsed_seconds);
    DOCTEST_CHECK(second.elapsed_seconds <= whole.elapsed_seconds);
    if (first.cpu_interval_seconds && second.cpu_interval_seconds && whole.cpu_interval_seconds) {
        DOCTEST_CHECK(second.elapsed_seconds - *second.cpu_interval_seconds == doctest::Approx(first.elapsed_seconds));
        DOCTEST_CHECK(*whole.cpu_interval_seconds >= *first.cpu_interval_seconds + *second.cpu_interval_seconds);
        DOCTEST_CHECK(*whole.cpu_interval_seconds > *second.cpu_interval_seconds);
    }
    const auto text = output(whole, true);
    DOCTEST_CHECK(text.find("\"resource_unavailable\":{") != std::string::npos);
    DOCTEST_CHECK(text.find("\"resource_phase\":\"after_cleanup\"") != std::string::npos);
}
