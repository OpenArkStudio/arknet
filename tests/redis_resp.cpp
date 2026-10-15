// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <arknet/redis/detail/resp.hpp>
#include "check.hpp"

namespace {

using namespace arknet::redis::detail;

resp_value decode(std::string_view wire) {
    resp_decoder decoder;
    resp_value value;
    check(decoder.append(wire), "append complete reply");
    check(decoder.next(value) == resp_status::ready, "decode complete reply");
    check(decoder.next(value) == resp_status::need_more, "consume exactly one reply");
    return value;
}

bool equal(const resp_value& left, const resp_value& right) {
    if (left.type != right.type || left.text != right.text || left.integer != right.integer ||
        left.boolean != right.boolean || left.elements.size() != right.elements.size() ||
        left.attributes.size() != right.attributes.size())
    {
        return false;
    }
    if (left.real != right.real && !(std::isnan(left.real) && std::isnan(right.real)))
    {
        return false;
    }
    for (std::size_t i = 0; i < left.elements.size(); ++i)
    {
        if (!equal(left.elements[i], right.elements[i]))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < left.attributes.size(); ++i)
    {
        if (!equal(left.attributes[i], right.attributes[i]))
        {
            return false;
        }
    }
    return true;
}

} // namespace

DOCTEST_TEST_CASE("RESP2 distinguishes null, empty values, binary strings and server errors") {
    check(decode("+OK\r\n").text == "OK", "simple string");
    auto error = decode("-WRONGTYPE key\r\n");
    check(error.type == resp_type::error && error.text == "WRONGTYPE key", "server error is a reply");
    check(decode(":+42\r\n").integer == 42, "signed positive integer");
    check(decode(":-9223372036854775808\r\n").integer == std::numeric_limits<std::int64_t>::min(), "minimum integer");
    check(decode(":9223372036854775807\r\n").integer == std::numeric_limits<std::int64_t>::max(), "maximum integer");
    check(decode("$-1\r\n").type == resp_type::null, "RESP2 null bulk");
    check(decode("*-1\r\n").type == resp_type::null, "RESP2 null array");
    auto empty = decode("$0\r\n\r\n");
    check(empty.type == resp_type::bulk_string && empty.text.empty(), "empty string is not null");
    check(decode("*0\r\n").type == resp_type::array, "empty array is not null");
    const std::string binary("a\0\r\nb", 5);
    check(decode("$5\r\n" + binary + "\r\n").text == binary, "bulk data preserves NUL and CRLF");
}

DOCTEST_TEST_CASE("RESP3 represents scalars, maps, sets, pushes and verbatim values") {
    check(decode("_\r\n").type == resp_type::null, "RESP3 null");
    check(decode("#t\r\n").boolean && !decode("#f\r\n").boolean, "booleans");
    check(decode(",+12.5e-1\r\n").real == 1.25, "real value");
    check(std::isinf(decode(",inf\r\n").real), "positive infinity");
    check(decode(",-inf\r\n").real < 0, "negative infinity");
    check(std::isnan(decode(",nan\r\n").real), "NaN");
    auto big = decode("(123456789012345678901234567890\r\n");
    check(big.type == resp_type::big_number && big.text == "123456789012345678901234567890", "lossless big integer");
    auto error = decode("!7\r\nERR bad\r\n");
    check(error.type == resp_type::bulk_error && error.text == "ERR bad", "bulk error");
    auto verbatim = decode("=7\r\ntxt:hey\r\n");
    check(verbatim.type == resp_type::verbatim_string && verbatim.text == "txt:hey", "preserve format tag");
    auto map = decode("%2\r\n+first\r\n:1\r\n:2\r\n~1\r\n#t\r\n");
    check(map.type == resp_type::map && map.elements.size() == 4, "map pairs");
    check(map.elements[2].integer == 2 && map.elements[3].type == resp_type::set, "non-string map keys");
    auto push = decode(">2\r\n+invalidate\r\n*1\r\n$3\r\nkey\r\n");
    check(push.type == resp_type::push && push.elements[0].text == "invalidate", "push remains distinguishable");
}

DOCTEST_TEST_CASE("RESP3 attributes decorate the correct nested value without consuming a reply") {
    auto value = decode("|1\r\n+outer\r\n:1\r\n|1\r\n+other\r\n:2\r\n"
                        "*2\r\n|1\r\n+ttl\r\n:9\r\n$1\r\na\r\n:5\r\n");
    check(value.attributes.size() == 4 && value.attributes[2].text == "other", "multiple attribute maps");
    check(value.elements.size() == 2, "attributes do not consume array elements");
    check(value.elements[0].attributes.size() == 2 && value.elements[0].attributes[1].integer == 9,
        "nested attributes attach to first value");
    check(value.elements[1].integer == 5 && value.elements[1].attributes.empty(), "attributes do not leak");
}

DOCTEST_TEST_CASE("RESP streamed encodings match fixed-length values") {
    check(equal(decode("$?\r\n;2\r\nhe\r\n;3\r\nllo\r\n;0\r\n"), decode("$5\r\nhello\r\n")), "streamed bulk");
    check(equal(decode("$?\r\n;0\r\n"), decode("$0\r\n\r\n")), "empty streamed bulk");
    check(equal(decode("*?\r\n:1\r\n~?\r\n+a\r\n.\r\n.\r\n"), decode("*2\r\n:1\r\n~1\r\n+a\r\n")), "nested streams");
    check(equal(decode("%?\r\n+k\r\n:1\r\n.\r\n"), decode("%1\r\n+k\r\n:1\r\n")), "streamed map");
}

DOCTEST_TEST_CASE("Every split position and one-byte input preserve nested RESP replies") {
    const std::vector<std::string> messages{"+OK\r\n", ":-123\r\n", "$5\r\nhello\r\n", "=7\r\ntxt:hey\r\n", ",nan\r\n",
        "|1\r\n+k\r\n:1\r\n*2\r\n%1\r\n+x\r\n~2\r\n#t\r\n_\r\n$?\r\n;2\r\nab\r\n;0\r\n",
        "*?\r\n:1\r\n%?\r\n+a\r\n$3\r\nxyz\r\n.\r\n.\r\n"};
    for (const auto& wire : messages)
    {
        const auto expected = decode(wire);
        for (std::size_t split = 0; split < wire.size(); ++split)
        {
            resp_decoder decoder;
            resp_value value;
            value.text = "unchanged";
            check(decoder.append(std::string_view(wire).substr(0, split)), "append prefix");
            check(decoder.next(value) == resp_status::need_more && value.text == "unchanged",
                "partial reply is not exposed");
            check(decoder.append(std::string_view(wire).substr(split)), "append suffix");
            check(decoder.next(value) == resp_status::ready && equal(value, expected), "split reply matches");
        }
        resp_decoder decoder;
        resp_value value;
        for (std::size_t i = 0; i < wire.size(); ++i)
        {
            check(decoder.append(std::string_view(wire).substr(i, 1)), "append byte");
            const auto status = decoder.next(value);
            check(
                status == (i + 1 == wire.size() ? resp_status::ready : resp_status::need_more), "bytewise completion");
        }
        check(equal(value, expected), "bytewise content matches");
    }
}

DOCTEST_TEST_CASE("Pipelined replies retain order across errors, attributes and pushes") {
    resp_decoder decoder;
    check(decoder.append("+OK\r\n-ERR bad\r\n>2\r\n+invalidate\r\n*0\r\n|1\r\n+k\r\n:3\r\n:4\r\n"), "append pipeline");
    resp_value value;
    for (const auto type : {resp_type::simple_string, resp_type::error, resp_type::push, resp_type::integer})
    {
        check(decoder.next(value) == resp_status::ready && value.type == type, "ordered typed reply");
    }
    check(value.integer == 4 && value.attributes.size() == 2, "attributes do not form an extra reply");
    check(decoder.next(value) == resp_status::need_more, "pipeline fully consumed");
    const std::string many = [] {
        std::string result;
        for (int i = 0; i < 10000; ++i)
        {
            result += ':' + std::to_string(i) + "\r\n";
        }
        return result;
    }();
    check(decoder.append(many), "append large pipeline");
    for (int i = 0; i < 10000; ++i)
    {
        check(decoder.next(value) == resp_status::ready && value.integer == i, "large pipeline ordering");
    }
}

DOCTEST_TEST_CASE("Malformed RESP is sticky until reset and cannot desynchronize later replies") {
    const std::vector<std::string> invalid{"+bad\n", "+bad\rx", "?x\r\n", ":\r\n", ":--1\r\n",
        ":9223372036854775808\r\n", ":-9223372036854775809\r\n", "$-2\r\n", "$+1\r\n", "$1\r\nx\nx", "$0\r\nxx",
        "*x\r\n", "%1x\r\n", "~-1\r\n", "!0\r\nxx", "_x\r\n", "#1\r\n", "(1x\r\n", ",.1\r\n", ",NaN\r\n", ",1x\r\n",
        ",1e999\r\n", "=3\r\ntxt\r\n", "=4\r\ntxtx\r\n", ".\r\n", "*1\r\n.\r\n", "%?\r\n+k\r\n.\r\n",
        "*?\r\n|1\r\n+k\r\n:1\r\n.\r\n", "$?\r\n:1\r\n", "$?\r\n;-1\r\n", "$?\r\n;1\r\nxXX", "*?\r\n.x\r\n"};
    for (const auto& wire : invalid)
    {
        resp_decoder decoder;
        resp_value value;
        check(decoder.append(wire + "+VALID\r\n"), "append invalid wire");
        check(decoder.next(value) == resp_status::malformed, "reject malformed frame");
        check(!decoder.append("+OK\r\n") && decoder.next(value) == resp_status::malformed, "error remains sticky");
        decoder.reset();
        check(decoder.append("+OK\r\n") && decoder.next(value) == resp_status::ready && value.text == "OK",
            "explicit reset");
    }
}

DOCTEST_TEST_CASE("RESP resource limits apply across fragments, nested values and streaming") {
    resp_limits limits;
    limits.max_message_bytes = 16;
    limits.max_buffer_bytes = 16;
    limits.max_bulk_bytes = 8;
    limits.max_line_bytes = 8;
    limits.max_values = 4;
    limits.max_depth = 2;
    const std::vector<std::string> excessive{"$9\r\n", "*4\r\n", "%2\r\n", "$999999999999999999999999999\r\n",
        "+123456789\r\n", "*1\r\n*1\r\n:1\r\n", "$?\r\n;5\r\nhello\r\n;4\r\n", "~?\r\n_\r\n_\r\n_\r\n_\r\n"};
    for (const auto& wire : excessive)
    {
        resp_decoder decoder(limits);
        resp_value value;
        resp_status status = resp_status::need_more;
        for (char c : wire)
        {
            check(decoder.append(std::string_view(&c, 1)), "bounded fragment append");
            status = decoder.next(value);
            if (status != resp_status::need_more)
            {
                break;
            }
        }
        check(status == resp_status::limit_exceeded, "reject resource exhaustion");
    }
    resp_decoder buffered(limits);
    check(!buffered.append(std::string(17, 'x')), "reject oversized input buffer before copying");
    resp_value value;
    check(buffered.next(value) == resp_status::limit_exceeded, "buffer failure is sticky");
    resp_decoder exact(limits);
    check(exact.append("$8\r\n12345678\r\n") && exact.next(value) == resp_status::ready, "exact bulk bound");
    for (int i = 0; i < 50; ++i)
    {
        check(
            exact.append(":1\r\n") && exact.next(value) == resp_status::ready, "limits reset between complete replies");
    }
    limits.max_depth = 129;
    DOCTEST_CHECK_THROWS_AS((resp_decoder{limits}), std::invalid_argument);
}

DOCTEST_TEST_CASE("Redis command encoding is binary-safe and bounded") {
    const std::string binary("x\0\r\ny", 5);
    const std::array<std::string_view, 4> args{"SET", "", binary, "NX"};
    const auto encoded = encode_command(args);
    auto value = decode(encoded);
    check(value.type == resp_type::array && value.elements.size() == 4, "command array");
    check(value.elements[1].text.empty() && value.elements[2].text == binary, "preserve empty and binary arguments");
    check(encode_command(args, encoded.size()) == encoded, "exact encoded size allowed");
    DOCTEST_CHECK_THROWS_AS(encode_command(args, encoded.size() - 1), std::length_error);
    DOCTEST_CHECK_THROWS_AS(encode_command(args, 0), std::length_error);
    DOCTEST_CHECK_THROWS_AS(encode_command({}), std::invalid_argument);
}

DOCTEST_TEST_CASE("Deterministic arbitrary input stays within decoder limits") {
    std::mt19937 random(73612);
    resp_limits limits{256, 256, 128, 32, 16, 8};
    for (int round = 0; round < 5000; ++round)
    {
        resp_decoder decoder(limits);
        resp_value value;
        std::string wire(random() % 128, '\0');
        for (char& c : wire)
        {
            c = static_cast<char>(random() % 256);
        }
        check(decoder.append(wire), "append bounded random input");
        for (std::size_t replies = 0; replies <= wire.size(); ++replies)
        {
            if (decoder.next(value) != resp_status::ready)
            {
                break;
            }
            check(replies < wire.size(), "each reply consumes input");
        }
    }
}
