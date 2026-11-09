// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace connection_load {

// Per-call socket read/write sizes for comparing IO cadence between diagnostic
// peers. Log2 byte buckets: 0, 1, 2-3, ..., 2048-4095 and 4096 or more.
// Updated only by the single IO thread; read after that thread has stopped.
struct io_histogram
{
    static constexpr std::size_t buckets = 14;

    static std::size_t bucket(std::size_t bytes) noexcept {
        return (std::min)(static_cast<std::size_t>(std::bit_width(bytes)), buckets - 1);
    }

    struct direction
    {
        std::array<std::uint64_t, buckets> requested{}, completed{};
        std::uint64_t calls{}, bytes{}, short_calls{}, would_block{};

        void record(std::size_t requested_bytes, std::size_t completed_bytes) noexcept {
            ++calls;
            bytes += completed_bytes;
            ++requested[bucket(requested_bytes)];
            ++completed[bucket(completed_bytes)];
            if (completed_bytes < requested_bytes)
            {
                ++short_calls;
            }
        }

        void write_json(std::ostream& out) const {
            out << "{\"calls\":" << calls << ",\"bytes\":" << bytes << ",\"short_calls\":" << short_calls
                << ",\"would_block\":" << would_block << ",\"requested\":";
            write_buckets(out, requested);
            out << ",\"completed\":";
            write_buckets(out, completed);
            out << '}';
        }
    };

    direction read, write;
    std::uint64_t waits{}, events{};

    void write_json(std::ostream& out) const {
        out << "{\"bucket_upper_bytes\":[0";
        for (std::size_t index = 1; index + 1 < buckets; ++index)
        {
            out << ',' << ((std::uint64_t{1} << index) - 1);
        }
        out << ",null],\"read\":";
        read.write_json(out);
        out << ",\"write\":";
        write.write_json(out);
        out << ",\"waits\":" << waits << ",\"events\":" << events << '}';
    }

private:
    static void write_buckets(std::ostream& out, const std::array<std::uint64_t, buckets>& values) {
        out << '[';
        for (std::size_t index = 0; index < buckets; ++index)
        {
            out << (index ? "," : "") << values[index];
        }
        out << ']';
    }
};

} // namespace connection_load
