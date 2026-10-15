#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <random>
#include <string>
#include <string_view>

#include "vendor/doctest/doctest.h"

#include <arknet/mysql/detail/wire.hpp>

using namespace arknet::mysql::detail;

namespace {

std::string encode(std::string_view payload, std::uint8_t sequence = 0) {
    std::string wire;
    REQUIRE(encode_message(payload, sequence, wire));
    return wire;
}

} // namespace

DOCTEST_TEST_CASE("MySQL integers retain all unsigned bits and distinguish NULL") {
    for (const auto value : std::array<std::uint64_t, 12>{0, 1, 250, 251, 255, 256, 65535, 65536, 0xffffff, 0x1000000,
             std::uint64_t(1) << 63, std::numeric_limits<std::uint64_t>::max()})
    {
        std::string wire;
        append_length(wire, value);
        wire_reader reader(wire);
        std::optional<std::uint64_t> result;
        REQUIRE(reader.read_length(result) == read_status::complete);
        REQUIRE(result.has_value());
        CHECK(*result == value);
        CHECK(reader.remaining() == 0);
        for (std::size_t size = 0; size < wire.size(); ++size)
        {
            wire_reader partial(std::string_view(wire).substr(0, size));
            result = 42;
            CHECK(partial.read_length(result) == read_status::incomplete);
            CHECK(partial.position() == 0);
            CHECK(result == 42);
        }
    }
    std::string wire;
    append_length(wire, std::nullopt);
    append_length(wire, 0);
    wire_reader reader(wire);
    std::optional<std::uint64_t> result = 9;
    CHECK(reader.read_length(result) == read_status::complete);
    CHECK_FALSE(result.has_value());
    CHECK(reader.read_length(result) == read_status::complete);
    CHECK(result == 0);
    wire_reader invalid(std::string_view("\xff", 1));
    CHECK(invalid.read_length(result) == read_status::invalid);
    CHECK(result == 0);
    CHECK(invalid.position() == 0);
}

DOCTEST_TEST_CASE("MySQL fixed integers reject invalid widths and preserve incomplete output") {
    for (std::size_t width = 1; width <= 8; ++width)
    {
        const auto maximum =
            width == 8 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t(1) << (width * 8)) - 1;
        std::string wire;
        append_integer(wire, maximum, width);
        std::uint64_t result = 0;
        wire_reader reader(wire);
        CHECK(reader.read_integer(width, result) == read_status::complete);
        CHECK(result == maximum);
        wire_reader partial(std::string_view(wire).substr(0, width - 1));
        result = 123;
        CHECK(partial.read_integer(width, result) == read_status::incomplete);
        CHECK(result == 123);
        CHECK(partial.position() == 0);
    }
    wire_reader reader("abcdefgh");
    std::uint64_t result = 91;
    CHECK(reader.read_integer(0, result) == read_status::invalid);
    CHECK(reader.read_integer(9, result) == read_status::invalid);
    CHECK(reader.position() == 0);
    CHECK(result == 91);
}

DOCTEST_TEST_CASE("MySQL strings preserve binary data NULL empty and transactional reads") {
    const std::string binary("a\0b\r\n\xff", 6);
    std::string wire;
    append_string(wire, std::nullopt);
    append_string(wire, "");
    append_string(wire, binary);
    wire_reader reader(wire);
    std::optional<std::string_view> result = "previous";
    REQUIRE(reader.read_string(result) == read_status::complete);
    CHECK_FALSE(result.has_value());
    REQUIRE(reader.read_string(result) == read_status::complete);
    REQUIRE(result.has_value());
    CHECK(result->empty());
    REQUIRE(reader.read_string(result) == read_status::complete);
    CHECK(*result == binary);
    CHECK(reader.remaining() == 0);

    wire.clear();
    append_string(wire, binary);
    for (std::size_t size = 0; size < wire.size(); ++size)
    {
        wire_reader partial(std::string_view(wire).substr(0, size));
        result = "previous";
        CHECK(partial.read_string(result) == read_status::incomplete);
        CHECK(partial.position() == 0);
        CHECK(*result == "previous");
    }
    std::string oversized;
    append_length(oversized, std::numeric_limits<std::uint64_t>::max());
    wire_reader huge(oversized);
    CHECK(huge.read_string(result) == read_status::incomplete);
    CHECK(huge.position() == 0);
    CHECK(*result == "previous");

    wire_reader terminated(std::string_view("mysql\0rest", 10));
    std::string_view text;
    CHECK(terminated.read_null_terminated(text) == read_status::complete);
    CHECK(text == "mysql");
    CHECK(terminated.read_bytes(4, text) == read_status::complete);
    CHECK(text == "rest");
    CHECK(terminated.read_null_terminated(text) == read_status::incomplete);
    CHECK(text == "rest");
}

DOCTEST_TEST_CASE("MySQL packet decoder accepts every stream split without exposing partial messages") {
    const std::string payload("binary\0payload\xff", 15);
    const auto wire = encode(payload, 7);
    for (std::size_t split = 0; split < wire.size(); ++split)
    {
        packet_decoder decoder(1024, 7);
        auto input = std::string_view(wire).substr(0, split);
        std::string result = "previous";
        CHECK(decoder.consume(input, result) == packet_status::incomplete);
        CHECK(input.empty());
        CHECK(result == "previous");
        input = std::string_view(wire).substr(split);
        REQUIRE(decoder.consume(input, result) == packet_status::complete);
        CHECK(result == payload);
        CHECK(input.empty());
        CHECK(decoder.expected_sequence() == 8);
        CHECK(decoder.buffered_size() == 0);
    }
    packet_decoder decoder(1024, 7);
    std::string result;
    for (std::size_t i = 0; i < wire.size(); ++i)
    {
        auto input = std::string_view(wire).substr(i, 1);
        CHECK(decoder.consume(input, result) ==
              (i + 1 == wire.size() ? packet_status::complete : packet_status::incomplete));
        CHECK(input.empty());
    }
    CHECK(result == payload);
}

DOCTEST_TEST_CASE("MySQL coalesced response packets retain sequence across wrap and reset per command") {
    std::string wire;
    std::uint8_t sequence = 250;
    for (int i = 0; i < 300; ++i)
    {
        std::string part;
        REQUIRE(encode_message(std::to_string(i), sequence, part));
        wire += part;
    }
    auto input = std::string_view(wire);
    packet_decoder decoder(1024, 250);
    std::string result;
    for (int i = 0; i < 300; ++i)
    {
        REQUIRE(decoder.consume(input, result) == packet_status::complete);
        CHECK(result == std::to_string(i));
    }
    CHECK(input.empty());
    CHECK(decoder.expected_sequence() == sequence);
    decoder.reset(1);
    const auto next = encode("OK", 1);
    input = next;
    CHECK(decoder.consume(input, result) == packet_status::complete);
    CHECK(result == "OK");
}

DOCTEST_TEST_CASE("MySQL maximum physical payload requires a continuation packet including an empty terminator") {
    for (const auto size : {max_packet_payload - 1, max_packet_payload, max_packet_payload + 1})
    {
        const std::string payload(size, 'x');
        auto sequence = std::uint8_t(255);
        std::string wire;
        REQUIRE(encode_message(payload, sequence, wire));
        CHECK(wire.size() == size + (size / max_packet_payload + 1) * 4);
        CHECK(sequence == (size < max_packet_payload ? 0 : 1));
        packet_decoder decoder(size, 255);
        std::string result = "previous";
        const auto consumed = std::min(wire.size() - 1, max_packet_payload + 3);
        auto input = std::string_view(wire).substr(0, consumed);
        CHECK(decoder.consume(input, result) == packet_status::incomplete);
        CHECK(bool(result == "previous"));
        input = std::string_view(wire).substr(consumed);
        REQUIRE(decoder.consume(input, result) == packet_status::complete);
        CHECK(bool(result == payload));
        CHECK(decoder.expected_sequence() == sequence);
    }
}

DOCTEST_TEST_CASE("MySQL packet limits and sequence failures are sticky until explicit reset") {
    const auto wire = encode("12345");
    packet_decoder decoder(4);
    auto input = std::string_view(wire).substr(0, 4);
    std::string result = "previous";
    CHECK(decoder.consume(input, result) == packet_status::message_too_large);
    CHECK(result == "previous");
    CHECK(decoder.buffered_size() == 4);
    const auto valid = encode("OK");
    input = valid;
    CHECK(decoder.consume(input, result) == packet_status::message_too_large);
    CHECK(input == valid);
    decoder.reset();
    CHECK(decoder.consume(input, result) == packet_status::complete);
    CHECK(result == "OK");
    decoder.reset(1);
    input = valid;
    CHECK(decoder.consume(input, result) == packet_status::sequence_mismatch);
    CHECK(result == "OK");
    decoder.reset();
    const auto empty = encode("");
    input = empty;
    CHECK(decoder.consume(input, result) == packet_status::complete);
    CHECK(result.empty());

    auto sequence = std::uint8_t(9);
    result = "previous";
    CHECK_FALSE(encode_message("12345", sequence, result, 4));
    CHECK(sequence == 9);
    CHECK(result == "previous");
}

DOCTEST_TEST_CASE("MySQL random fragmented messages roundtrip with bounded retained payload") {
    std::mt19937 random(12345);
    packet_decoder decoder(2048);
    std::uint8_t sequence = 0;
    for (int i = 0; i < 1000; ++i)
    {
        std::string payload(random() % 2049, '\0');
        for (auto& byte : payload)
        {
            byte = static_cast<char>(random());
        }
        std::string wire;
        REQUIRE(encode_message(payload, sequence, wire, 2048));
        std::size_t position = 0;
        std::string result = "previous";
        while (position < wire.size())
        {
            const auto size = std::min<std::size_t>(random() % 127 + 1, wire.size() - position);
            auto input = std::string_view(wire).substr(position, size);
            position += size;
            const auto status = decoder.consume(input, result);
            CHECK(status == (position == wire.size() ? packet_status::complete : packet_status::incomplete));
            CHECK(input.empty());
            CHECK(decoder.buffered_size() <= payload.size() + 4);
            if (status == packet_status::incomplete)
            {
                CHECK(result == "previous");
            }
        }
        CHECK(result == payload);
    }
}
