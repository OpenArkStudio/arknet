// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <cstdint>
#include <limits>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_protocol.hpp"

using namespace connection_load;

DOCTEST_TEST_CASE("Connection load frames retain full identities and reject corrupted content") {
    const identity owner{0xfedcba9876543210, 0x123456789abcdef0, 0x1122334455667788};
    auto frame = make_frame(frame_kind::data, owner, 0x8877665544332211, 257);
    frame_header header;
    DOCTEST_REQUIRE(decode(frame, header));
    DOCTEST_CHECK(header.owner == owner);
    DOCTEST_CHECK(header.sequence == 0x8877665544332211);
    DOCTEST_CHECK(header.body_bytes == 257);
    DOCTEST_CHECK(header.kind == frame_kind::data);
    frame[header_bytes + 128] ^= 1;
    DOCTEST_CHECK_FALSE(decode(frame, header));
}

DOCTEST_TEST_CASE("Connection load frames reject malformed control and length fields") {
    const identity owner{1, 2, 3};
    frame_header header;
    const auto valid = make_frame(frame_kind::hello, owner);
    for (std::size_t length = 0; length < header_bytes; ++length)
    {
        DOCTEST_CHECK_FALSE(decode(std::string_view(valid).substr(0, length), header));
    }
    auto corrupt = valid;
    corrupt[0] = 'X';
    DOCTEST_CHECK_FALSE(decode(corrupt, header));
    corrupt = valid;
    write_integer(corrupt, 8, 8);
    DOCTEST_CHECK_FALSE(decode(corrupt, header));
    corrupt = valid;
    write_integer(corrupt, 48, (std::numeric_limits<std::uint64_t>::max)());
    DOCTEST_CHECK_FALSE(decode(corrupt, header));
    corrupt = valid;
    corrupt[56] = 1;
    DOCTEST_CHECK_FALSE(decode(corrupt, header));
    DOCTEST_CHECK_FALSE(decode(make_frame(frame_kind::finish, owner, 1, 1), header));
    DOCTEST_CHECK_THROWS_AS(
        make_frame(frame_kind::data, owner, 1, (std::numeric_limits<std::size_t>::max)()), std::length_error);
}

DOCTEST_TEST_CASE("Connection latency buckets contain every observation and retain mergeable counts") {
    latency_distribution first, second, combined;
    constexpr std::array<std::uint64_t, 15> observations{0, 1, 2, 7, 8, 9, 15, 16, 17, 127, 128, 1000, 1000000,
        std::uint64_t{1} << 63, (std::numeric_limits<std::uint64_t>::max)()};
    for (std::size_t index = 0; index < observations.size(); ++index)
    {
        const auto value = observations[index];
        const auto position = latency_distribution::bucket(value);
        DOCTEST_REQUIRE(position < latency_distribution::bucket_count);
        DOCTEST_CHECK(latency_distribution::upper_bound(position) >= value);
        if (position)
        {
            DOCTEST_CHECK(latency_distribution::upper_bound(position - 1) < value);
        }
        (index % 2 ? first : second).observe(value);
        combined.observe(value);
    }
    for (std::size_t index = 0; index < latency_distribution::bucket_count; ++index)
    {
        DOCTEST_CHECK(first.count(index) + second.count(index) == combined.count(index));
    }
    DOCTEST_CHECK(combined.total() == observations.size());
    DOCTEST_CHECK(combined.percentile(50) >= 16);
    DOCTEST_CHECK(combined.percentile(50) <= 17);
    DOCTEST_CHECK(combined.percentile(99) == (std::numeric_limits<std::uint64_t>::max)());
}
