// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
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
    if (snapshot.cpu_interval_seconds)
    {
        DOCTEST_CHECK(std::isfinite(*snapshot.cpu_interval_seconds));
        DOCTEST_CHECK(*snapshot.cpu_interval_seconds > 0);
        DOCTEST_CHECK(*snapshot.cpu_interval_seconds <= snapshot.elapsed_seconds);
    }
    if (snapshot.metrics.cpu_percent.value)
    {
        DOCTEST_CHECK(std::isfinite(*snapshot.metrics.cpu_percent.value));
        DOCTEST_CHECK(*snapshot.metrics.cpu_percent.value >= 0);
    }
    else
    {
        DOCTEST_CHECK_FALSE(snapshot.metrics.cpu_percent.unavailable_reason.empty());
    }
}

} // namespace

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
            "\"nofile_hard\":\"unlimited\"", "\"resource_sample_elapsed_s\":2.5", "\"resource_phase\":\"running\""})
    {
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
             "thread_count", "nofile_soft", "nofile_hard", "resource_cpu_interval_s"})
    {
        DOCTEST_CHECK(text.find('"' + std::string(field) + "\":null") != std::string::npos);
        DOCTEST_CHECK(text.find('"' + std::string(field) + "\":\"") != std::string::npos);
    }
    DOCTEST_CHECK(text.find("FD query \\\"failed\\\"\\\\line\\u000anext") != std::string::npos);
    for (unsigned char character : text)
    {
        DOCTEST_CHECK(character >= 32);
    }
    std::string controls;
    for (char character = 0; character < 32; ++character)
    {
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
    if (actual.rlim_cur != RLIM_INFINITY)
    {
        DOCTEST_CHECK(sample.nofile_soft.value->descriptors == static_cast<std::uint64_t>(actual.rlim_cur));
    }
    if (actual.rlim_max != RLIM_INFINITY)
    {
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
    if (first.cpu_interval_seconds && second.cpu_interval_seconds && whole.cpu_interval_seconds)
    {
        DOCTEST_CHECK(second.elapsed_seconds - *second.cpu_interval_seconds == doctest::Approx(first.elapsed_seconds));
        DOCTEST_CHECK(*whole.cpu_interval_seconds >= *first.cpu_interval_seconds + *second.cpu_interval_seconds);
        DOCTEST_CHECK(*whole.cpu_interval_seconds > *second.cpu_interval_seconds);
    }
    const auto text = output(whole, true);
    DOCTEST_CHECK(text.find("\"resource_unavailable\":{") != std::string::npos);
    DOCTEST_CHECK(text.find("\"resource_phase\":\"after_cleanup\"") != std::string::npos);
}
