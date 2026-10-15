// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace arknet::redis::detail {

enum class resp_type
{
    simple_string,
    error,
    integer,
    bulk_string,
    array,
    null,
    boolean,
    real,
    big_number,
    bulk_error,
    verbatim_string,
    map,
    attribute,
    set,
    push
};

struct resp_value
{
    resp_type type{resp_type::null};
    std::string text;
    std::int64_t integer{};
    double real{};
    bool boolean{};
    // Maps and attributes contain alternating keys and values, preserving all key types.
    std::vector<resp_value> elements;
    std::vector<resp_value> attributes;
};

struct resp_limits
{
    std::size_t max_buffer_bytes{64 * 1024 * 1024};
    std::size_t max_message_bytes{64 * 1024 * 1024};
    std::size_t max_bulk_bytes{64 * 1024 * 1024};
    std::size_t max_line_bytes{64 * 1024};
    std::size_t max_values{1024 * 1024};
    std::size_t max_depth{64};
};

enum class resp_status
{
    need_more,
    ready,
    malformed,
    limit_exceeded
};

// Incremental RESP2/RESP3 decoder. A ready result owns its data; subsequent input cannot invalidate it.
// This internal codec does not correlate replies with requests. Pushes are distinct values and
// attributes decorate the next value at their nesting level, without consuming a reply slot.
class resp_decoder
{
public:
    explicit resp_decoder(resp_limits limits = {})
        : limits_(limits) {
        if (!limits_.max_buffer_bytes || !limits_.max_message_bytes || !limits_.max_bulk_bytes ||
            !limits_.max_line_bytes || !limits_.max_values || !limits_.max_depth || limits_.max_depth > 128)
        {
            throw std::invalid_argument("RESP limits must be positive and max_depth must not exceed 128");
        }
    }

    bool append(std::string_view input) {
        if (failed())
        {
            return false;
        }
        if (input.size() > limits_.max_buffer_bytes - (buffer_.size() - position_))
        {
            fail(resp_status::limit_exceeded);
            return false;
        }
        compact(true);
        buffer_.append(input);
        return true;
    }

    resp_status next(resp_value& output) {
        if (failed())
        {
            return status_;
        }
        while (position_ < buffer_.size())
        {
            if (phase_ == phase::bulk)
            {
                const auto count = std::min(remaining_, buffer_.size() - position_);
                if (!consume(count))
                {
                    return status_;
                }
                current_.text.append(buffer_.data() + position_ - count, count);
                remaining_ -= count;
                if (remaining_ != 0)
                {
                    break;
                }
                phase_ = phase::bulk_cr;
                continue;
            }

            const char byte = buffer_[position_];
            if (!consume(1))
            {
                return status_;
            }
            if (phase_ == phase::prefix)
            {
                if (byte == '.')
                {
                    if (frames_.empty() || !frames_.back().streamed || !pending_attributes_.empty())
                    {
                        return fail(resp_status::malformed);
                    }
                }
                else if (std::string_view("+-:$*_#,(!=%|~>").find(byte) == std::string_view::npos)
                {
                    return fail(resp_status::malformed);
                }
                else if (++values_ > limits_.max_values || frames_.size() >= limits_.max_depth)
                {
                    return fail(resp_status::limit_exceeded);
                }
                prefix_ = byte;
                phase_ = phase::line;
            }
            else if (phase_ == phase::chunk_prefix)
            {
                if (byte != ';')
                {
                    return fail(resp_status::malformed);
                }
                prefix_ = byte;
                phase_ = phase::line;
            }
            else if (phase_ == phase::line)
            {
                if (byte == '\r')
                {
                    phase_ = phase::line_lf;
                }
                else if (byte == '\n')
                {
                    return fail(resp_status::malformed);
                }
                else if (line_.size() == limits_.max_line_bytes)
                {
                    return fail(resp_status::limit_exceeded);
                }
                else
                {
                    line_.push_back(byte);
                }
            }
            else if (phase_ == phase::line_lf)
            {
                if (byte != '\n')
                {
                    return fail(resp_status::malformed);
                }
                const auto result = finish_line(output);
                line_.clear();
                if (result != resp_status::need_more)
                {
                    compact();
                    return result;
                }
            }
            else if (phase_ == phase::bulk_cr)
            {
                if (byte != '\r')
                {
                    return fail(resp_status::malformed);
                }
                phase_ = phase::bulk_lf;
            }
            else if (phase_ == phase::bulk_lf)
            {
                if (byte != '\n')
                {
                    return fail(resp_status::malformed);
                }
                if (streamed_bulk_)
                {
                    phase_ = phase::chunk_prefix;
                    continue;
                }
                if (current_.type == resp_type::verbatim_string &&
                    (current_.text.size() < 4 || current_.text[3] != ':'))
                {
                    return fail(resp_status::malformed);
                }
                phase_ = phase::prefix;
                const auto result = complete(std::move(current_), output);
                if (result == resp_status::ready)
                {
                    compact();
                    return result;
                }
            }
        }
        compact();
        return resp_status::need_more;
    }

    void reset() {
        buffer_.clear();
        position_ = 0;
        phase_ = phase::prefix;
        status_ = resp_status::need_more;
        line_.clear();
        current_ = {};
        frames_.clear();
        pending_attributes_.clear();
        remaining_ = 0;
        wire_bytes_ = 0;
        values_ = 0;
        streamed_bulk_ = false;
    }

private:
    enum class phase
    {
        prefix,
        line,
        line_lf,
        bulk,
        bulk_cr,
        bulk_lf,
        chunk_prefix
    };

    struct frame
    {
        resp_value value;
        std::size_t count{};
        bool streamed{};
    };

    bool failed() const noexcept {
        return status_ == resp_status::malformed || status_ == resp_status::limit_exceeded;
    }

    resp_status fail(resp_status status) noexcept {
        status_ = status;
        return status;
    }

    bool consume(std::size_t count) noexcept {
        if (count > limits_.max_message_bytes - wire_bytes_)
        {
            fail(resp_status::limit_exceeded);
            return false;
        }
        wire_bytes_ += count;
        position_ += count;
        return true;
    }

    void compact(bool force = false) {
        if (position_ == buffer_.size())
        {
            buffer_.clear();
            position_ = 0;
        }
        else if (position_ != 0 && (force || (position_ >= 4096 && position_ >= buffer_.size() / 2)))
        {
            buffer_.erase(0, position_);
            position_ = 0;
        }
    }

    static bool digits(std::string_view value) noexcept {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    static bool signed_digits(std::string_view value) noexcept {
        if (!value.empty() && (value.front() == '+' || value.front() == '-'))
        {
            value.remove_prefix(1);
        }
        return digits(value);
    }

    bool size_value(std::size_t& result) {
        if (!digits(line_))
        {
            fail(resp_status::malformed);
            return false;
        }
        const auto parsed = std::from_chars(line_.data(), line_.data() + line_.size(), result);
        if (parsed.ec == std::errc::result_out_of_range)
        {
            fail(resp_status::limit_exceeded);
            return false;
        }
        return parsed.ec == std::errc{};
    }

    resp_value new_value(resp_type type) {
        resp_value value;
        value.type = type;
        value.attributes = std::move(pending_attributes_);
        pending_attributes_.clear();
        return value;
    }

    resp_status complete(resp_value value, resp_value& output) {
        for (;;)
        {
            if (value.type == resp_type::attribute)
            {
                pending_attributes_ = std::move(value.attributes);
                pending_attributes_.insert(pending_attributes_.end(), std::make_move_iterator(value.elements.begin()),
                    std::make_move_iterator(value.elements.end()));
                return resp_status::need_more;
            }
            if (frames_.empty())
            {
                output = std::move(value);
                wire_bytes_ = 0;
                values_ = 0;
                return resp_status::ready;
            }
            auto& parent = frames_.back();
            parent.value.elements.push_back(std::move(value));
            if (parent.streamed || parent.value.elements.size() != parent.count)
            {
                return resp_status::need_more;
            }
            value = std::move(parent.value);
            frames_.pop_back();
        }
    }

    resp_status finish_line(resp_value& output) {
        phase_ = phase::prefix;
        if (prefix_ == '.')
        {
            if (!line_.empty() ||
                (frames_.back().value.type == resp_type::map && frames_.back().value.elements.size() % 2 != 0))
            {
                return fail(resp_status::malformed);
            }
            auto value = std::move(frames_.back().value);
            frames_.pop_back();
            return complete(std::move(value), output);
        }
        if (prefix_ == ';')
        {
            if (!size_value(remaining_))
            {
                return status_;
            }
            if (remaining_ > limits_.max_bulk_bytes - current_.text.size())
            {
                return fail(resp_status::limit_exceeded);
            }
            if (remaining_ == 0)
            {
                streamed_bulk_ = false;
                return complete(std::move(current_), output);
            }
            phase_ = phase::bulk;
            return resp_status::need_more;
        }

        resp_type type;
        switch (prefix_)
        {
        case '+':
            type = resp_type::simple_string;
            break;
        case '-':
            type = resp_type::error;
            break;
        case ':':
            type = resp_type::integer;
            break;
        case '$':
            type = resp_type::bulk_string;
            break;
        case '*':
            type = resp_type::array;
            break;
        case '_':
            type = resp_type::null;
            break;
        case '#':
            type = resp_type::boolean;
            break;
        case ',':
            type = resp_type::real;
            break;
        case '(':
            type = resp_type::big_number;
            break;
        case '!':
            type = resp_type::bulk_error;
            break;
        case '=':
            type = resp_type::verbatim_string;
            break;
        case '%':
            type = resp_type::map;
            break;
        case '|':
            type = resp_type::attribute;
            break;
        case '~':
            type = resp_type::set;
            break;
        case '>':
            type = resp_type::push;
            break;
        default:
            return fail(resp_status::malformed);
        }

        auto value = new_value(type);
        if (type == resp_type::bulk_string || type == resp_type::bulk_error || type == resp_type::verbatim_string)
        {
            if (type == resp_type::bulk_string && line_ == "-1")
            {
                value.type = resp_type::null;
                return complete(std::move(value), output);
            }
            if (type == resp_type::bulk_string && line_ == "?")
            {
                current_ = std::move(value);
                streamed_bulk_ = true;
                phase_ = phase::chunk_prefix;
                return resp_status::need_more;
            }
            if (!size_value(remaining_))
            {
                return status_;
            }
            if (remaining_ > limits_.max_bulk_bytes || remaining_ > limits_.max_message_bytes - wire_bytes_)
            {
                return fail(resp_status::limit_exceeded);
            }
            current_ = std::move(value);
            phase_ = remaining_ ? phase::bulk : phase::bulk_cr;
            return resp_status::need_more;
        }
        if (type == resp_type::array || type == resp_type::map || type == resp_type::set || type == resp_type::push ||
            type == resp_type::attribute)
        {
            if (type == resp_type::array && line_ == "-1")
            {
                value.type = resp_type::null;
                return complete(std::move(value), output);
            }
            const bool streamed =
                line_ == "?" && (type == resp_type::array || type == resp_type::map || type == resp_type::set);
            std::size_t count = 0;
            if (!streamed)
            {
                if (!size_value(count))
                {
                    return status_;
                }
                const std::size_t factor = (type == resp_type::map || type == resp_type::attribute) ? 2 : 1;
                if (count > (limits_.max_values - values_) / factor)
                {
                    return fail(resp_status::limit_exceeded);
                }
                count *= factor;
                if (count == 0)
                {
                    return complete(std::move(value), output);
                }
            }
            frames_.push_back({std::move(value), count, streamed});
            return resp_status::need_more;
        }

        switch (type)
        {
        case resp_type::simple_string:
        case resp_type::error:
            value.text = line_;
            break;
        case resp_type::integer:
        {
            auto number = std::string_view(line_);
            if (!signed_digits(number))
            {
                return fail(resp_status::malformed);
            }
            if (number.front() == '+')
            {
                number.remove_prefix(1);
            }
            if (std::from_chars(number.data(), number.data() + number.size(), value.integer).ec != std::errc{})
            {
                return fail(resp_status::malformed);
            }
            break;
        }
        case resp_type::null:
            if (!line_.empty())
            {
                return fail(resp_status::malformed);
            }
            break;
        case resp_type::boolean:
            if (line_ != "t" && line_ != "f")
            {
                return fail(resp_status::malformed);
            }
            value.boolean = line_ == "t";
            break;
        case resp_type::big_number:
            if (!signed_digits(line_))
            {
                return fail(resp_status::malformed);
            }
            value.text = line_;
            break;
        case resp_type::real:
        {
            if (line_ == "inf" || line_ == "-inf" || line_ == "nan")
            {
                value.real = line_ == "nan" ? std::numeric_limits<double>::quiet_NaN()
                                            : (line_ == "inf" ? 1 : -1) * std::numeric_limits<double>::infinity();
                break;
            }
            auto number = std::string_view(line_);
            if (!number.empty() && number.front() == '+')
            {
                number.remove_prefix(1);
            }
            auto integral = number;
            if (!integral.empty() && integral.front() == '-')
            {
                integral.remove_prefix(1);
            }
            if (integral.empty() || integral.front() < '0' || integral.front() > '9')
            {
                return fail(resp_status::malformed);
            }
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value.real);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !std::isfinite(value.real))
            {
                return fail(resp_status::malformed);
            }
            break;
        }
        default:
            return fail(resp_status::malformed);
        }
        return complete(std::move(value), output);
    }

    resp_limits limits_;
    std::string buffer_;
    std::size_t position_{};
    phase phase_{phase::prefix};
    resp_status status_{resp_status::need_more};
    char prefix_{};
    std::string line_;
    resp_value current_;
    std::vector<frame> frames_;
    std::vector<resp_value> pending_attributes_;
    std::size_t remaining_{};
    std::size_t wire_bytes_{};
    std::size_t values_{};
    bool streamed_bulk_{};
};

inline std::string encode_command(
    std::span<const std::string_view> arguments, std::size_t max_bytes = 64 * 1024 * 1024) {
    if (arguments.empty())
    {
        throw std::invalid_argument("A Redis command requires at least one argument");
    }
    const auto count = std::to_string(arguments.size());
    std::size_t size = 1 + count.size() + 2;
    if (size > max_bytes)
    {
        throw std::length_error("Redis command exceeds the configured byte limit");
    }
    for (const auto argument : arguments)
    {
        const auto overhead = 1 + std::to_string(argument.size()).size() + 4;
        if (overhead > max_bytes - size || argument.size() > max_bytes - size - overhead)
        {
            throw std::length_error("Redis command exceeds the configured byte limit");
        }
        size += overhead + argument.size();
    }
    std::string encoded;
    encoded.reserve(size);
    encoded += '*' + count + "\r\n";
    for (const auto argument : arguments)
    {
        encoded += '$' + std::to_string(argument.size()) + "\r\n";
        encoded.append(argument);
        encoded += "\r\n";
    }
    return encoded;
}

} // namespace arknet::redis::detail
