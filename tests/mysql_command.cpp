#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "vendor/doctest/doctest.h"

#include <arknet/mysql/detail/command.hpp>

using namespace arknet::mysql::detail;

namespace {

column_definition column(field_type type, bool is_unsigned = false) {
    column_definition value;
    value.type = type;
    value.flags = is_unsigned ? unsigned_column_flag : 0;
    return value;
}

std::string metadata(field_type type = field_type::longlong, std::uint16_t flags = unsigned_column_flag) {
    std::string payload;
    for (const auto text : {"def", "schema", "alias_table", "real_table", "alias_name", "real_name"})
    {
        append_string(payload, text);
    }
    payload.push_back(12);
    append_integer(payload, 63, 2);
    append_integer(payload, 0xffffffff, 4);
    payload.push_back(static_cast<char>(type));
    append_integer(payload, flags, 2);
    payload.push_back(31);
    payload.append(2, '\0');
    return payload;
}

std::string text_row(std::initializer_list<std::optional<std::string_view>> values) {
    std::string payload;
    for (const auto& value : values)
    {
        append_string(payload, value);
    }
    return payload;
}

void check_old_row(const row_values& row) {
    REQUIRE(row.size() == 2);
    CHECK(std::get<std::int64_t>(row[0]) == 42);
    CHECK(std::get<std::string>(row[1]) == "unchanged");
}

row_values old_row() {
    return {std::int64_t(42), std::string("unchanged")};
}

std::string binary_from_execute(std::span<const field_value> values, std::vector<column_definition>& columns) {
    std::string execute;
    REQUIRE(encode_execute(1, values, execute) == command_status::complete);
    REQUIRE(!values.empty());
    const auto parameter_bitmap_size = (values.size() + 7) / 8;
    const auto value_offset = 10 + parameter_bitmap_size + 1 + 2 * values.size();
    REQUIRE(execute.size() >= value_offset);
    CHECK(execute[10 + parameter_bitmap_size] == 1);
    const auto row_bitmap_size = (values.size() + 9) / 8;
    std::string payload(1 + row_bitmap_size, '\0');
    columns.clear();
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto type_offset = 10 + parameter_bitmap_size + 1 + index * 2;
        const auto type = static_cast<field_type>(static_cast<unsigned char>(execute[type_offset]));
        columns.push_back(column(type, static_cast<unsigned char>(execute[type_offset + 1]) == 0x80));
        if ((static_cast<unsigned char>(execute[10 + index / 8]) & (1u << (index % 8))) != 0)
        {
            const auto bit = index + 2;
            payload[1 + bit / 8] =
                static_cast<char>(static_cast<unsigned char>(payload[1 + bit / 8]) | (1u << (bit % 8)));
        }
    }
    payload.append(std::string_view(execute).substr(value_offset));
    return payload;
}

} // namespace

DOCTEST_TEST_CASE("MySQL SQL commands preserve raw SQL bytes and close has the documented fixed layout") {
    const std::string sql("SELECT 'a\0b'", 12);
    std::string payload;
    REQUIRE(encode_query(sql, payload) == command_status::complete);
    CHECK(static_cast<unsigned char>(payload[0]) == 3);
    CHECK(std::string_view(payload).substr(1) == sql);
    REQUIRE(encode_prepare("SELECT CONCAT(?, ?) AS col1", payload) == command_status::complete);
    CHECK(payload == std::string("\x16") + "SELECT CONCAT(?, ?) AS col1");
    REQUIRE(encode_close(0x01020304, payload) == command_status::complete);
    CHECK(payload == std::string("\x19\x04\x03\x02\x01", 5));
    codec_limits limits;
    limits.max_bytes = 4;
    payload = "old";
    CHECK(encode_query("1234", payload, limits) == command_status::limit_exceeded);
    CHECK(encode_prepare("1234", payload, limits) == command_status::limit_exceeded);
    CHECK(encode_close(1, payload, limits) == command_status::limit_exceeded);
    CHECK(payload == "old");
    limits.max_bytes = 0;
    CHECK(encode_query("", payload, limits) == command_status::limit_exceeded);
    CHECK(payload == "old");
}

DOCTEST_TEST_CASE("MySQL prepare OK decodes the documented twelve byte fixture and bounds metadata counts") {
    const std::string payload("\0\1\0\0\0\1\0\2\0\0\0\0", 12);
    prepared_statement_ok result;
    REQUIRE(parse_prepare_ok(payload, result) == command_status::complete);
    CHECK(result.statement_id == 1);
    CHECK(result.column_count == 1);
    CHECK(result.parameter_count == 2);
    CHECK(result.warnings == 0);
    for (std::size_t size = 0; size < payload.size(); ++size)
    {
        result = {99, 98, 97, 96};
        CHECK(parse_prepare_ok(std::string_view(payload).substr(0, size), result) == command_status::incomplete);
        CHECK(result.statement_id == 99);
        CHECK(result.column_count == 98);
        CHECK(result.parameter_count == 97);
        CHECK(result.warnings == 96);
    }
    for (const auto offset : {0, 9})
    {
        auto malformed = payload;
        malformed[offset] = 1;
        CHECK(parse_prepare_ok(malformed, result) == command_status::invalid);
        CHECK(result.statement_id == 99);
    }
    CHECK(parse_prepare_ok(payload + 'x', result) == command_status::invalid);
    codec_limits limits;
    limits.max_columns = 0;
    CHECK(parse_prepare_ok(payload, result, limits) == command_status::limit_exceeded);
    limits = {};
    limits.max_parameters = 1;
    CHECK(parse_prepare_ok(payload, result, limits) == command_status::limit_exceeded);
    CHECK(parse_prepare_ok(payload, result, {}, optional_metadata_capability | capability::protocol_41) ==
          command_status::unsupported);
    CHECK(parse_prepare_ok(payload, result, {}, 0) == command_status::unsupported);
    CHECK(result.statement_id == 99);
}

DOCTEST_TEST_CASE("MySQL column count rejects NULL zero overflow unsupported metadata and trailing bytes") {
    std::size_t count = 42;
    for (const auto expected : {1, 250, 251, 4096})
    {
        std::string payload;
        append_length(payload, expected);
        REQUIRE(parse_column_count(payload, count) == command_status::complete);
        CHECK(count == static_cast<std::size_t>(expected));
        for (std::size_t size = 0; size < payload.size(); ++size)
        {
            count = 42;
            CHECK(parse_column_count(std::string_view(payload).substr(0, size), count) == command_status::incomplete);
            CHECK(count == 42);
        }
    }
    count = 42;
    for (const auto byte : {'\0', '\xfb', '\xff'})
    {
        CHECK(parse_column_count(std::string(1, byte), count) == command_status::invalid);
        CHECK(count == 42);
    }
    std::string payload;
    append_length(payload, std::numeric_limits<std::uint64_t>::max());
    CHECK(parse_column_count(payload, count) == command_status::limit_exceeded);
    CHECK(parse_column_count(std::string("\1x", 2), count) == command_status::invalid);
    CHECK(parse_column_count(std::string(1, '\1'), count, {}, optional_metadata_capability | capability::protocol_41) ==
          command_status::unsupported);
    CHECK(count == 42);
}

DOCTEST_TEST_CASE("MySQL ColumnDefinition41 owns six names and reads fixed metadata without allocating column length") {
    auto payload = metadata();
    column_definition result;
    REQUIRE(parse_column(payload, result) == command_status::complete);
    CHECK(result.catalog == "def");
    CHECK(result.schema == "schema");
    CHECK(result.table == "alias_table");
    CHECK(result.original_table == "real_table");
    CHECK(result.name == "alias_name");
    CHECK(result.original_name == "real_name");
    CHECK(result.character_set == 63);
    CHECK(result.column_length == 0xffffffff);
    CHECK(result.type == field_type::longlong);
    CHECK(result.flags == unsigned_column_flag);
    CHECK(result.decimals == 31);
    payload.assign(payload.size(), 'x');
    CHECK(result.name == "alias_name");
    payload = metadata();
    for (std::size_t size = 0; size < payload.size(); ++size)
    {
        result.name = "old";
        CHECK(parse_column(std::string_view(payload).substr(0, size), result) == command_status::incomplete);
        CHECK(result.name == "old");
    }
    auto malformed = payload;
    malformed[0] = '\xfb';
    CHECK(parse_column(malformed, result) == command_status::invalid);
    malformed = payload;
    malformed[1] = 'x';
    CHECK(parse_column(malformed, result) == command_status::invalid);
    malformed = payload;
    malformed[payload.size() - 13] = 11;
    CHECK(parse_column(malformed, result) == command_status::invalid);
    malformed = payload;
    malformed.back() = 1;
    CHECK(parse_column(malformed, result) == command_status::invalid);
    CHECK(parse_column(payload + 'x', result) == command_status::invalid);
    CHECK(parse_column(metadata(static_cast<field_type>(242)), result) == command_status::unsupported);
    CHECK(result.name == "old");
    codec_limits limits;
    limits.max_bytes = payload.size() - 1;
    CHECK(parse_column(payload, result, limits) == command_status::limit_exceeded);
}

DOCTEST_TEST_CASE("MySQL classic EOF distinguishes its warning and status order from OK payloads") {
    const std::string payload("\xfe\x34\x12\x0a\0", 5);
    ok_packet result;
    REQUIRE(parse_eof(payload, result) == command_status::complete);
    CHECK(result.warnings == 0x1234);
    CHECK(result.status_flags == 0x000a);
    CHECK(result.affected_rows == 0);
    CHECK(result.last_insert_id == 0);
    for (std::size_t size = 0; size < payload.size(); ++size)
    {
        result = {42, 43, 44, 45, "old", "state"};
        CHECK(parse_eof(std::string_view(payload).substr(0, size), result) == command_status::incomplete);
        CHECK(result.affected_rows == 42);
        CHECK(result.status_flags == 44);
        CHECK(result.info == "old");
    }
    CHECK(parse_eof(payload + 'x', result) == command_status::invalid);
    CHECK(parse_eof(std::string("\0\0\0\0\0", 5), result) == command_status::invalid);
    CHECK(result.info == "old");
}

DOCTEST_TEST_CASE("MySQL text rows retain NULL unsigned integers exact decimals and binary safe strings") {
    const std::vector columns = {column(field_type::null), column(field_type::longlong),
        column(field_type::longlong, true), column(field_type::double_number), column(field_type::blob),
        column(field_type::var_string), column(field_type::newdecimal), column(field_type::year)};
    const std::string binary("\0\xff\r\n", 4);
    auto payload = text_row({std::nullopt, "-9223372036854775808", "18446744073709551615", "10.25", binary, "",
        "123456789012345678901234567890.0001", "2026"});
    row_values row;
    REQUIRE(parse_text_row(payload, columns, row) == command_status::complete);
    REQUIRE(row.size() == columns.size());
    CHECK(std::holds_alternative<std::monostate>(row[0]));
    CHECK(std::get<std::int64_t>(row[1]) == std::numeric_limits<std::int64_t>::min());
    CHECK(std::get<std::uint64_t>(row[2]) == std::numeric_limits<std::uint64_t>::max());
    CHECK(std::get<double>(row[3]) == 10.25);
    CHECK(std::get<std::string>(row[4]) == binary);
    CHECK(std::get<std::string>(row[5]).empty());
    CHECK(std::get<std::string>(row[6]) == "123456789012345678901234567890.0001");
    CHECK(std::get<std::uint64_t>(row[7]) == 2026);
    payload.assign(payload.size(), 'x');
    CHECK(std::get<std::string>(row[4]) == binary);
}

DOCTEST_TEST_CASE("MySQL text numeric conversion rejects overflow incomplete numbers and nonfinite doubles") {
    const auto check_invalid = [](field_type type, bool is_unsigned, std::string_view text) {
        const std::array columns = {column(type, is_unsigned)};
        auto row = old_row();
        CHECK(parse_text_row(text_row({text}), columns, row) == command_status::invalid);
        check_old_row(row);
    };
    for (const auto text : {"", "1x", " 1", "+1", "9223372036854775808", "-9223372036854775809"})
    {
        check_invalid(field_type::longlong, false, text);
    }
    for (const auto text : {"-1", "18446744073709551616"})
    {
        check_invalid(field_type::longlong, true, text);
    }
    check_invalid(field_type::tiny, false, "128");
    check_invalid(field_type::tiny, false, "-129");
    check_invalid(field_type::tiny, true, "256");
    check_invalid(field_type::short_integer, false, "32768");
    check_invalid(field_type::short_integer, true, "65536");
    check_invalid(field_type::int24, false, "8388608");
    check_invalid(field_type::long_integer, true, "4294967296");
    for (const auto text : {"", "1x", "nan", "inf", "-inf", "1e999", "1e-999"})
    {
        check_invalid(field_type::double_number, false, text);
    }
    check_invalid(field_type::float_number, false, "1e100");
}

DOCTEST_TEST_CASE("MySQL text temporal values preserve zero dates fractional precision and signed durations") {
    const std::array columns = {column(field_type::date), column(field_type::datetime), column(field_type::timestamp),
        column(field_type::time)};
    row_values row;
    REQUIRE(parse_text_row(
                text_row({"0000-00-00", "2026-02-31 12:34:56.123456", "2026-10-15 00:00:00.1", "-838:59:59.999999"}),
                columns, row) == command_status::complete);
    CHECK(std::get<date_value>(row[0]) == date_value{});
    CHECK(std::get<datetime_value>(row[1]) == datetime_value{{2026, 2, 31}, 12, 34, 56, 123456});
    CHECK(std::get<datetime_value>(row[2]) == datetime_value{{2026, 10, 15}, 0, 0, 0, 100000});
    CHECK(std::get<time_value>(row[3]) == time_value{true, 34, 22, 59, 59, 999999});
    for (const auto text : {"2026-13-01", "2026-01-32", "2026-1-01", "xxxx-01-01"})
    {
        const std::array one_column = {column(field_type::date)};
        row = old_row();
        CHECK(parse_text_row(text_row({text}), one_column, row) == command_status::invalid);
        check_old_row(row);
    }
    for (const auto text : {"2026-01-01 24:00:00", "2026-01-01 00:60:00", "2026-01-01 00:00:00.",
             "2026-01-01 00:00:00.1234567", "2026-01-01 00:00:00x"})
    {
        const std::array one_column = {column(field_type::datetime)};
        row = old_row();
        CHECK(parse_text_row(text_row({text}), one_column, row) == command_status::invalid);
        check_old_row(row);
    }
    for (const auto text :
        {"1:00:00", "00:60:00", "00:00:60", "00:00:00.", "18446744073709551616:00:00", "103079215104:00:00"})
    {
        const std::array one_column = {column(field_type::time)};
        row = old_row();
        CHECK(parse_text_row(text_row({text}), one_column, row) == command_status::invalid);
        check_old_row(row);
    }
}

DOCTEST_TEST_CASE("MySQL execute encodes the documented command prefix types unsigned flag and parameter values") {
    const std::string binary("a\0b", 3);
    const std::vector<field_value> values = {std::monostate{}, std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::uint64_t>::max(), 10.25, binary};
    std::string payload;
    REQUIRE(encode_execute(0x01020304, values, payload) == command_status::complete);
    const std::string prefix("\x17\x04\x03\x02\x01\0\1\0\0\0\1\1", 12);
    CHECK(payload.substr(0, 12) == prefix);
    const std::string types("\x06\0\x08\0\x08\x80\x05\0\xfd\0", 10);
    CHECK(payload.substr(12, 10) == types);
    std::string bytes;
    bytes.append("\0\0\0\0\0\0\0\x80", 8);
    bytes.append(8, '\xff');
    bytes.append("\0\0\0\0\0\x80\x24\x40", 8);
    bytes.append("\3a\0b", 4);
    CHECK(payload.substr(22) == bytes);
    REQUIRE(encode_execute(0x01020304, {}, payload) == command_status::complete);
    CHECK(payload == prefix.substr(0, 10));
    std::vector<field_value> nulls(9);
    nulls[1] = std::int64_t(1);
    REQUIRE(encode_execute(1, nulls, payload) == command_status::complete);
    CHECK(static_cast<unsigned char>(payload[10]) == 0xfd);
    CHECK(static_cast<unsigned char>(payload[11]) == 1);
    CHECK(payload[12] == 1);
}

DOCTEST_TEST_CASE("MySQL binary result integers sign extend all widths independently of parameter types") {
    const std::vector columns = {column(field_type::tiny), column(field_type::tiny, true),
        column(field_type::short_integer), column(field_type::short_integer, true), column(field_type::int24),
        column(field_type::long_integer), column(field_type::longlong), column(field_type::longlong, true),
        column(field_type::year), column(field_type::float_number), column(field_type::double_number)};
    std::string payload(3, '\0');
    payload.append("\x80\xff\0\x80\xff\xff", 6);
    payload.append("\0\0\x80\xff\0\0\0\x80", 8);
    payload.append("\0\0\0\0\0\0\0\x80", 8);
    payload.append(8, '\xff');
    payload.append("\xff\xff", 2);
    payload.append("\0\0\x24\x41", 4); // float 10.25.
    payload.append("\0\0\0\0\0\x80\x24\x40", 8);
    row_values row;
    REQUIRE(parse_binary_row(payload, columns, row) == command_status::complete);
    CHECK(std::get<std::int64_t>(row[0]) == -128);
    CHECK(std::get<std::uint64_t>(row[1]) == 255);
    CHECK(std::get<std::int64_t>(row[2]) == -32768);
    CHECK(std::get<std::uint64_t>(row[3]) == 65535);
    CHECK(std::get<std::int64_t>(row[4]) == -8388608);
    CHECK(std::get<std::int64_t>(row[5]) == std::numeric_limits<std::int32_t>::min());
    CHECK(std::get<std::int64_t>(row[6]) == std::numeric_limits<std::int64_t>::min());
    CHECK(std::get<std::uint64_t>(row[7]) == std::numeric_limits<std::uint64_t>::max());
    CHECK(std::get<std::uint64_t>(row[8]) == 65535);
    CHECK(std::get<double>(row[9]) == 10.25);
    CHECK(std::get<double>(row[10]) == 10.25);
}

DOCTEST_TEST_CASE("MySQL result NULL bitmap uses offset two across byte boundaries") {
    const std::vector columns(9, column(field_type::tiny));
    std::string payload("\0\4\5", 3); // Fields 0, 6 and 8 NULL.
    payload.append(6, '\1');
    row_values row;
    REQUIRE(parse_binary_row(payload, columns, row) == command_status::complete);
    for (std::size_t index = 0; index < row.size(); ++index)
    {
        CHECK(std::holds_alternative<std::monostate>(row[index]) == (index == 0 || index == 6 || index == 8));
    }
    auto malformed = payload;
    malformed[1] |= 1;
    row = old_row();
    CHECK(parse_binary_row(malformed, columns, row) == command_status::invalid);
    check_old_row(row);
}

DOCTEST_TEST_CASE("MySQL binary temporal fixtures follow date datetime timestamp and duration layouts") {
    const std::vector columns = {column(field_type::date), column(field_type::datetime), column(field_type::timestamp),
        column(field_type::time)};
    std::string payload("\0\0", 2);
    payload.append("\4\xda\7\x0a\x11", 5);
    payload.append("\x0b\xda\7\x0a\x11\x13\x1b\x1e\1\0\0\0", 12);
    payload.push_back('\0');
    payload.append("\x0c\1\x78\0\0\0\x13\x1b\x1e\1\0\0\0", 13);
    row_values row;
    REQUIRE(parse_binary_row(payload, columns, row) == command_status::complete);
    CHECK(std::get<date_value>(row[0]) == date_value{2010, 10, 17});
    CHECK(std::get<datetime_value>(row[1]) == datetime_value{{2010, 10, 17}, 19, 27, 30, 1});
    CHECK(std::get<datetime_value>(row[2]) == datetime_value{});
    CHECK(std::get<time_value>(row[3]) == time_value{true, 120, 19, 27, 30, 1});
    const std::vector<field_value> temporal = {date_value{}, date_value{2026, 2, 31}, datetime_value{},
        datetime_value{{2026, 10, 15}}, datetime_value{{2026, 10, 15}, 12, 34, 56},
        datetime_value{{2026, 10, 15}, 12, 34, 56, 999999}, time_value{}, time_value{true, 120, 19, 27, 30},
        time_value{true, 120, 19, 27, 30, 999999}};
    std::vector<column_definition> roundtrip_columns;
    const auto encoded = binary_from_execute(temporal, roundtrip_columns);
    REQUIRE(parse_binary_row(encoded, roundtrip_columns, row) == command_status::complete);
    CHECK(row == temporal);
}

DOCTEST_TEST_CASE("MySQL row decoders preserve output on mandatory truncation malformed fields and extra bytes") {
    const std::array columns = {column(field_type::longlong), column(field_type::var_string)};
    const auto text = text_row({"42", "binary"});
    std::string binary("\0\0", 2);
    append_integer(binary, 42, 8);
    append_string(binary, "binary");
    for (const auto& [payload, is_binary] : std::array{std::pair{text, false}, std::pair{binary, true}})
    {
        for (std::size_t size = 0; size < payload.size(); ++size)
        {
            auto row = old_row();
            const auto prefix = std::string_view(payload).substr(0, size);
            CHECK((is_binary ? parse_binary_row(prefix, columns, row) : parse_text_row(prefix, columns, row)) ==
                  command_status::incomplete);
            check_old_row(row);
        }
        auto row = old_row();
        CHECK((is_binary ? parse_binary_row(payload + 'x', columns, row)
                         : parse_text_row(payload + 'x', columns, row)) == command_status::invalid);
        check_old_row(row);
    }
    for (const auto type : {field_type::tiny, field_type::short_integer, field_type::long_integer, field_type::longlong,
             field_type::double_number, field_type::float_number, field_type::var_string, field_type::date,
             field_type::datetime, field_type::time})
    {
        const std::array one_column = {column(type)};
        auto row = old_row();
        CHECK(parse_text_row(std::string(1, '\xff'), one_column, row) == command_status::invalid);
        check_old_row(row);
    }
    const std::array string_column = {column(field_type::blob)};
    auto row = old_row();
    CHECK(parse_binary_row(std::string("\0\0\xfb", 3), string_column, row) == command_status::invalid);
    CHECK(parse_binary_row(std::string("\1\0", 2), string_column, row) == command_status::invalid);
    check_old_row(row);
    for (const auto type : {14, 17, 18, 19, 20, 242, 243, 244})
    {
        const std::array unknown = {column(static_cast<field_type>(type))};
        CHECK(parse_binary_row(std::string("\0\4", 2), unknown, row) == command_status::unsupported);
        CHECK(parse_text_row(std::string(1, '\xfb'), unknown, row) == command_status::unsupported);
        check_old_row(row);
    }
}

DOCTEST_TEST_CASE("MySQL malformed binary temporal numeric and float fields never reach caller output") {
    const auto invalid = [](field_type type, std::string bytes) {
        const std::array columns = {column(type)};
        auto row = old_row();
        CHECK(parse_binary_row(std::string("\0\0", 2) + bytes, columns, row) == command_status::invalid);
        check_old_row(row);
    };
    invalid(field_type::date, std::string(1, '\1'));
    invalid(field_type::datetime, std::string(1, '\5'));
    invalid(field_type::time, std::string(1, '\1'));
    invalid(field_type::time, std::string("\x08\x02\0\0\0\0\0\0\0", 9));
    invalid(field_type::date, std::string("\x04\0\0\x0d\1", 5));
    invalid(field_type::date, std::string("\x04\0\0\1\x20", 5));
    invalid(field_type::datetime, std::string("\x07\0\0\0\0\x18\0\0", 8));
    std::string datetime("\x0b\0\0\0\0\0\0\0", 8);
    append_integer(datetime, 1000000, 4);
    invalid(field_type::datetime, datetime);
    invalid(field_type::int24, std::string("\0\0\x80\0", 4));
    for (const auto number : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        std::string bytes;
        append_integer(bytes, std::bit_cast<std::uint64_t>(number), 8);
        invalid(field_type::double_number, bytes);
    }
}

DOCTEST_TEST_CASE("MySQL binary temporal payloads reject every missing mandatory byte") {
    const std::vector<field_value> values = {date_value{}, date_value{2026, 10, 15}, datetime_value{},
        datetime_value{{2026, 10, 15}}, datetime_value{{2026, 10, 15}, 1, 2, 3},
        datetime_value{{2026, 10, 15}, 1, 2, 3, 4}, time_value{}, time_value{false, 2, 1, 2, 3},
        time_value{true, std::numeric_limits<std::uint32_t>::max(), 23, 59, 59, 999999}};
    for (const auto& value : values)
    {
        const std::array one_value = {value};
        std::vector<column_definition> columns;
        const auto payload = binary_from_execute(one_value, columns);
        for (std::size_t size = 0; size < payload.size(); ++size)
        {
            auto row = old_row();
            CHECK(parse_binary_row(std::string_view(payload).substr(0, size), columns, row) ==
                  command_status::incomplete);
            check_old_row(row);
        }
    }
}

DOCTEST_TEST_CASE("MySQL byte string protocol types preserve arbitrary bytes in both row encodings") {
    const std::string bytes("\xff\0\xfe\xfb", 4);
    for (const auto type :
        {field_type::decimal, field_type::varchar, field_type::bit, field_type::json, field_type::newdecimal,
            field_type::enumeration, field_type::set, field_type::tiny_blob, field_type::medium_blob,
            field_type::long_blob, field_type::blob, field_type::var_string, field_type::string, field_type::geometry})
    {
        const std::array columns = {column(type)};
        auto text = text_row({bytes});
        row_values row;
        CHECK(parse_text_row(text, columns, row) == command_status::complete);
        CHECK(std::get<std::string>(row.at(0)) == bytes);
        CHECK(parse_binary_row(std::string("\0\0", 2) + text, columns, row) == command_status::complete);
        CHECK(std::get<std::string>(row.at(0)) == bytes);
    }
}

DOCTEST_TEST_CASE("MySQL byte and count limits apply before publishing rows or execute payloads") {
    codec_limits limits;
    limits.max_parameters = 1;
    const std::vector<field_value> parameters = {std::int64_t(1), std::int64_t(2)};
    std::string output = "old";
    CHECK(encode_execute(1, parameters, output, limits) == command_status::limit_exceeded);
    CHECK(output == "old");
    limits = {};
    limits.max_bytes = 21;
    CHECK(encode_execute(1, std::span(parameters).first(1), output, limits) == command_status::limit_exceeded);
    CHECK(output == "old");
    limits.max_bytes = 22;
    CHECK(encode_execute(1, std::span(parameters).first(1), output, limits) == command_status::complete);
    CHECK(output.size() == 22);
    limits = {};
    limits.max_columns = 1;
    const std::array columns = {column(field_type::var_string), column(field_type::var_string)};
    auto row = old_row();
    CHECK(parse_text_row({}, columns, row, limits) == command_status::limit_exceeded);
    CHECK(parse_binary_row({}, columns, row, limits) == command_status::limit_exceeded);
    check_old_row(row);
    limits = {};
    limits.max_bytes = 3;
    CHECK(parse_text_row("long", columns, row, limits) == command_status::limit_exceeded);
    CHECK(parse_binary_row("long", columns, row, limits) == command_status::limit_exceeded);
    CHECK(parse_text_row({}, {}, row) == command_status::invalid);
    CHECK(parse_binary_row({}, {}, row) == command_status::invalid);
    check_old_row(row);
}

DOCTEST_TEST_CASE("MySQL large binary values cross all length encoding boundaries without changing data") {
    const std::array columns = {column(field_type::long_blob)};
    for (const auto size : {0, 250, 251, 255, 256, 65535, 65536, 1024 * 1024})
    {
        std::string value(size, '\0');
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            value[index] = static_cast<char>(index & 255);
        }
        const auto text = text_row({value});
        row_values row;
        REQUIRE(parse_text_row(text, columns, row) == command_status::complete);
        CHECK(std::get<std::string>(row[0]) == value);
        const auto binary = std::string("\0\0", 2) + text;
        REQUIRE(parse_binary_row(binary, columns, row) == command_status::complete);
        CHECK(std::get<std::string>(row[0]) == value);
        const std::array<field_value, 1> parameters = {value};
        std::string output;
        REQUIRE(encode_execute(1, parameters, output) == command_status::complete);
        CHECK(output.substr(14) == text);
        codec_limits limits;
        limits.max_bytes = binary.size() - 1;
        row = old_row();
        CHECK(parse_binary_row(binary, columns, row, limits) == command_status::limit_exceeded);
        check_old_row(row);
    }
}

DOCTEST_TEST_CASE("MySQL execute rejects invalid numeric and temporal parameters without changing output") {
    for (const field_value& value : {field_value(std::numeric_limits<double>::infinity()),
             field_value(std::numeric_limits<double>::quiet_NaN()), field_value(date_value{10000, 1, 1}),
             field_value(date_value{2026, 13, 1}), field_value(datetime_value{{2026, 1, 1}, 24}),
             field_value(time_value{false, 0, 0, 60}), field_value(time_value{false, 0, 0, 0, 0, 1000000})})
    {
        std::string output = "old";
        const std::array values = {value};
        CHECK(encode_execute(1, values, output) == command_status::invalid);
        CHECK(output == "old");
    }
    field_value value = std::int64_t(1);
    try
    { value.emplace<std::string>(std::string::npos, 'x'); }
    catch (const std::length_error&)
    {}
    if (value.valueless_by_exception())
    {
        const std::array values = {value};
        std::string output = "old";
        CHECK(encode_execute(1, values, output) == command_status::invalid);
        CHECK(output == "old");
    }
}

DOCTEST_TEST_CASE("MySQL prepared typed values roundtrip with independently shifted result NULL bitmap") {
    std::mt19937_64 random(0x436f6d6d616e64);
    for (unsigned iteration = 0; iteration < 400; ++iteration)
    {
        std::vector<field_value> expected;
        for (unsigned index = 0; index < 17; ++index)
        {
            switch (random() % 6)
            {
            case 0:
                expected.emplace_back(std::monostate{});
                break;
            case 1:
                expected.emplace_back(std::bit_cast<std::int64_t>(random()));
                break;
            case 2:
                expected.emplace_back(random());
                break;
            case 3:
                expected.emplace_back(static_cast<double>(std::bit_cast<std::int64_t>(random())) / 16);
                break;
            case 4:
                expected.emplace_back(std::string(random() % 256, static_cast<char>(random())));
                break;
            default:
                expected.emplace_back(date_value{static_cast<std::uint16_t>(random() % 10000),
                    static_cast<std::uint8_t>(random() % 13), static_cast<std::uint8_t>(random() % 32)});
                break;
            }
        }
        std::vector<column_definition> columns;
        const auto payload = binary_from_execute(expected, columns);
        row_values row;
        REQUIRE(parse_binary_row(payload, columns, row) == command_status::complete);
        CHECK(row == expected);
    }
}

DOCTEST_TEST_CASE("MySQL maximum prepared parameter count stays bounded and emits no NULL values") {
    const std::vector<field_value> values(65536);
    std::string payload;
    REQUIRE(encode_execute(1, std::span(values).first(65535), payload) == command_status::complete);
    CHECK(payload.size() == 10 + 8192 + 1 + 65535 * 2);
    CHECK(static_cast<unsigned char>(payload[10]) == 255);
    CHECK(static_cast<unsigned char>(payload[10 + 8191]) == 127);
    CHECK(payload[10 + 8192] == 1);
    payload = "old";
    codec_limits limits;
    limits.max_parameters = std::numeric_limits<std::size_t>::max();
    CHECK(encode_execute(1, values, payload, limits) == command_status::limit_exceeded);
    CHECK(payload == "old");
}

DOCTEST_TEST_CASE("MySQL hostile command result payloads never partially overwrite rows or column metadata") {
    std::mt19937 random(0x726f7773);
    const std::vector columns = {column(field_type::longlong), column(field_type::blob), column(field_type::datetime)};
    for (unsigned iteration = 0; iteration < 2000; ++iteration)
    {
        std::string payload(random() % 96, '\0');
        for (auto& byte : payload)
        {
            byte = static_cast<char>(random() & 255);
        }
        auto row = old_row();
        if (parse_text_row(payload, columns, row) != command_status::complete)
        {
            check_old_row(row);
        }
        row = old_row();
        if (parse_binary_row(payload, columns, row) != command_status::complete)
        {
            check_old_row(row);
        }
        column_definition definition;
        definition.name = "old";
        if (parse_column(payload, definition) != command_status::complete)
        {
            CHECK(definition.name == "old");
        }
    }
}
