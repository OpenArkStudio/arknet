// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <limits>
#include <sstream>
#include <string>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_kernel_samples.hpp"
#include "../benchmarks/connections_trace.hpp"

namespace {

using connection_load::identity;
using connection_load::tcp_kernel_info;
using connection_load::tcp_kernel_samples;
constexpr identity first{7, 1, 1}, second{7, 1, 2};

std::string history(const tcp_kernel_samples& samples, identity owner = first) {
    std::ostringstream out;
    samples.write_history(out, owner);
    return out.str();
}

std::string summary(const tcp_kernel_samples& samples) {
    std::ostringstream out;
    samples.write_summary(out);
    return out.str();
}
} // namespace

DOCTEST_TEST_CASE("kernel samples stay inert when off and before the inactivity threshold") {
    unsigned calls{};
    auto query = [&] {
        ++calls;
        return tcp_kernel_info{};
    };
    tcp_kernel_samples disabled(false), enabled(true);
    disabled.sample(first, 2000000, 1000000, 136, 65, 69, query);
    enabled.sample(first, 2000000, 999999, 136, 65, 69, query);
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(disabled.storage_bytes() == 0);
    DOCTEST_CHECK(enabled.storage_bytes() <= tcp_kernel_samples::memory_limit);
    DOCTEST_CHECK_FALSE(enabled.contains(first));
    DOCTEST_CHECK(tcp_kernel_samples::stalled(true, false, false, true, 1000000));
    DOCTEST_CHECK_FALSE(tcp_kernel_samples::stalled(true, false, false, true, 999999));
    DOCTEST_CHECK_FALSE(tcp_kernel_samples::stalled(false, false, false, true, 1000000));
    DOCTEST_CHECK_FALSE(tcp_kernel_samples::stalled(true, true, false, true, 1000000));
    DOCTEST_CHECK_FALSE(tcp_kernel_samples::stalled(true, false, true, true, 1000000));
    DOCTEST_CHECK_FALSE(tcp_kernel_samples::stalled(true, false, false, false, 1000000));
}

DOCTEST_TEST_CASE("kernel samples retain pre-reset state and cannot be overwritten after freeze") {
    tcp_kernel_samples samples(true);
    tcp_kernel_info info;
    info.result = tcp_kernel_info::status::available;
    info.state = 4;
    info.rto_ms = 200;
    info.send_buffer_bytes = 4364;
    unsigned calls{};
    auto query = [&] {
        ++calls;
        return info;
    };
    samples.sample(first, 9000000, 1000000, 136, 65, 69, query);
    samples.sample(first, 10000000, 2000000, 136, 65, 69, query);
    const auto before = history(samples);
    DOCTEST_CHECK(before.find("\"state\":4") != std::string::npos);
    DOCTEST_CHECK(before.find("\"rto_ms\":200") != std::string::npos);
    DOCTEST_CHECK(before.find("\"sample_time_us\":9000000") != std::string::npos);
    DOCTEST_CHECK(before.find("periodic_io_worker") != std::string::npos);
    samples.freeze();
    info.state = 0;
    info.rto_ms = 0;
    samples.sample(first, 14000000, 6000000, 136, 65, 69, query);
    samples.release(first);
    DOCTEST_CHECK(calls == 2);
    DOCTEST_CHECK(history(samples) == before);
}

DOCTEST_TEST_CASE("kernel sample capacity preserves persistent stalls and reuses recovered slots") {
    tcp_kernel_samples samples(true);
    unsigned calls{};
    auto query = [&] {
        ++calls;
        return tcp_kernel_info{};
    };
    for (std::uint64_t index = 1; index <= tcp_kernel_samples::session_capacity; ++index)
    {
        samples.sample({7, 1, index}, 2000000, 1000000, 4, 2, 3, query);
    }
    const identity extra{7, 1, 17};
    samples.sample(extra, 2000000, 1000000, 4, 2, 3, query);
    DOCTEST_CHECK(calls == 16);
    DOCTEST_CHECK_FALSE(samples.contains(extra));
    DOCTEST_CHECK(summary(samples).find("\"omitted_eligible_observations\":1") != std::string::npos);
    samples.prune(first, true, 4);  // Persistent stall survives while another identity progresses.
    samples.prune(second, true, 5); // Progress is detected even if inactivity exceeds 1s again.
    samples.sample(extra, 3000000, 1000000, 4, 2, 3, query);
    samples.sample(first, 3000000, 2000000, 4, 2, 3, query);
    DOCTEST_CHECK(samples.contains(first));
    DOCTEST_CHECK(samples.contains(extra));
    DOCTEST_CHECK_FALSE(samples.contains(second));
    DOCTEST_CHECK(history(samples).find("\"sample_time_us\":2000000") != std::string::npos);
    DOCTEST_CHECK(summary(samples).find("\"released_histories\":1") != std::string::npos);
    DOCTEST_CHECK(summary(samples).find("\"slot_reuses\":1") != std::string::npos);
}

DOCTEST_TEST_CASE("kernel samples bound history and separate identities across client and run") {
    tcp_kernel_samples samples(true);
    for (unsigned index = 0; index < 10; ++index)
    {
        samples.sample(first, 2000000 + index, 1000000 + index, 136, 65, 69, [] { return tcp_kernel_info{}; });
    }
    const auto text = history(samples);
    DOCTEST_CHECK(text.find("\"overwritten_samples\":2") != std::string::npos);
    DOCTEST_CHECK(text.find("\"sample_time_us\":2000000") == std::string::npos);
    DOCTEST_CHECK(text.find("\"sample_time_us\":2000002") != std::string::npos);
    DOCTEST_CHECK_FALSE(samples.contains({8, 1, 1}));
    DOCTEST_CHECK_FALSE(samples.contains({7, 2, 1}));
    samples.release({8, 1, 1});
    DOCTEST_CHECK(samples.contains(first));
    DOCTEST_CHECK(text.size() < 8192); // Existing trace output's fixed record scratch ceiling.
}

DOCTEST_TEST_CASE("unavailable kernel samples emit null with a reason rather than zero metrics") {
    for (const auto status : {tcp_kernel_info::status::unsupported, tcp_kernel_info::status::closed,
             tcp_kernel_info::status::query_failed, tcp_kernel_info::status::short_response})
    {
        tcp_kernel_samples samples(true);
        samples.sample(first, 2000000, 1000000, 136, 65, 69, [status] {
            tcp_kernel_info value;
            value.result = status;
            value.error = 54;
            return value;
        });
        const auto text = history(samples);
        DOCTEST_CHECK(text.find("\"tcp_info\":null,\"unavailable_reason\":") != std::string::npos);
        DOCTEST_CHECK(text.find("\"rto_ms\":0") == std::string::npos);
        DOCTEST_CHECK(
            (text.find("\"error\":54") != std::string::npos) == (status == tcp_kernel_info::status::query_failed));
    }
}

DOCTEST_TEST_CASE("largest kernel sample history fits the existing bounded record output") {
    tcp_kernel_samples samples(true);
    for (unsigned index = 0; index < tcp_kernel_samples::sample_capacity; ++index)
    {
        samples.sample(first, (std::numeric_limits<std::int64_t>::max)(), (std::numeric_limits<std::int64_t>::max)(),
            136, (std::numeric_limits<std::uint64_t>::max)(), (std::numeric_limits<std::uint64_t>::max)(), [] {
                tcp_kernel_info value;
                value.result = tcp_kernel_info::status::available;
                value.state = value.flags = value.rto_ms = value.send_buffer_bytes = value.send_window_bytes =
                    value.receive_window_bytes = value.congestion_window_bytes = value.slow_start_threshold_bytes =
                        (std::numeric_limits<std::uint32_t>::max)();
                value.tx_bytes = value.rx_bytes = value.retransmit_bytes = value.out_of_order_bytes =
                    (std::numeric_limits<std::uint64_t>::max)();
                return value;
            });
    }
    connection_load::trace_output output;
    DOCTEST_CHECK(output.append([&](std::ostream& out) {
        out << "{\"event\":\"tcp_kernel_sample_history\",\"run_id\":7,\"client_id\":1,\"connection_id\":1,";
        samples.write_history(out, first);
        out << '}';
    }));
    DOCTEST_CHECK_FALSE(output.truncated());
}
