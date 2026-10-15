// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <arknet/mysql/detail/handshake.hpp>

namespace arknet::mysql::detail {

// Classic command/result payloads, without QUERY_ATTRIBUTES or optional metadata.
// https://dev.mysql.com/doc/dev/mysql-server/9.7.2/page_protocol_binary_resultset.html
enum class command_status
{
    complete,
    incomplete,
    invalid,
    unsupported,
    limit_exceeded
};

enum class result_phase
{
    initial,
    columns,
    metadata_end,
    rows,
    complete,
    failed
};

enum class field_type : std::uint8_t
{
    decimal = 0,
    tiny = 1,
    short_integer = 2,
    long_integer = 3,
    float_number = 4,
    double_number = 5,
    null = 6,
    timestamp = 7,
    longlong = 8,
    int24 = 9,
    date = 10,
    time = 11,
    datetime = 12,
    year = 13,
    varchar = 15,
    bit = 16,
    json = 245,
    newdecimal = 246,
    enumeration = 247,
    set = 248,
    tiny_blob = 249,
    medium_blob = 250,
    long_blob = 251,
    blob = 252,
    var_string = 253,
    string = 254,
    geometry = 255
};

inline constexpr std::uint16_t unsigned_column_flag = 0x20;
inline constexpr std::uint32_t optional_metadata_capability = 1u << 25;

struct codec_limits
{
    std::size_t max_columns = 4096;
    std::size_t max_parameters = 65535;
    std::size_t max_bytes = default_message_limit;
};

struct date_value
{
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;

    bool operator==(const date_value&) const = default;
};

struct datetime_value
{
    date_value date;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint32_t microsecond = 0;

    bool operator==(const datetime_value&) const = default;
};

struct time_value
{
    bool negative = false;
    std::uint32_t days = 0;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint32_t microsecond = 0;

    bool operator==(const time_value&) const = default;
};

// DECIMAL, BIT and other byte/string fields remain strings to avoid precision
// loss and charset assumptions. Returned strings own their data.
using field_value = std::variant<std::monostate, std::int64_t, std::uint64_t, double, std::string, date_value,
    datetime_value, time_value>;
using row_values = std::vector<field_value>;

struct column_definition
{
    std::string catalog;
    std::string schema;
    std::string table;
    std::string original_table;
    std::string name;
    std::string original_name;
    std::uint16_t character_set = 0;
    std::uint32_t column_length = 0;
    field_type type = field_type::null;
    std::uint16_t flags = 0;
    std::uint8_t decimals = 0;
};

struct prepared_statement_ok
{
    std::uint32_t statement_id = 0;
    std::uint16_t column_count = 0;
    std::uint16_t parameter_count = 0;
    std::uint16_t warnings = 0;
};

inline command_status command_read_status(read_status status) {
    return status == read_status::complete     ? command_status::complete
           : status == read_status::incomplete ? command_status::incomplete
                                               : command_status::invalid;
}

inline bool string_field_type(field_type type) {
    switch (type)
    {
    case field_type::decimal:
    case field_type::varchar:
    case field_type::bit:
    case field_type::json:
    case field_type::newdecimal:
    case field_type::enumeration:
    case field_type::set:
    case field_type::tiny_blob:
    case field_type::medium_blob:
    case field_type::long_blob:
    case field_type::blob:
    case field_type::var_string:
    case field_type::string:
    case field_type::geometry:
        return true;
    default:
        return false;
    }
}

inline unsigned integer_field_bits(field_type type) {
    switch (type)
    {
    case field_type::tiny:
        return 8;
    case field_type::short_integer:
    case field_type::year:
        return 16;
    case field_type::int24:
        return 24;
    case field_type::long_integer:
        return 32;
    case field_type::longlong:
        return 64;
    default:
        return 0;
    }
}

inline bool supported_field_type(field_type type) {
    return string_field_type(type) || integer_field_bits(type) != 0 || type == field_type::null ||
           type == field_type::float_number || type == field_type::double_number || type == field_type::date ||
           type == field_type::datetime || type == field_type::timestamp || type == field_type::time;
}

inline command_status encode_sql_command(
    std::uint8_t command, std::string_view sql, std::string& output, const codec_limits& limits = {}) {
    if (limits.max_bytes == 0 || sql.size() > limits.max_bytes - 1)
    {
        return command_status::limit_exceeded;
    }
    std::string encoded(1, static_cast<char>(command));
    encoded.append(sql);
    output = std::move(encoded);
    return command_status::complete;
}

inline command_status encode_query(std::string_view sql, std::string& output, const codec_limits& limits = {}) {
    return encode_sql_command(0x03, sql, output, limits);
}

inline command_status encode_prepare(std::string_view sql, std::string& output, const codec_limits& limits = {}) {
    return encode_sql_command(0x16, sql, output, limits);
}

inline command_status encode_close(std::uint32_t statement_id, std::string& output, const codec_limits& limits = {}) {
    if (limits.max_bytes < 5)
    {
        return command_status::limit_exceeded;
    }
    std::string encoded(1, '\x19');
    append_integer(encoded, statement_id, 4);
    output = std::move(encoded);
    return command_status::complete;
}

inline command_status parse_prepare_ok(std::string_view payload, prepared_statement_ok& output,
    const codec_limits& limits = {}, std::uint32_t capabilities = capability::protocol_41) {
    if ((capabilities & capability::protocol_41) == 0 || (capabilities & optional_metadata_capability) != 0)
    {
        return command_status::unsupported;
    }
    if (payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if (value != 0)
    {
        return command_status::invalid;
    }
    prepared_statement_ok result;
    if (reader.read_integer(4, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.statement_id = static_cast<std::uint32_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.column_count = static_cast<std::uint16_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.parameter_count = static_cast<std::uint16_t>(value);
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if (value != 0)
    {
        return command_status::invalid;
    }
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.warnings = static_cast<std::uint16_t>(value);
    if (reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    if (result.column_count > limits.max_columns || result.parameter_count > limits.max_parameters)
    {
        return command_status::limit_exceeded;
    }
    output = result;
    return command_status::complete;
}

inline command_status parse_column_count(std::string_view payload, std::size_t& output, const codec_limits& limits = {},
    std::uint32_t capabilities = capability::protocol_41) {
    if ((capabilities & capability::protocol_41) == 0 || (capabilities & optional_metadata_capability) != 0)
    {
        return command_status::unsupported;
    }
    if (payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    wire_reader reader(payload);
    std::optional<std::uint64_t> count;
    const auto status = reader.read_length(count);
    if (status != read_status::complete)
    {
        return command_read_status(status);
    }
    if (!count || *count == 0 || reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    if (*count > limits.max_columns)
    {
        return command_status::limit_exceeded;
    }
    output = static_cast<std::size_t>(*count);
    return command_status::complete;
}

inline command_status parse_eof(std::string_view payload, ok_packet& output, const codec_limits& limits = {}) {
    if (payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    wire_reader reader(payload);
    std::uint64_t value;
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if (value != 0xfe)
    {
        return command_status::invalid;
    }
    ok_packet result;
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.warnings = static_cast<std::uint16_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.status_flags = static_cast<std::uint16_t>(value);
    if (reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    output = std::move(result);
    return command_status::complete;
}

inline command_status parse_column(
    std::string_view payload, column_definition& output, const codec_limits& limits = {}) {
    if (payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    wire_reader reader(payload);
    column_definition result;
    for (auto* target :
        {&result.catalog, &result.schema, &result.table, &result.original_table, &result.name, &result.original_name})
    {
        const auto status = required_string(reader, *target);
        if (status != handshake_status::complete)
        {
            return status == handshake_status::incomplete ? command_status::incomplete : command_status::invalid;
        }
    }
    std::uint64_t value;
    const auto status = required_length(reader, value);
    if (status != handshake_status::complete)
    {
        return status == handshake_status::incomplete ? command_status::incomplete : command_status::invalid;
    }
    if (value != 12 || result.catalog != "def")
    {
        return command_status::invalid;
    }
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.character_set = static_cast<std::uint16_t>(value);
    if (reader.read_integer(4, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.column_length = static_cast<std::uint32_t>(value);
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.type = static_cast<field_type>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.flags = static_cast<std::uint16_t>(value);
    if (reader.read_integer(1, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    result.decimals = static_cast<std::uint8_t>(value);
    if (reader.read_integer(2, value) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if (value != 0 || reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    if (!supported_field_type(result.type))
    {
        return command_status::unsupported;
    }
    output = std::move(result);
    return command_status::complete;
}

inline bool valid_date(const date_value& value) {
    // MySQL can contain zero dates or component-valid dates under permissive SQL modes.
    return value.year <= 9999 && value.month <= 12 && value.day <= 31;
}

inline bool valid_time_parts(unsigned hour, unsigned minute, unsigned second, std::uint32_t microsecond) {
    return hour < 24 && minute < 60 && second < 60 && microsecond < 1000000;
}

inline bool parse_digits(std::string_view text, std::uint64_t& output) {
    if (text.empty())
    {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc() && end == text.data() + text.size();
}

inline bool parse_text_date(std::string_view text, date_value& output) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    {
        return false;
    }
    std::uint64_t year, month, day;
    if (!parse_digits(text.substr(0, 4), year) || !parse_digits(text.substr(5, 2), month) ||
        !parse_digits(text.substr(8, 2), day) || year > 9999 || month > 12 || day > 31)
    {
        return false;
    }
    output = {static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month), static_cast<std::uint8_t>(day)};
    return true;
}

inline bool parse_text_microseconds(std::string_view text, std::uint32_t& output) {
    output = 0;
    if (text.empty())
    {
        return true;
    }
    if (text.size() < 2 || text.size() > 7 || text.front() != '.')
    {
        return false;
    }
    std::uint64_t microsecond;
    if (!parse_digits(text.substr(1), microsecond))
    {
        return false;
    }
    for (std::size_t width = text.size() - 1; width < 6; ++width)
    {
        microsecond *= 10;
    }
    output = static_cast<std::uint32_t>(microsecond);
    return true;
}

inline bool parse_text_datetime(std::string_view text, datetime_value& output) {
    if (text.size() < 19 || text[10] != ' ' || text[13] != ':' || text[16] != ':')
    {
        return false;
    }
    datetime_value value;
    std::uint64_t hour, minute, second;
    if (!parse_text_date(text.substr(0, 10), value.date) || !parse_digits(text.substr(11, 2), hour) ||
        !parse_digits(text.substr(14, 2), minute) || !parse_digits(text.substr(17, 2), second) ||
        !parse_text_microseconds(text.substr(19), value.microsecond) ||
        !valid_time_parts(hour, minute, second, value.microsecond))
    {
        return false;
    }
    value.hour = static_cast<std::uint8_t>(hour);
    value.minute = static_cast<std::uint8_t>(minute);
    value.second = static_cast<std::uint8_t>(second);
    output = value;
    return true;
}

inline bool parse_text_time(std::string_view text, time_value& output) {
    time_value value;
    if (!text.empty() && text.front() == '-')
    {
        value.negative = true;
        text.remove_prefix(1);
    }
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon < 2 || text.size() < colon + 6 || text[colon + 3] != ':')
    {
        return false;
    }
    std::uint64_t hours, minute, second;
    if (!parse_digits(text.substr(0, colon), hours) || !parse_digits(text.substr(colon + 1, 2), minute) ||
        !parse_digits(text.substr(colon + 4, 2), second) ||
        !parse_text_microseconds(text.substr(colon + 6), value.microsecond) ||
        hours / 24 > std::numeric_limits<std::uint32_t>::max() ||
        !valid_time_parts(hours % 24, minute, second, value.microsecond))
    {
        return false;
    }
    value.days = static_cast<std::uint32_t>(hours / 24);
    value.hour = static_cast<std::uint8_t>(hours % 24);
    value.minute = static_cast<std::uint8_t>(minute);
    value.second = static_cast<std::uint8_t>(second);
    output = value;
    return true;
}

inline bool unsigned_field(const column_definition& column) {
    return (column.flags & unsigned_column_flag) != 0 || column.type == field_type::year;
}

inline bool integer_in_range(std::uint64_t value, unsigned bits) {
    return bits == 64 || value < (std::uint64_t(1) << bits);
}

inline bool integer_in_range(std::int64_t value, unsigned bits) {
    return bits == 64 || (value >= -(std::int64_t(1) << (bits - 1)) && value < (std::int64_t(1) << (bits - 1)));
}

inline command_status text_field(std::string_view text, const column_definition& column, field_value& output) {
    const auto bits = integer_field_bits(column.type);
    if (bits != 0)
    {
        if (unsigned_field(column))
        {
            std::uint64_t value;
            if (!parse_digits(text, value) || !integer_in_range(value, bits))
            {
                return command_status::invalid;
            }
            output = value;
        }
        else
        {
            if (text.empty())
            {
                return command_status::invalid;
            }
            std::int64_t value;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc() || end != text.data() + text.size() || !integer_in_range(value, bits))
            {
                return command_status::invalid;
            }
            output = value;
        }
    }
    else if (column.type == field_type::double_number || column.type == field_type::float_number)
    {
        if (text.empty())
        {
            return command_status::invalid;
        }
        double value;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value) ||
            (column.type == field_type::float_number && std::abs(value) > std::numeric_limits<float>::max()))
        {
            return command_status::invalid;
        }
        output = value;
    }
    else if (string_field_type(column.type))
    {
        output = std::string(text);
    }
    else if (column.type == field_type::date)
    {
        date_value value;
        if (!parse_text_date(text, value))
        {
            return command_status::invalid;
        }
        output = value;
    }
    else if (column.type == field_type::datetime || column.type == field_type::timestamp)
    {
        datetime_value value;
        if (!parse_text_datetime(text, value))
        {
            return command_status::invalid;
        }
        output = value;
    }
    else if (column.type == field_type::time)
    {
        time_value value;
        if (!parse_text_time(text, value))
        {
            return command_status::invalid;
        }
        output = value;
    }
    else
    {
        return column.type == field_type::null ? command_status::invalid : command_status::unsupported;
    }
    return command_status::complete;
}

inline command_status parse_text_row(std::string_view payload, std::span<const column_definition> columns,
    row_values& output, const codec_limits& limits = {}) {
    if (columns.size() > limits.max_columns || payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    if (columns.empty())
    {
        return command_status::invalid;
    }
    wire_reader reader(payload);
    row_values row;
    row.reserve(columns.size());
    for (const auto& column : columns)
    {
        std::optional<std::string_view> text;
        const auto status = reader.read_string(text);
        if (status != read_status::complete)
        {
            return command_read_status(status);
        }
        if (!supported_field_type(column.type))
        {
            return command_status::unsupported;
        }
        field_value value;
        if (text)
        {
            const auto field_status = text_field(*text, column, value);
            if (field_status != command_status::complete)
            {
                return field_status;
            }
        }
        row.push_back(std::move(value));
    }
    if (reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    output = std::move(row);
    return command_status::complete;
}

inline command_status binary_temporal(wire_reader& reader, field_type type, field_value& output) {
    std::uint64_t length;
    if (reader.read_integer(1, length) != read_status::complete)
    {
        return command_status::incomplete;
    }
    const bool is_time = type == field_type::time;
    if ((is_time && length != 0 && length != 8 && length != 12) ||
        (!is_time && length != 0 && length != 4 && length != 7 && length != 11) ||
        (type == field_type::date && length != 0 && length != 4))
    {
        return command_status::invalid;
    }
    std::string_view bytes;
    if (reader.read_bytes(static_cast<std::size_t>(length), bytes) != read_status::complete)
    {
        return command_status::incomplete;
    }
    wire_reader parts(bytes);
    std::uint64_t value = 0;
    if (is_time)
    {
        time_value time;
        if (length != 0)
        {
            parts.read_integer(1, value);
            if (value > 1)
            {
                return command_status::invalid;
            }
            time.negative = value == 1;
            parts.read_integer(4, value);
            time.days = static_cast<std::uint32_t>(value);
            parts.read_integer(1, value);
            time.hour = static_cast<std::uint8_t>(value);
            parts.read_integer(1, value);
            time.minute = static_cast<std::uint8_t>(value);
            parts.read_integer(1, value);
            time.second = static_cast<std::uint8_t>(value);
            if (length == 12)
            {
                parts.read_integer(4, value);
                time.microsecond = static_cast<std::uint32_t>(value);
            }
        }
        if (!valid_time_parts(time.hour, time.minute, time.second, time.microsecond))
        {
            return command_status::invalid;
        }
        output = time;
    }
    else
    {
        datetime_value datetime;
        if (length >= 4)
        {
            parts.read_integer(2, value);
            datetime.date.year = static_cast<std::uint16_t>(value);
            parts.read_integer(1, value);
            datetime.date.month = static_cast<std::uint8_t>(value);
            parts.read_integer(1, value);
            datetime.date.day = static_cast<std::uint8_t>(value);
        }
        if (length >= 7)
        {
            parts.read_integer(1, value);
            datetime.hour = static_cast<std::uint8_t>(value);
            parts.read_integer(1, value);
            datetime.minute = static_cast<std::uint8_t>(value);
            parts.read_integer(1, value);
            datetime.second = static_cast<std::uint8_t>(value);
        }
        if (length == 11)
        {
            parts.read_integer(4, value);
            datetime.microsecond = static_cast<std::uint32_t>(value);
        }
        if (!valid_date(datetime.date) ||
            !valid_time_parts(datetime.hour, datetime.minute, datetime.second, datetime.microsecond))
        {
            return command_status::invalid;
        }
        output = type == field_type::date ? field_value(datetime.date) : field_value(datetime);
    }
    return command_status::complete;
}

inline command_status binary_field(wire_reader& reader, const column_definition& column, field_value& output) {
    const auto bits = integer_field_bits(column.type);
    if (bits != 0 || column.type == field_type::float_number || column.type == field_type::double_number)
    {
        const auto width = column.type == field_type::int24 || column.type == field_type::float_number ? 4
                           : column.type == field_type::double_number                                  ? 8
                                                                                                       : bits / 8;
        std::uint64_t value;
        const auto status = reader.read_integer(width, value);
        if (status != read_status::complete)
        {
            return command_read_status(status);
        }
        if (bits != 0)
        {
            if (unsigned_field(column))
            {
                if (!integer_in_range(value, bits))
                {
                    return command_status::invalid;
                }
                output = value;
            }
            else
            {
                if (width != 8 && (value & (std::uint64_t(1) << (width * 8 - 1))) != 0)
                {
                    value |= std::numeric_limits<std::uint64_t>::max() << (width * 8);
                }
                const auto signed_value = std::bit_cast<std::int64_t>(value);
                if (!integer_in_range(signed_value, bits))
                {
                    return command_status::invalid;
                }
                output = signed_value;
            }
        }
        else
        {
            const double number = column.type == field_type::float_number
                                      ? static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(value)))
                                      : std::bit_cast<double>(value);
            if (!std::isfinite(number))
            {
                return command_status::invalid;
            }
            output = number;
        }
        return command_status::complete;
    }
    if (string_field_type(column.type))
    {
        std::optional<std::string_view> text;
        const auto status = reader.read_string(text);
        if (status != read_status::complete)
        {
            return command_read_status(status);
        }
        if (!text)
        {
            return command_status::invalid; // Binary NULL belongs in the bitmap.
        }
        output = std::string(*text);
        return command_status::complete;
    }
    if (column.type == field_type::date || column.type == field_type::datetime ||
        column.type == field_type::timestamp || column.type == field_type::time)
    {
        return binary_temporal(reader, column.type, output);
    }
    return column.type == field_type::null ? command_status::invalid : command_status::unsupported;
}

inline command_status parse_binary_row(std::string_view payload, std::span<const column_definition> columns,
    row_values& output, const codec_limits& limits = {}) {
    if (columns.size() > limits.max_columns || payload.size() > limits.max_bytes)
    {
        return command_status::limit_exceeded;
    }
    if (columns.empty())
    {
        return command_status::invalid;
    }
    wire_reader reader(payload);
    std::uint64_t header;
    if (reader.read_integer(1, header) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if (header != 0)
    {
        return command_status::invalid;
    }
    // Divide before adding so user-selected limits cannot overflow size_t.
    const auto bitmap_size = columns.size() / 8 + (columns.size() % 8 + 9) / 8;
    std::string_view bitmap;
    if (reader.read_bytes(bitmap_size, bitmap) != read_status::complete)
    {
        return command_status::incomplete;
    }
    if ((static_cast<unsigned char>(bitmap[0]) & 3) != 0)
    {
        return command_status::invalid;
    }
    row_values row;
    row.reserve(columns.size());
    for (std::size_t index = 0; index < columns.size(); ++index)
    {
        if (!supported_field_type(columns[index].type))
        {
            return command_status::unsupported;
        }
        const auto bit = index + 2;
        field_value value;
        if ((static_cast<unsigned char>(bitmap[bit / 8]) & (1u << (bit % 8))) == 0)
        {
            const auto status = binary_field(reader, columns[index], value);
            if (status != command_status::complete)
            {
                return status;
            }
        }
        row.push_back(std::move(value));
    }
    if (reader.remaining() != 0)
    {
        return command_status::invalid;
    }
    output = std::move(row);
    return command_status::complete;
}

inline field_type parameter_type(const field_value& value) {
    constexpr field_type types[] = {field_type::null, field_type::longlong, field_type::longlong,
        field_type::double_number, field_type::var_string, field_type::date, field_type::datetime, field_type::time};
    return types[value.index()];
}

inline command_status append_parameter(std::string& encoded, const field_value& value) {
    if (value.valueless_by_exception())
    {
        return command_status::invalid;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        append_integer(encoded, std::bit_cast<std::uint64_t>(*integer), 8);
    }
    else if (const auto* integer = std::get_if<std::uint64_t>(&value))
    {
        append_integer(encoded, *integer, 8);
    }
    else if (const auto* number = std::get_if<double>(&value))
    {
        if (!std::isfinite(*number))
        {
            return command_status::invalid;
        }
        append_integer(encoded, std::bit_cast<std::uint64_t>(*number), 8);
    }
    else if (const auto* text = std::get_if<std::string>(&value))
    {
        append_string(encoded, *text);
    }
    else if (const auto* time = std::get_if<time_value>(&value))
    {
        if (!valid_time_parts(time->hour, time->minute, time->second, time->microsecond))
        {
            return command_status::invalid;
        }
        const unsigned size = time->microsecond != 0                                                         ? 12
                              : time->days != 0 || time->hour != 0 || time->minute != 0 || time->second != 0 ? 8
                                                                                                             : 0;
        encoded.push_back(static_cast<char>(size));
        if (size != 0)
        {
            encoded.push_back(time->negative ? 1 : 0);
            append_integer(encoded, time->days, 4);
            encoded.push_back(static_cast<char>(time->hour));
            encoded.push_back(static_cast<char>(time->minute));
            encoded.push_back(static_cast<char>(time->second));
            if (size == 12)
            {
                append_integer(encoded, time->microsecond, 4);
            }
        }
    }
    else if (std::holds_alternative<date_value>(value) || std::holds_alternative<datetime_value>(value))
    {
        const auto date_only = std::holds_alternative<date_value>(value);
        const auto datetime = date_only ? datetime_value{std::get<date_value>(value)} : std::get<datetime_value>(value);
        if (!valid_date(datetime.date) ||
            !valid_time_parts(datetime.hour, datetime.minute, datetime.second, datetime.microsecond))
        {
            return command_status::invalid;
        }
        const unsigned size = datetime.microsecond != 0                                                       ? 11
                              : datetime.hour != 0 || datetime.minute != 0 || datetime.second != 0            ? 7
                              : datetime.date.year != 0 || datetime.date.month != 0 || datetime.date.day != 0 ? 4
                                                                                                              : 0;
        encoded.push_back(static_cast<char>(size));
        if (size >= 4)
        {
            append_integer(encoded, datetime.date.year, 2);
            encoded.push_back(static_cast<char>(datetime.date.month));
            encoded.push_back(static_cast<char>(datetime.date.day));
        }
        if (size >= 7)
        {
            encoded.push_back(static_cast<char>(datetime.hour));
            encoded.push_back(static_cast<char>(datetime.minute));
            encoded.push_back(static_cast<char>(datetime.second));
        }
        if (size == 11)
        {
            append_integer(encoded, datetime.microsecond, 4);
        }
    }
    return command_status::complete;
}

inline command_status encode_execute(std::uint32_t statement_id, std::span<const field_value> parameters,
    std::string& output, const codec_limits& limits = {}) {
    if (parameters.size() > limits.max_parameters || parameters.size() > 65535 || limits.max_bytes < 10)
    {
        return command_status::limit_exceeded;
    }
    std::string encoded(1, '\x17');
    append_integer(encoded, statement_id, 4);
    encoded.push_back('\0'); // No server-side cursor requested.
    append_integer(encoded, 1, 4);
    if (!parameters.empty())
    {
        const auto bitmap_size = (parameters.size() + 7) / 8;
        if (bitmap_size + 1 + parameters.size() * 2 > limits.max_bytes - encoded.size())
        {
            return command_status::limit_exceeded;
        }
        const auto bitmap_offset = encoded.size();
        encoded.append(bitmap_size, '\0');
        encoded.push_back(1); // Always send current types; no hidden cached binding state.
        for (std::size_t index = 0; index < parameters.size(); ++index)
        {
            const auto& parameter = parameters[index];
            if (parameter.valueless_by_exception())
            {
                return command_status::invalid;
            }
            if (std::holds_alternative<std::monostate>(parameter))
            {
                encoded[bitmap_offset + index / 8] = static_cast<char>(
                    static_cast<unsigned char>(encoded[bitmap_offset + index / 8]) | (1u << (index % 8)));
            }
            encoded.push_back(static_cast<char>(parameter_type(parameter)));
            encoded.push_back(std::holds_alternative<std::uint64_t>(parameter) ? static_cast<char>(0x80) : 0);
        }
        for (const auto& parameter : parameters)
        {
            const auto* text = std::get_if<std::string>(&parameter);
            if (text != nullptr && text->size() > limits.max_bytes - encoded.size())
            {
                return command_status::limit_exceeded;
            }
            const auto status = append_parameter(encoded, parameter);
            if (status != command_status::complete)
            {
                return status;
            }
            if (encoded.size() > limits.max_bytes)
            {
                return command_status::limit_exceeded;
            }
        }
    }
    output = std::move(encoded);
    return command_status::complete;
}

} // namespace arknet::mysql::detail
