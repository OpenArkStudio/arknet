// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arknet::mysql::detail {

// Classic protocol framing: https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html
inline constexpr std::size_t max_packet_payload = 0x00ffffff;
inline constexpr std::size_t default_message_limit = 64 * 1024 * 1024;

enum class read_status
{
    complete,
    incomplete,
    invalid
};

class wire_reader
{
public:
    explicit wire_reader(std::string_view input)
        : input_(input) {}

    std::size_t remaining() const noexcept {
        return input_.size() - position_;
    }

    std::size_t position() const noexcept {
        return position_;
    }

    read_status read_integer(std::size_t width, std::uint64_t& output) {
        if (width == 0 || width > sizeof(output))
        {
            return read_status::invalid;
        }
        if (remaining() < width)
        {
            return read_status::incomplete;
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i)
        {
            value |= std::uint64_t(static_cast<unsigned char>(input_[position_ + i])) << (i * 8);
        }
        position_ += width;
        output = value;
        return read_status::complete;
    }

    read_status read_bytes(std::size_t count, std::string_view& output) {
        if (count > remaining())
        {
            return read_status::incomplete;
        }
        output = input_.substr(position_, count);
        position_ += count;
        return read_status::complete;
    }

    read_status read_null_terminated(std::string_view& output) {
        const auto end = input_.find('\0', position_);
        if (end == std::string_view::npos)
        {
            return read_status::incomplete;
        }
        output = input_.substr(position_, end - position_);
        position_ = end + 1;
        return read_status::complete;
    }

    // The null marker is distinct from the integer zero and an empty string.
    read_status read_length(std::optional<std::uint64_t>& output) {
        if (remaining() == 0)
        {
            return read_status::incomplete;
        }
        const auto marker = static_cast<unsigned char>(input_[position_]);
        if (marker < 0xfb)
        {
            ++position_;
            output = marker;
            return read_status::complete;
        }
        if (marker == 0xfb)
        {
            ++position_;
            output.reset();
            return read_status::complete;
        }
        if (marker == 0xff)
        {
            return read_status::invalid;
        }
        const std::size_t width = marker == 0xfc ? 2 : marker == 0xfd ? 3 : 8;
        if (remaining() - 1 < width)
        {
            return read_status::incomplete;
        }
        ++position_;
        std::uint64_t value = 0;
        read_integer(width, value);
        output = value;
        return read_status::complete;
    }

    read_status read_string(std::optional<std::string_view>& output) {
        const auto start = position_;
        std::optional<std::uint64_t> length;
        const auto status = read_length(length);
        if (status != read_status::complete)
        {
            return status;
        }
        if (!length)
        {
            output.reset();
            return read_status::complete;
        }
        if (*length > remaining())
        {
            position_ = start;
            return read_status::incomplete;
        }
        output = input_.substr(position_, static_cast<std::size_t>(*length));
        position_ += static_cast<std::size_t>(*length);
        return read_status::complete;
    }

private:
    std::string_view input_;
    std::size_t position_ = 0;
};

inline void append_integer(std::string& output, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
    {
        output.push_back(static_cast<char>(value & 0xff));
        value >>= 8;
    }
}

inline void append_length(std::string& output, std::optional<std::uint64_t> value) {
    if (!value)
    {
        output.push_back(static_cast<char>(0xfb));
    }
    else if (*value < 0xfb)
    {
        output.push_back(static_cast<char>(*value));
    }
    else
    {
        const std::size_t width = *value <= 0xffff ? 2 : *value <= 0xffffff ? 3 : 8;
        output.push_back(static_cast<char>(width == 2 ? 0xfc : width == 3 ? 0xfd : 0xfe));
        append_integer(output, *value, width);
    }
}

inline void append_string(std::string& output, std::optional<std::string_view> value) {
    if (!value)
    {
        append_length(output, std::nullopt);
        return;
    }
    append_length(output, value->size());
    output.append(*value);
}

enum class packet_status
{
    complete,
    incomplete,
    sequence_mismatch,
    message_too_large
};

// Consumes at most one logical message per call. The caller retains unconsumed
// input and calls again for coalesced messages. Partial input never changes output.
class packet_decoder
{
public:
    explicit packet_decoder(std::size_t message_limit = default_message_limit, std::uint8_t sequence = 0)
        : message_limit_(message_limit)
        , expected_sequence_(sequence) {}

    packet_status consume(std::string_view& input, std::string& output) {
        if (failure_ != packet_status::incomplete)
        {
            return failure_;
        }
        for (;;)
        {
            if (header_size_ < header_.size())
            {
                const auto count = std::min(input.size(), header_.size() - header_size_);
                std::copy_n(input.begin(), count, header_.begin() + header_size_);
                header_size_ += count;
                input.remove_prefix(count);
                if (header_size_ < header_.size())
                {
                    return packet_status::incomplete;
                }
                if (static_cast<unsigned char>(header_[3]) != expected_sequence_)
                {
                    return fail(packet_status::sequence_mismatch);
                }
                packet_size_ = static_cast<unsigned char>(header_[0]) |
                               (std::size_t(static_cast<unsigned char>(header_[1])) << 8) |
                               (std::size_t(static_cast<unsigned char>(header_[2])) << 16);
                if (packet_size_ > message_limit_ - message_.size())
                {
                    return fail(packet_status::message_too_large);
                }
                remaining_ = packet_size_;
                ++expected_sequence_;
            }
            const auto count = std::min(input.size(), remaining_);
            message_.append(input.substr(0, count));
            input.remove_prefix(count);
            remaining_ -= count;
            if (remaining_ != 0)
            {
                return packet_status::incomplete;
            }
            header_size_ = 0;
            if (packet_size_ < max_packet_payload)
            {
                output = std::move(message_);
                message_.clear();
                return packet_status::complete;
            }
        }
    }

    std::uint8_t expected_sequence() const noexcept {
        return expected_sequence_;
    }

    std::size_t buffered_size() const noexcept {
        return message_.size() + header_size_;
    }

    void reset(std::uint8_t sequence = 0) {
        message_.clear();
        header_size_ = 0;
        remaining_ = 0;
        packet_size_ = 0;
        expected_sequence_ = sequence;
        failure_ = packet_status::incomplete;
    }

private:
    packet_status fail(packet_status status) {
        failure_ = status;
        message_.clear();
        return status;
    }

    std::size_t message_limit_;
    std::string message_;
    std::array<char, 4> header_{};
    std::size_t header_size_ = 0;
    std::size_t remaining_ = 0;
    std::size_t packet_size_ = 0;
    std::uint8_t expected_sequence_ = 0;
    packet_status failure_ = packet_status::incomplete;
};

// Sequence and output are committed only after the entire message is encoded.
inline bool encode_message(std::string_view message, std::uint8_t& sequence, std::string& output,
    std::size_t message_limit = default_message_limit) {
    if (message.size() > message_limit)
    {
        return false;
    }
    const auto packet_count = message.size() / max_packet_payload + 1;
    std::string encoded;
    if (message.size() > encoded.max_size() || packet_count > (encoded.max_size() - message.size()) / 4)
    {
        return false;
    }
    encoded.reserve(message.size() + packet_count * 4);
    auto next_sequence = sequence;
    for (;;)
    {
        const auto count = std::min(message.size(), max_packet_payload);
        append_integer(encoded, count, 3);
        encoded.push_back(static_cast<char>(next_sequence++));
        encoded.append(message.substr(0, count));
        message.remove_prefix(count);
        if (count < max_packet_payload)
        {
            break;
        }
    }
    output = std::move(encoded);
    sequence = next_sequence;
    return true;
}

} // namespace arknet::mysql::detail
