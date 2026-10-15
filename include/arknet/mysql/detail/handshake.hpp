// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arknet/mysql/detail/wire.hpp>

namespace arknet::mysql::detail {

// MySQL Classic protocol payloads. Packet headers and sequence IDs belong to wire.hpp.
// https://dev.mysql.com/doc/dev/mysql-server/9.7.2/page_protocol_connection_phase_packets_protocol_handshake_v10.html
namespace capability {

inline constexpr std::uint32_t long_password = 1u << 0;
inline constexpr std::uint32_t found_rows = 1u << 1;
inline constexpr std::uint32_t long_flag = 1u << 2;
inline constexpr std::uint32_t connect_with_db = 1u << 3;
inline constexpr std::uint32_t compress = 1u << 5;
inline constexpr std::uint32_t local_files = 1u << 7;
inline constexpr std::uint32_t ignore_space = 1u << 8;
inline constexpr std::uint32_t protocol_41 = 1u << 9;
inline constexpr std::uint32_t interactive = 1u << 10;
inline constexpr std::uint32_t ssl = 1u << 11;
inline constexpr std::uint32_t ignore_sigpipe = 1u << 12;
inline constexpr std::uint32_t transactions = 1u << 13;
inline constexpr std::uint32_t secure_connection = 1u << 15;
inline constexpr std::uint32_t multi_statements = 1u << 16;
inline constexpr std::uint32_t multi_results = 1u << 17;
inline constexpr std::uint32_t ps_multi_results = 1u << 18;
inline constexpr std::uint32_t plugin_auth = 1u << 19;
inline constexpr std::uint32_t connect_attrs = 1u << 20;
inline constexpr std::uint32_t plugin_auth_lenenc_client_data = 1u << 21;
inline constexpr std::uint32_t session_track = 1u << 23;
inline constexpr std::uint32_t deprecate_eof = 1u << 24;
inline constexpr std::uint32_t zstd_compression_algorithm = 1u << 26;
inline constexpr std::uint32_t query_attributes = 1u << 27;
inline constexpr std::uint32_t multi_factor_authentication = 1u << 28;

} // namespace capability

inline constexpr std::uint16_t server_session_state_changed = 0x4000;

enum class handshake_status
{
    complete,
    incomplete,
    invalid,
    unsupported
};

struct server_greeting
{
    std::string server_version;
    std::uint32_t connection_id = 0;
    std::uint32_t capabilities = 0;
    std::uint8_t character_set = 0;
    std::uint16_t status_flags = 0;
    std::string auth_plugin_name;
    std::string auth_plugin_data;
};

inline handshake_status parse_greeting(std::string_view payload, server_greeting& output) {
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (value != 10)
    {
        return handshake_status::unsupported;
    }
    server_greeting result;
    std::string_view text;
    if (reader.read_null_terminated(text) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.server_version = text;
    if (reader.read_integer(4, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.connection_id = static_cast<std::uint32_t>(value);
    if (reader.read_bytes(8, text) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.auth_plugin_data = text;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (value != 0)
    {
        return handshake_status::invalid;
    }
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.capabilities = static_cast<std::uint32_t>(value);
    if ((result.capabilities & capability::protocol_41) == 0)
    {
        return handshake_status::unsupported;
    }
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.character_set = static_cast<std::uint8_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.status_flags = static_cast<std::uint16_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.capabilities |= static_cast<std::uint32_t>(value) << 16;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    const auto auth_size = static_cast<std::size_t>(value);
    if (reader.read_bytes(10, text) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (std::any_of(text.begin(), text.end(), [](char byte) { return byte != 0; }))
    {
        return handshake_status::invalid;
    }
    if ((result.capabilities & (capability::secure_connection | capability::plugin_auth)) == 0)
    {
        return handshake_status::unsupported;
    }
    const bool plugin_auth = (result.capabilities & capability::plugin_auth) != 0;
    if ((!plugin_auth && auth_size != 0) || (plugin_auth && auth_size != 0 && auth_size < 8))
    {
        return handshake_status::invalid;
    }
    // The declared count includes all opaque bytes actually emitted by MySQL.
    // A zero count in the legacy format leaves the fixed 13-byte suffix.
    const auto suffix_size = auth_size == 0 ? 13 : auth_size - 8;
    if (reader.read_bytes(suffix_size, text) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.auth_plugin_data.append(text);
    if (plugin_auth)
    {
        if (reader.read_null_terminated(text) != read_status::complete)
        {
            return handshake_status::incomplete;
        }
        if (text.empty())
        {
            return handshake_status::invalid;
        }
        result.auth_plugin_name = text;
    }
    else
    {
        result.auth_plugin_name = "mysql_native_password";
    }
    if (reader.remaining() != 0)
    {
        return handshake_status::invalid;
    }
    output = std::move(result);
    return handshake_status::complete;
}

// Known password methods use a 20-byte scramble, optionally followed by its
// protocol NUL. Other plugins keep their opaque data and need their own method.
inline handshake_status password_scramble(std::string_view plugin, std::string_view data, std::string_view& output) {
    if (plugin != "mysql_native_password" && plugin != "caching_sha2_password" && plugin != "sha256_password")
    {
        return handshake_status::unsupported;
    }
    if (data.size() == 21 && data.back() == '\0')
    {
        data.remove_suffix(1);
    }
    if (data.size() != 20)
    {
        return handshake_status::invalid;
    }
    output = data;
    return handshake_status::complete;
}

struct handshake_response
{
    std::uint32_t capabilities = capability::protocol_41 | capability::secure_connection;
    std::uint32_t max_packet_size = static_cast<std::uint32_t>(default_message_limit);
    std::uint8_t character_set = 45;
    std::string_view username;
    std::string_view auth_response;
    std::string_view database;
    std::string_view auth_plugin_name;
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
};

inline handshake_status validate_handshake_capabilities(std::uint32_t flags) {
    constexpr auto supported = capability::long_password | capability::found_rows | capability::long_flag |
                               capability::connect_with_db | capability::ignore_space | capability::protocol_41 |
                               capability::interactive | capability::ssl | capability::ignore_sigpipe |
                               capability::transactions | capability::secure_connection | capability::multi_statements |
                               capability::multi_results | capability::ps_multi_results | capability::plugin_auth |
                               capability::connect_attrs | capability::plugin_auth_lenenc_client_data |
                               capability::session_track | capability::deprecate_eof;
    if ((flags & capability::protocol_41) == 0 || (flags & ~supported) != 0 ||
        (flags & (capability::secure_connection | capability::plugin_auth)) == 0)
    {
        return handshake_status::unsupported;
    }
    if ((flags & capability::plugin_auth_lenenc_client_data) != 0 && (flags & capability::plugin_auth) == 0)
    {
        return handshake_status::invalid;
    }
    return handshake_status::complete;
}

// This is only the 32-byte payload; encode_message supplies the Classic header.
inline handshake_status encode_ssl_request(
    std::uint32_t capabilities, std::uint32_t max_packet_size, std::uint8_t character_set, std::string& output) {
    const auto status = validate_handshake_capabilities(capabilities);
    if (status != handshake_status::complete)
    {
        return status;
    }
    if ((capabilities & capability::ssl) == 0 || max_packet_size == 0)
    {
        return handshake_status::invalid;
    }
    std::string encoded;
    append_integer(encoded, capabilities, 4);
    append_integer(encoded, max_packet_size, 4);
    encoded.push_back(static_cast<char>(character_set));
    encoded.append(23, '\0');
    output = std::move(encoded);
    return handshake_status::complete;
}

inline handshake_status encode_handshake_response(const handshake_response& response, std::string& output) {
    const auto flags = response.capabilities;
    const auto status = validate_handshake_capabilities(flags);
    if (status != handshake_status::complete)
    {
        return status;
    }
    const bool lenenc_auth = (flags & capability::plugin_auth_lenenc_client_data) != 0;
    const bool has_database = (flags & capability::connect_with_db) != 0;
    const bool has_plugin = (flags & capability::plugin_auth) != 0;
    const bool has_attributes = (flags & capability::connect_attrs) != 0;
    if (response.max_packet_size == 0 || response.username.find('\0') != std::string_view::npos ||
        response.database.find('\0') != std::string_view::npos ||
        response.auth_plugin_name.find('\0') != std::string_view::npos ||
        (!has_database && !response.database.empty()) || (!has_plugin && !response.auth_plugin_name.empty()) ||
        (has_plugin && response.auth_plugin_name.empty()) || (!has_attributes && !response.attributes.empty()) ||
        (!lenenc_auth && response.auth_response.size() > 255))
    {
        return handshake_status::invalid;
    }
    std::string encoded;
    append_integer(encoded, flags, 4);
    append_integer(encoded, response.max_packet_size, 4);
    encoded.push_back(static_cast<char>(response.character_set));
    encoded.append(23, '\0');
    encoded.append(response.username);
    encoded.push_back('\0');
    if (lenenc_auth)
    {
        append_length(encoded, response.auth_response.size());
    }
    else
    {
        encoded.push_back(static_cast<char>(response.auth_response.size()));
    }
    encoded.append(response.auth_response);
    if (has_database)
    {
        encoded.append(response.database);
        encoded.push_back('\0');
    }
    if (has_plugin)
    {
        encoded.append(response.auth_plugin_name);
        encoded.push_back('\0');
    }
    if (has_attributes)
    {
        std::string attributes;
        for (const auto& [key, value] : response.attributes)
        {
            append_string(attributes, key);
            append_string(attributes, value);
        }
        append_string(encoded, attributes);
    }
    output = std::move(encoded);
    return handshake_status::complete;
}

struct ok_packet
{
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::uint16_t status_flags = 0;
    std::uint16_t warnings = 0;
    std::string info;
    std::string session_state;
};

inline handshake_status required_length(wire_reader& reader, std::uint64_t& output) {
    std::optional<std::uint64_t> value;
    const auto status = reader.read_length(value);
    if (status == read_status::incomplete)
    {
        return handshake_status::incomplete;
    }
    if (status == read_status::invalid || !value)
    {
        return handshake_status::invalid;
    }
    output = *value;
    return handshake_status::complete;
}

inline handshake_status required_string(wire_reader& reader, std::string& output) {
    std::optional<std::string_view> value;
    const auto status = reader.read_string(value);
    if (status == read_status::incomplete)
    {
        return handshake_status::incomplete;
    }
    if (status == read_status::invalid || !value)
    {
        return handshake_status::invalid;
    }
    output.assign(*value);
    return handshake_status::complete;
}

inline handshake_status parse_ok(std::string_view payload, std::uint32_t capabilities, ok_packet& output) {
    if ((capabilities & capability::protocol_41) == 0)
    {
        return handshake_status::unsupported;
    }
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (value != 0 && (value != 0xfe || (capabilities & capability::deprecate_eof) == 0))
    {
        return handshake_status::invalid;
    }
    ok_packet result;
    auto status = required_length(reader, result.affected_rows);
    if (status != handshake_status::complete)
    {
        return status;
    }
    status = required_length(reader, result.last_insert_id);
    if (status != handshake_status::complete)
    {
        return status;
    }
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.status_flags = static_cast<std::uint16_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    result.warnings = static_cast<std::uint16_t>(value);
    const bool state_changed =
        (capabilities & capability::session_track) != 0 && (result.status_flags & server_session_state_changed) != 0;
    // MySQL 8/9 net_send_ok stores nonempty info with a length prefix even
    // without SESSION_TRACK; the published string<EOF> table is inconsistent.
    if (reader.remaining() != 0 || state_changed)
    {
        status = required_string(reader, result.info);
        if (status != handshake_status::complete)
        {
            return status;
        }
    }
    if (state_changed)
    {
        status = required_string(reader, result.session_state);
        if (status != handshake_status::complete)
        {
            return status;
        }
    }
    if (reader.remaining() != 0)
    {
        return handshake_status::invalid;
    }
    output = std::move(result);
    return handshake_status::complete;
}

struct error_packet
{
    std::uint16_t code = 0;
    std::string sql_state;
    std::string message;
};

inline handshake_status parse_error(std::string_view payload, std::uint32_t capabilities, error_packet& output) {
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (value != 0xff)
    {
        return handshake_status::invalid;
    }
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    error_packet result;
    result.code = static_cast<std::uint16_t>(value);
    std::string_view text;
    if ((capabilities & capability::protocol_41) != 0)
    {
        if (reader.read_integer(1, value) != read_status::complete)
        {
            return handshake_status::incomplete;
        }
        if (value != '#')
        {
            return handshake_status::invalid;
        }
        if (reader.read_bytes(5, text) != read_status::complete)
        {
            return handshake_status::incomplete;
        }
        result.sql_state = text;
    }
    reader.read_bytes(reader.remaining(), text);
    result.message = text;
    output = std::move(result);
    return handshake_status::complete;
}

struct auth_switch_request
{
    std::string auth_plugin_name;
    std::string auth_plugin_data;
};

inline handshake_status parse_auth_switch(std::string_view payload, auth_switch_request& output) {
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (value != 0xfe)
    {
        return handshake_status::invalid;
    }
    if (reader.remaining() == 0)
    {
        return handshake_status::unsupported; // Old Password Authentication request.
    }
    auth_switch_request result;
    std::string_view text;
    if (reader.read_null_terminated(text) != read_status::complete)
    {
        return handshake_status::incomplete;
    }
    if (text.empty())
    {
        return handshake_status::invalid;
    }
    result.auth_plugin_name = text;
    reader.read_bytes(reader.remaining(), text);
    result.auth_plugin_data = text;
    output = std::move(result);
    return handshake_status::complete;
}

} // namespace arknet::mysql::detail
