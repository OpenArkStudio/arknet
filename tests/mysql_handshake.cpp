#include <array>
#include <cstdint>
#include <limits>
#include <ostream>
#include <random>
#include <string>
#include <string_view>

#include "vendor/doctest/doctest.h"

#include <arknet/mysql/detail/handshake.hpp>

using namespace arknet::mysql::detail;

namespace {

constexpr auto modern_capabilities = capability::protocol_41 | capability::secure_connection | capability::plugin_auth |
                                     capability::plugin_auth_lenenc_client_data | capability::transactions |
                                     capability::ssl;

std::string make_greeting(std::string_view auth_data, std::string_view plugin = "caching_sha2_password",
    std::uint32_t capabilities = modern_capabilities) {
    REQUIRE(auth_data.size() >= 8);
    REQUIRE(auth_data.size() <= 255);
    std::string payload(1, '\x0a');
    payload += "9.7.2-test";
    payload.push_back('\0');
    append_integer(payload, 0xfeedbeef, 4);
    payload.append(auth_data.substr(0, 8));
    payload.push_back('\0');
    append_integer(payload, capabilities & 0xffff, 2);
    payload.push_back(static_cast<char>(255));
    append_integer(payload, 2, 2);
    append_integer(payload, capabilities >> 16, 2);
    payload.push_back((capabilities & capability::plugin_auth) != 0 ? static_cast<char>(auth_data.size()) : 0);
    payload.append(10, '\0');
    payload.append(auth_data.substr(8));
    if ((capabilities & capability::plugin_auth) != 0)
    {
        payload.append(plugin);
        payload.push_back('\0');
    }
    return payload;
}

std::string make_scramble(bool terminator = true) {
    std::string scramble("0123\0"
                         "56789abcdefghij",
        20);
    if (terminator)
    {
        scramble.push_back('\0');
    }
    return scramble;
}

void check_greeting_unchanged(const server_greeting& greeting) {
    CHECK(greeting.server_version == "unchanged");
    CHECK(greeting.connection_id == 42);
    CHECK(greeting.capabilities == 43);
    CHECK(greeting.character_set == 44);
    CHECK(greeting.status_flags == 45);
    CHECK(greeting.auth_plugin_name == "plugin");
    CHECK(greeting.auth_plugin_data == "opaque");
}

server_greeting old_greeting() {
    return {"unchanged", 42, 43, 44, 45, "plugin", "opaque"};
}

std::string make_ok(std::uint16_t status = 2, std::uint8_t header = 0) {
    std::string payload(1, static_cast<char>(header));
    append_length(payload, std::numeric_limits<std::uint64_t>::max());
    append_length(payload, std::uint64_t(1) << 63);
    append_integer(payload, status, 2);
    append_integer(payload, 65535, 2);
    return payload;
}

void check_ok_unchanged(const ok_packet& packet) {
    CHECK(packet.affected_rows == 11);
    CHECK(packet.last_insert_id == 12);
    CHECK(packet.status_flags == 13);
    CHECK(packet.warnings == 14);
    CHECK(packet.info == "old info");
    CHECK(packet.session_state == "old state");
}

} // namespace

DOCTEST_TEST_CASE("MySQL protocol 10 greeting keeps capabilities and opaque binary authentication data") {
    for (const auto plugin : {"caching_sha2_password", "mysql_native_password", "sha256_password", "custom_plugin"})
    {
        for (const auto terminator : {false, true})
        {
            const auto scramble = make_scramble(terminator);
            auto payload = make_greeting(scramble, plugin);
            server_greeting result;
            REQUIRE(parse_greeting(payload, result) == handshake_status::complete);
            CHECK(result.server_version == "9.7.2-test");
            CHECK(result.connection_id == 0xfeedbeef);
            CHECK(result.capabilities == modern_capabilities);
            CHECK(result.character_set == 255);
            CHECK(result.status_flags == 2);
            CHECK(result.auth_plugin_name == plugin);
            CHECK(result.auth_plugin_data == scramble);
            payload.assign(payload.size(), 'x');
            CHECK(result.auth_plugin_name == plugin);
            CHECK(result.auth_plugin_data == scramble);
        }
    }
    auto payload =
        make_greeting(make_scramble(), "caching_sha2_password", modern_capabilities & ~capability::secure_connection);
    server_greeting result;
    CHECK(parse_greeting(payload, result) == handshake_status::complete);
    payload = make_greeting(make_scramble(), "", capability::protocol_41 | capability::secure_connection);
    REQUIRE(parse_greeting(payload, result) == handshake_status::complete);
    CHECK(result.auth_plugin_name == "mysql_native_password");
    CHECK(result.auth_plugin_data == make_scramble());
}

DOCTEST_TEST_CASE("MySQL greetings reject every truncated prefix and preserve prior output") {
    const auto payload = make_greeting(make_scramble());
    for (std::size_t size = 0; size < payload.size(); ++size)
    {
        CAPTURE(size);
        auto result = old_greeting();
        CHECK(parse_greeting(std::string_view(payload).substr(0, size), result) == handshake_status::incomplete);
        check_greeting_unchanged(result);
    }
    auto result = old_greeting();
    CHECK(parse_greeting(std::string(1, '\x09'), result) == handshake_status::unsupported);
    check_greeting_unchanged(result);
    // Capability and length offsets are derived independently from the fixed fields.
    const auto lower_flags = 1 + std::string_view("9.7.2-test").size() + 1 + 4 + 8 + 1;
    for (const auto offset : {lower_flags - 1, lower_flags + 9})
    {
        auto malformed = payload;
        malformed[offset] = 1;
        CHECK(parse_greeting(malformed, result) == handshake_status::invalid);
        check_greeting_unchanged(result);
    }
    auto malformed = payload;
    malformed[lower_flags + 7] = 7;
    CHECK(parse_greeting(malformed, result) == handshake_status::invalid);
    check_greeting_unchanged(result);
    malformed = make_greeting(make_scramble(), "");
    CHECK(parse_greeting(malformed, result) == handshake_status::invalid);
    check_greeting_unchanged(result);
    malformed = payload + 'x';
    CHECK(parse_greeting(malformed, result) == handshake_status::invalid);
    check_greeting_unchanged(result);
    malformed = make_greeting(make_scramble(), "", 0);
    CHECK(parse_greeting(malformed, result) == handshake_status::unsupported);
    check_greeting_unchanged(result);
}

DOCTEST_TEST_CASE("MySQL password scramble validation never truncates opaque plugins") {
    for (const auto plugin : {"mysql_native_password", "caching_sha2_password", "sha256_password"})
    {
        for (const auto terminator : {false, true})
        {
            const auto data = make_scramble(terminator);
            std::string_view scramble = "old";
            REQUIRE(password_scramble(plugin, data, scramble) == handshake_status::complete);
            CHECK(scramble == std::string_view(data).substr(0, 20));
            CHECK(scramble.data() == data.data());
        }
        for (const auto size : {0, 1, 19, 21, 22, 255})
        {
            std::string_view scramble = "old";
            const std::string data(size, 'x');
            CHECK(password_scramble(plugin, data, scramble) == handshake_status::invalid);
            CHECK(scramble == "old");
        }
    }
    std::string_view result = "old";
    CHECK(password_scramble("unknown", make_scramble(), result) == handshake_status::unsupported);
    CHECK(result == "old");
}

DOCTEST_TEST_CASE("MySQL greetings bound authentication lengths without inspecting opaque plugin bytes") {
    const auto length_offset = 1 + std::string_view("9.7.2-test").size() + 1 + 4 + 8 + 1 + 7;
    for (const auto size : {8, 9, 20, 21, 254, 255})
    {
        const std::string data(size, '\xff');
        const auto payload = make_greeting(data, "opaque_plugin");
        server_greeting result;
        REQUIRE(parse_greeting(payload, result) == handshake_status::complete);
        CHECK(result.auth_plugin_data == data);
        CHECK(result.auth_plugin_name == "opaque_plugin");
        for (std::size_t prefix = 0; prefix < payload.size(); ++prefix)
        {
            result = old_greeting();
            CHECK(parse_greeting(std::string_view(payload).substr(0, prefix), result) == handshake_status::incomplete);
            check_greeting_unchanged(result);
        }
    }
    auto payload = make_greeting(make_scramble());
    payload[length_offset] = '\0';
    server_greeting result;
    CHECK(parse_greeting(payload, result) == handshake_status::complete);
    CHECK(result.auth_plugin_data == make_scramble());
    for (unsigned size = 1; size < 8; ++size)
    {
        payload[length_offset] = static_cast<char>(size);
        result = old_greeting();
        CHECK(parse_greeting(payload, result) == handshake_status::invalid);
        check_greeting_unchanged(result);
    }
    payload[length_offset] = static_cast<char>(255);
    result = old_greeting();
    CHECK(parse_greeting(payload, result) == handshake_status::incomplete);
    check_greeting_unchanged(result);
}

DOCTEST_TEST_CASE("MySQL SSLRequest contains only the exact 32 byte protocol 41 prefix") {
    std::string payload = "old";
    REQUIRE(encode_ssl_request(modern_capabilities, 0x12345678, 255, payload) == handshake_status::complete);
    REQUIRE(payload.size() == 32);
    wire_reader reader(payload);
    std::uint64_t value;
    CHECK(reader.read_integer(4, value) == read_status::complete);
    CHECK(value == modern_capabilities);
    CHECK(reader.read_integer(4, value) == read_status::complete);
    CHECK(value == 0x12345678);
    CHECK(reader.read_integer(1, value) == read_status::complete);
    CHECK(value == 255);
    std::string_view filler;
    CHECK(reader.read_bytes(23, filler) == read_status::complete);
    CHECK(filler == std::string(23, '\0'));
    payload = "old";
    CHECK(encode_ssl_request(modern_capabilities & ~capability::ssl, 1024, 45, payload) == handshake_status::invalid);
    CHECK(payload == "old");
    CHECK(encode_ssl_request(modern_capabilities, 0, 45, payload) == handshake_status::invalid);
    CHECK(payload == "old");
    CHECK(encode_ssl_request(modern_capabilities | capability::compress, 1024, 45, payload) ==
          handshake_status::unsupported);
    CHECK(payload == "old");
}

DOCTEST_TEST_CASE("MySQL handshake response encodes binary auth schema plugin and attributes in wire order") {
    handshake_response response;
    response.capabilities = modern_capabilities | capability::connect_with_db | capability::connect_attrs;
    response.max_packet_size = 64 * 1024 * 1024;
    response.character_set = 255;
    response.username = "alice";
    const std::string auth("\0\xfftoken\0", 8);
    response.auth_response = auth;
    response.database = "app";
    response.auth_plugin_name = "caching_sha2_password";
    response.attributes = {{"_client_name", "arknet"}, {"empty", ""}};
    std::string payload;
    REQUIRE(encode_handshake_response(response, payload) == handshake_status::complete);
    std::string expected;
    append_integer(expected, response.capabilities, 4);
    append_integer(expected, response.max_packet_size, 4);
    expected.push_back(static_cast<char>(255));
    expected.append(23, '\0');
    expected.append("alice", 5);
    expected.push_back('\0');
    append_string(expected, auth);
    expected.append("app\0caching_sha2_password\0", 26);
    std::string attributes;
    append_string(attributes, "_client_name");
    append_string(attributes, "arknet");
    append_string(attributes, "empty");
    append_string(attributes, "");
    append_string(expected, attributes);
    CHECK(payload == expected);
    response.attributes.clear();
    REQUIRE(encode_handshake_response(response, payload) == handshake_status::complete);
    CHECK(payload.back() == '\0');
}

DOCTEST_TEST_CASE("MySQL auth response lengths cover one byte and length encoded boundaries") {
    for (const auto size : {0, 1, 20, 250, 251, 255, 256, 65535, 65536})
    {
        const std::string auth(size, '\xff');
        handshake_response response;
        response.username = "u";
        response.auth_response = auth;
        std::string payload = "old";
        if (size <= 255)
        {
            REQUIRE(encode_handshake_response(response, payload) == handshake_status::complete);
            REQUIRE(payload.size() == 35 + auth.size());
            CHECK(static_cast<unsigned char>(payload[34]) == size);
            CHECK(std::string_view(payload).substr(35) == auth);
        }
        else
        {
            CHECK(encode_handshake_response(response, payload) == handshake_status::invalid);
            CHECK(payload == "old");
        }
        response.capabilities |= capability::plugin_auth | capability::plugin_auth_lenenc_client_data;
        response.auth_plugin_name = "caching_sha2_password";
        REQUIRE(encode_handshake_response(response, payload) == handshake_status::complete);
        wire_reader reader(std::string_view(payload).substr(34));
        std::optional<std::string_view> decoded;
        REQUIRE(reader.read_string(decoded) == read_status::complete);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == auth);
        std::string_view name;
        CHECK(reader.read_null_terminated(name) == read_status::complete);
        CHECK(name == response.auth_plugin_name);
        CHECK(reader.remaining() == 0);
    }
}

DOCTEST_TEST_CASE("MySQL response rejects contradictory flags NUL fields and unimplemented wire extensions") {
    std::string payload = "old";
    const auto check_invalid = [&](const handshake_response& response, handshake_status expected) {
        CHECK(encode_handshake_response(response, payload) == expected);
        CHECK(payload == "old");
    };
    handshake_response response;
    response.capabilities = 0;
    check_invalid(response, handshake_status::unsupported);
    for (const auto flag : {capability::compress, capability::zstd_compression_algorithm, capability::local_files,
             capability::query_attributes, capability::multi_factor_authentication, 1u << 29})
    {
        response = {};
        response.capabilities |= flag;
        check_invalid(response, handshake_status::unsupported);
    }
    response = {};
    response.capabilities |= capability::plugin_auth_lenenc_client_data;
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.username = std::string_view("a\0b", 3);
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.database = "not advertised";
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.capabilities |= capability::connect_with_db;
    response.database = std::string_view("a\0b", 3);
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.auth_plugin_name = "not advertised";
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.capabilities |= capability::plugin_auth;
    check_invalid(response, handshake_status::invalid);
    response.auth_plugin_name = std::string_view("a\0b", 3);
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.attributes = {{"key", "value"}};
    check_invalid(response, handshake_status::invalid);
    response = {};
    response.max_packet_size = 0;
    check_invalid(response, handshake_status::invalid);
}

DOCTEST_TEST_CASE("MySQL OK parsing preserves unsigned counts and supports absent or encoded info") {
    for (const auto flags : {capability::protocol_41, capability::protocol_41 | capability::session_track})
    {
        for (const auto& info : {std::string(), std::string("Rows matched: 5"), std::string("a\0b", 3)})
        {
            auto payload = make_ok();
            if (!info.empty())
            {
                append_string(payload, info);
            }
            ok_packet result;
            REQUIRE(parse_ok(payload, flags, result) == handshake_status::complete);
            CHECK(result.affected_rows == std::numeric_limits<std::uint64_t>::max());
            CHECK(result.last_insert_id == (std::uint64_t(1) << 63));
            CHECK(result.status_flags == 2);
            CHECK(result.warnings == 65535);
            CHECK(result.info == info);
            CHECK(result.session_state.empty());
        }
    }
    auto payload = make_ok(2, 0xfe);
    ok_packet result;
    CHECK(parse_ok(payload, capability::protocol_41, result) == handshake_status::invalid);
    CHECK(parse_ok(payload, capability::protocol_41 | capability::deprecate_eof, result) == handshake_status::complete);
    CHECK(parse_ok(payload, 0, result) == handshake_status::unsupported);
    CHECK(
        parse_ok(std::string_view("\0\0\0\2\0\0\0", 7), capability::protocol_41, result) == handshake_status::complete);
}

DOCTEST_TEST_CASE("MySQL OK session state rejects NULL truncation invalid markers and trailing bytes") {
    const auto flags = capability::protocol_41 | capability::session_track;
    auto payload = make_ok(server_session_state_changed | 2);
    append_string(payload, "done");
    const std::string state("\1\4app\0", 6);
    append_string(payload, state);
    ok_packet result;
    REQUIRE(parse_ok(payload, flags, result) == handshake_status::complete);
    CHECK(result.info == "done");
    CHECK(result.session_state == state);
    for (std::size_t size = 0; size < payload.size(); ++size)
    {
        CAPTURE(size);
        result = {11, 12, 13, 14, "old info", "old state"};
        CHECK(parse_ok(std::string_view(payload).substr(0, size), flags, result) == handshake_status::incomplete);
        check_ok_unchanged(result);
    }
    const auto check_invalid = [&](std::string malformed) {
        result = {11, 12, 13, 14, "old info", "old state"};
        CHECK(parse_ok(malformed, flags, result) == handshake_status::invalid);
        check_ok_unchanged(result);
    };
    check_invalid(std::string("\0\xfb", 2));
    check_invalid(std::string("\0\xff", 2));
    check_invalid(std::string("\0\0\xfb", 3));
    check_invalid(make_ok() + '\xfb');
    check_invalid(make_ok() + '\xff');
    check_invalid(payload + 'x');
    check_invalid(std::string("\x01", 1));
}

DOCTEST_TEST_CASE("MySQL errors parse negotiated SQLSTATE and initial pre-negotiation errors") {
    std::string payload(1, '\xff');
    append_integer(payload, 1045, 2);
    payload += "#28000Access denied";
    error_packet result;
    REQUIRE(parse_error(payload, capability::protocol_41, result) == handshake_status::complete);
    CHECK(result.code == 1045);
    CHECK(result.sql_state == "28000");
    CHECK(result.message == "Access denied");
    for (std::size_t size = 0; size < 9; ++size)
    {
        result = {99, "old", "message"};
        CHECK(parse_error(std::string_view(payload).substr(0, size), capability::protocol_41, result) ==
              handshake_status::incomplete);
        CHECK(result.code == 99);
        CHECK(result.sql_state == "old");
        CHECK(result.message == "message");
    }
    result = {99, "old", "message"};
    auto malformed = payload;
    malformed[3] = '!';
    CHECK(parse_error(malformed, capability::protocol_41, result) == handshake_status::invalid);
    CHECK(result.code == 99);
    CHECK(result.sql_state == "old");
    CHECK(result.message == "message");
    payload.resize(3);
    payload += "Too many connections";
    REQUIRE(parse_error(payload, 0, result) == handshake_status::complete);
    CHECK(result.sql_state.empty());
    CHECK(result.message == "Too many connections");
    CHECK(parse_error(std::string_view("\0", 1), 0, result) == handshake_status::invalid);
}

DOCTEST_TEST_CASE("MySQL auth switch preserves plugin bytes and explicitly rejects old authentication") {
    const auto data = make_scramble();
    std::string payload(1, '\xfe');
    payload += "mysql_native_password";
    payload.push_back('\0');
    payload += data;
    auth_switch_request result;
    REQUIRE(parse_auth_switch(payload, result) == handshake_status::complete);
    CHECK(result.auth_plugin_name == "mysql_native_password");
    CHECK(result.auth_plugin_data == data);
    std::string_view scramble;
    CHECK(password_scramble(result.auth_plugin_name, result.auth_plugin_data, scramble) == handshake_status::complete);
    CHECK(scramble.size() == 20);
    result = {"old", "opaque"};
    CHECK(parse_auth_switch(std::string_view("\xfe", 1), result) == handshake_status::unsupported);
    CHECK(parse_auth_switch(std::string_view("\xfe\0", 2), result) == handshake_status::invalid);
    CHECK(parse_auth_switch(std::string_view("\xfeplugin", 7), result) == handshake_status::incomplete);
    CHECK(parse_auth_switch({}, result) == handshake_status::incomplete);
    CHECK(result.auth_plugin_name == "old");
    CHECK(result.auth_plugin_data == "opaque");
}

DOCTEST_TEST_CASE("MySQL handshake payloads are independent of packet sequence and fragmentation") {
    const auto payload = make_greeting(make_scramble());
    for (const auto initial : std::array<std::uint8_t, 4>{0, 1, 254, 255})
    {
        auto sequence = initial;
        std::string wire;
        REQUIRE(encode_message(payload, sequence, wire));
        CHECK(sequence == static_cast<std::uint8_t>(initial + 1));
        packet_decoder decoder(default_message_limit, initial);
        std::string decoded;
        for (std::size_t index = 0; index < wire.size(); ++index)
        {
            std::string_view input(wire.data() + index, 1);
            const auto status = decoder.consume(input, decoded);
            CHECK(status == (index + 1 == wire.size() ? packet_status::complete : packet_status::incomplete));
            CHECK(input.empty());
        }
        server_greeting result;
        CHECK(parse_greeting(decoded, result) == handshake_status::complete);
        CHECK(result.connection_id == 0xfeedbeef);
    }
}

DOCTEST_TEST_CASE("MySQL hostile handshake payloads retain outputs unless fully accepted") {
    std::mt19937 random(0x4d795351);
    for (unsigned iteration = 0; iteration < 2000; ++iteration)
    {
        std::string payload(random() % 160, '\0');
        for (auto& byte : payload)
        {
            byte = static_cast<char>(random() & 0xff);
        }
        auto greeting = old_greeting();
        if (parse_greeting(payload, greeting) != handshake_status::complete)
        {
            check_greeting_unchanged(greeting);
        }
        ok_packet ok = {11, 12, 13, 14, "old info", "old state"};
        if (parse_ok(payload, modern_capabilities | capability::session_track, ok) != handshake_status::complete)
        {
            check_ok_unchanged(ok);
        }
        error_packet error = {99, "old", "message"};
        if (parse_error(payload, capability::protocol_41, error) != handshake_status::complete)
        {
            CHECK(error.code == 99);
            CHECK(error.sql_state == "old");
            CHECK(error.message == "message");
        }
    }
}
