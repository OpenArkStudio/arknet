// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace connection_load {

constexpr std::size_t header_bytes = 64;
constexpr std::string_view magic = "ARKCON01";

enum class frame_kind : std::uint64_t
{
    hello = 1,
    hello_ack,
    start,
    start_ack,
    data,
    finish,
    finish_ack
};

struct identity
{
    std::uint64_t run{}, client{}, connection{};

    bool operator==(const identity&) const = default;
};

struct frame_header
{
    frame_kind kind{};
    identity owner{};
    std::uint64_t sequence{}, body_bytes{};
};

inline void write_integer(std::string& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
    {
        bytes[offset + index] = static_cast<char>(value >> (index * 8));
    }
}

inline std::uint64_t read_integer(std::string_view bytes, std::size_t offset) {
    std::uint64_t value{};
    for (std::size_t index = 0; index < 8; ++index)
    {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + index])) << (index * 8);
    }
    return value;
}

inline char body_byte(const identity& owner, std::uint64_t sequence, std::size_t index) {
    return static_cast<char>(owner.run + owner.client * 17 + owner.connection * 31 + sequence * 13 + index * 7);
}

inline std::string make_frame(frame_kind kind, identity owner, std::uint64_t sequence = 0, std::size_t body = 0) {
    if (body > (std::numeric_limits<std::size_t>::max)() - header_bytes)
    {
        throw std::length_error("frame size overflow");
    }
    std::string result(header_bytes + body, '\0');
    result.replace(0, magic.size(), magic);
    write_integer(result, 8, static_cast<std::uint64_t>(kind));
    write_integer(result, 16, owner.run);
    write_integer(result, 24, owner.client);
    write_integer(result, 32, owner.connection);
    write_integer(result, 40, sequence);
    write_integer(result, 48, body);
    for (std::size_t index = 0; index < body; ++index)
    {
        result[header_bytes + index] = body_byte(owner, sequence, index);
    }
    return result;
}

inline bool decode(std::string_view bytes, frame_header& header) {
    if (bytes.size() < header_bytes || bytes.substr(0, magic.size()) != magic || read_integer(bytes, 56) != 0)
    {
        return false;
    }
    const auto kind = read_integer(bytes, 8);
    if (kind < 1 || kind > 7 || read_integer(bytes, 48) != bytes.size() - header_bytes)
    {
        return false;
    }
    header = {static_cast<frame_kind>(kind),
        {read_integer(bytes, 16), read_integer(bytes, 24), read_integer(bytes, 32)}, read_integer(bytes, 40),
        read_integer(bytes, 48)};
    if (header.kind != frame_kind::data && header.body_bytes != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < header.body_bytes; ++index)
    {
        if (bytes[header_bytes + index] != body_byte(header.owner, header.sequence, index))
        {
            return false;
        }
    }
    return true;
}

// Eight subdivisions per power of two. Values are rounded up to the bucket's
// integer microsecond upper bound; counters can be added across processes.
class latency_distribution
{
public:
    static constexpr std::size_t bucket_count = 513;

    static std::size_t bucket(std::uint64_t microseconds) noexcept {
        if (!microseconds)
        {
            return 0;
        }
        const auto exponent = static_cast<std::size_t>(std::bit_width(microseconds) - 1);
        const auto base = std::uint64_t{1} << exponent;
        // Division before multiplication avoids overflowing at the top of uint64_t.
        const auto subdivision = exponent >= 3 ? (microseconds - base) / (base / 8) : (microseconds - base) * 8 / base;
        return 1 + exponent * 8 + static_cast<std::size_t>(subdivision);
    }

    static std::uint64_t upper_bound(std::size_t index) noexcept {
        if (!index)
        {
            return 0;
        }
        const auto exponent = (index - 1) / 8;
        const auto subdivision = (index - 1) % 8;
        const auto base = std::uint64_t{1} << exponent;
        if (exponent < 3)
        {
            return base + (base * (subdivision + 1) + 7) / 8 - 1;
        }
        const auto extension = base / 8 * (subdivision + 1);
        return base > (std::numeric_limits<std::uint64_t>::max)() - extension
                   ? (std::numeric_limits<std::uint64_t>::max)()
                   : base + extension - 1;
    }

    void observe(std::uint64_t microseconds) noexcept {
        ++counts_[bucket(microseconds)];
    }

    std::uint64_t count(std::size_t index) const noexcept {
        return counts_[index].load();
    }

    std::uint64_t total() const noexcept {
        std::uint64_t result{};
        for (const auto& value : counts_)
        {
            result += value.load();
        }
        return result;
    }

    std::uint64_t percentile(unsigned percent) const noexcept {
        const auto target = (total() / 100) * percent + ((total() % 100) * percent + 99) / 100;
        std::uint64_t cumulative{};
        for (std::size_t index = 0; index < bucket_count; ++index)
        {
            cumulative += count(index);
            if (cumulative >= target)
            {
                return upper_bound(index);
            }
        }
        return 0;
    }

private:
    std::array<std::atomic<std::uint64_t>, bucket_count> counts_{};
};

} // namespace connection_load
