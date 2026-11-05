// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>

#if !defined(_WIN32)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

#include "connections_kernel_samples.hpp"

namespace connection_load {

struct socket_observation
{
    int poll_result{-1}, poll_error{}, readable_bytes{-1}, readable_error{}, peek_bytes{-1}, peek_error{};
    short events{};
    std::array<unsigned char, 67> peek{};
    tcp_kernel_info tcp;
    std::uint32_t tcp_requested{}, tcp_returned{};
    std::array<unsigned char, 256> tcp_raw{};
};

// No consuming read, registration with Asio, or change to file status flags.
inline socket_observation observe_socket(int descriptor) noexcept {
    socket_observation result;
#if !defined(_WIN32)
    if (descriptor < 0)
    {
        result.poll_error = result.readable_error = result.peek_error = EBADF;
        result.tcp = query_tcp_kernel_info(-1);
        return result;
    }
    pollfd item{descriptor, POLLIN, 0};
    result.poll_result = ::poll(&item, 1, 0);
    result.poll_error = result.poll_result < 0 ? errno : 0;
    result.events = item.revents;
    int available{};
    if (::ioctl(descriptor, FIONREAD, &available) == 0)
    {
        result.readable_bytes = available;
    }
    else
    {
        result.readable_error = errno;
    }
    const auto bytes = ::recv(descriptor, result.peek.data(), result.peek.size(), MSG_PEEK | MSG_DONTWAIT);
    result.peek_bytes = static_cast<int>(bytes);
    result.peek_error = bytes < 0 ? errno : 0;
#if defined(__APPLE__) && defined(TCP_CONNECTION_INFO)
    tcp_connection_info raw{};
    static_assert(sizeof(raw) <= result.tcp_raw.size());
    socklen_t length = sizeof(raw);
    result.tcp_requested = sizeof(raw);
    if (::getsockopt(descriptor, IPPROTO_TCP, TCP_CONNECTION_INFO, &raw, &length) == 0)
    {
        result.tcp_returned = length;
        result.tcp = decode_tcp_kernel_info(raw, length);
        const auto* begin = reinterpret_cast<const unsigned char*>(&raw);
        std::copy_n(begin, (std::min)(std::size_t(length), sizeof(raw)), result.tcp_raw.begin());
    }
    else
    {
        result.tcp.result = tcp_kernel_info::status::query_failed;
        result.tcp.error = errno;
    }
#endif
#else
    (void)descriptor;
#endif
    return result;
}

struct observation_clock_anchor
{
    std::int64_t steady_before_ns{}, unix_ns{}, steady_after_ns{};
};

inline std::int64_t observation_steady_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline observation_clock_anchor observation_anchor() noexcept {
    observation_clock_anchor value;
    value.steady_before_ns = observation_steady_ns();
    value.unix_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    value.steady_after_ns = observation_steady_ns();
    return value;
}

// Single IO worker publishes snapshots; the existing controller observes pinned
// descriptors. The mutex also serializes dup retirement before socket.close().
class socket_observer
{
public:
    static constexpr std::size_t session_capacity = 16, sample_capacity = 8;
    static constexpr std::int64_t interval_us = 100000;

    struct snapshot
    {
        identity owner{};
        std::int64_t time_us{}, idle_us{};
        std::uint64_t completions{}, next_receive{}, next_send{}, input_bytes{};
        bool read_pending{}, frame_ready{};
        std::string local, remote;
    };

    struct token
    {
        identity owner{};
        std::uint64_t generation{};
    };

    explicit socket_observer(bool enabled)
        : data_(enabled ? std::make_unique<storage>() : nullptr) {}

    ~socket_observer() {
        if (data_)
        {
            for (auto& value : data_->slots)
            {
                retire(value);
            }
        }
    }

    token publish(int descriptor, snapshot state) {
        if (!data_)
        {
            return {};
        }
        std::lock_guard lock(mutex_);
        auto* value = find(state.owner);
        const bool created = !value;
        if (!value)
        {
            for (auto& candidate : data_->slots)
            {
                if (!candidate.generation)
                {
                    value = &candidate;
                    break;
                }
            }
            if (!value)
            {
                ++omitted_;
                return {};
            }
#if !defined(_WIN32)
            value->descriptor = ::dup(descriptor);
            if (value->descriptor < 0)
            {
                ++pin_failures_;
                return {};
            }
#else
            return {};
#endif
            value->generation = ++generation_;
            value->probe_submitted = state.time_us;
        }
        value->state = std::move(state);
        return created ? token{value->state.owner, value->generation} : token{};
    }

    // Recovered sessions free capacity; histories for still-stalled/closed peers
    // survive final cleanup. Never confuse a recycled fd/slot with its predecessor.
    void prune(identity owner, bool eligible, std::uint64_t completions) {
        if (!data_)
        {
            return;
        }
        std::lock_guard lock(mutex_);
        if (auto* value = find(owner);
            value && value->descriptor >= 0 && (!eligible || value->state.completions != completions))
        {
            retire(*value);
            *value = {};
            ++released_;
        }
    }

    void close(identity owner) {
        if (data_)
        {
            std::lock_guard lock(mutex_);
            if (auto* value = find(owner); value && value->descriptor >= 0)
            {
                // This runs before the caller cancels/closes the original socket.
                value->before_cancel = observation_anchor();
                retire(*value);
            }
        }
    }

    void complete(token expected, std::int64_t time_us) {
        if (data_)
        {
            std::lock_guard lock(mutex_);
            if (auto* value = find(expected.owner); value && value->generation == expected.generation)
            {
                value->probe_completed = time_us;
            }
        }
    }

    void sample(std::int64_t time_us, std::int64_t marker_completed) {
        if (!data_)
        {
            return;
        }
        std::lock_guard lock(mutex_);
        for (auto& value : data_->slots)
        {
            if (value.descriptor >= 0 && value.count < sample_capacity &&
                (!value.count || time_us - value.samples[value.count - 1].time_us >= interval_us))
            {
                auto& entry = value.samples[value.count++];
                entry.time_us = time_us;
                entry.marker_completed = marker_completed;
                entry.probe_completed = value.probe_completed;
                entry.state = value.state;
                entry.anchor = observation_anchor();
                entry.query = observe_socket(value.descriptor);
                entry.query_completed_steady_ns = observation_steady_ns();
            }
        }
    }

    void write_json(std::ostream& out, const std::string& role, std::int64_t clock_origin_us = 0) const {
        if (!data_)
        {
            return;
        }
        for (const auto& value : data_->slots)
        {
            for (std::size_t index = 0; index < value.count; ++index)
            {
                const auto& entry = value.samples[index];
                const auto& q = entry.query;
                out << "{\"event\":\"socket_observation\",\"schema\":1,\"role\":" << std::quoted(role)
                    << ",\"clock_origin_us\":" << clock_origin_us << ",\"run_id\":" << entry.state.owner.run
                    << ",\"client_id\":" << entry.state.owner.client
                    << ",\"connection_id\":" << entry.state.owner.connection << ",\"generation\":" << value.generation
                    << ",\"sample\":" << index << ",\"time_us\":" << entry.time_us
                    << ",\"snapshot_time_us\":" << entry.state.time_us << ",\"idle_us\":" << entry.state.idle_us
                    << ",\"local_endpoint\":" << std::quoted(entry.state.local)
                    << ",\"remote_endpoint\":" << std::quoted(entry.state.remote)
                    << ",\"next_receive\":" << entry.state.next_receive << ",\"next_send\":" << entry.state.next_send
                    << ",\"read_pending\":" << (entry.state.read_pending ? "true" : "false")
                    << ",\"input_bytes\":" << entry.state.input_bytes
                    << ",\"input_frame_ready\":" << (entry.state.frame_ready ? "true" : "false")
                    << ",\"global_marker_completed_us\":" << entry.marker_completed
                    << ",\"strand_probe_submitted_us\":" << value.probe_submitted
                    << ",\"strand_probe_completed_us\":" << entry.probe_completed << ",\"clock_anchor\":";
                write_anchor(out, entry.anchor);
                out << ",\"query_completed_steady_ns\":" << std::quoted(std::to_string(entry.query_completed_steady_ns))
                    << ",\"poll_result\":" << q.poll_result << ",\"poll_error\":" << q.poll_error
                    << ",\"poll_events\":" << q.events << ",\"readable_bytes\":" << q.readable_bytes
                    << ",\"readable_error\":" << q.readable_error << ",\"peek_bytes\":" << q.peek_bytes
                    << ",\"peek_error\":" << q.peek_error << ",\"peek_hex\":\"";
                hex(out, q.peek.data(), q.peek_bytes > 0 ? std::size_t(q.peek_bytes) : 0);
                out << "\",\"tcp_status\":" << static_cast<unsigned>(q.tcp.result) << ",\"tcp_error\":" << q.tcp.error
                    << ",\"tcp_requested_length\":" << q.tcp_requested << ",\"tcp_returned_length\":" << q.tcp_returned
                    << ",\"tcp_raw_hex\":\"";
                hex(out, q.tcp_raw.data(), (std::min)(std::size_t(q.tcp_returned), q.tcp_raw.size()));
                out << "\",\"tcp_info\":";
                if (q.tcp.result == tcp_kernel_info::status::available)
                {
                    out << "{\"state\":" << q.tcp.state << ",\"flags\":" << q.tcp.flags
                        << ",\"rto_ms\":" << q.tcp.rto_ms
                        << ",\"congestion_window_bytes\":" << q.tcp.congestion_window_bytes
                        << ",\"send_buffer_bytes\":" << q.tcp.send_buffer_bytes
                        << ",\"send_window_bytes\":" << q.tcp.send_window_bytes
                        << ",\"receive_window_bytes\":" << q.tcp.receive_window_bytes
                        << ",\"tx_bytes\":" << q.tcp.tx_bytes << ",\"rx_bytes\":" << q.tcp.rx_bytes
                        << ",\"retransmit_bytes\":" << q.tcp.retransmit_bytes << '}';
                }
                else
                {
                    out << "null";
                }
                out << "}\n";
            }
            if (value.before_cancel.unix_ns)
            {
                out << "{\"event\":\"socket_observer_before_cancel\",\"schema\":1,\"role\":" << std::quoted(role)
                    << ",\"run_id\":" << value.state.owner.run << ",\"client_id\":" << value.state.owner.client
                    << ",\"connection_id\":" << value.state.owner.connection << ",\"generation\":" << value.generation
                    << ",\"clock_anchor\":";
                write_anchor(out, value.before_cancel);
                out << "}\n";
            }
        }
        out << "{\"event\":\"socket_observer_summary\",\"role\":" << std::quoted(role)
            << ",\"clock_domain\":\"local_steady_clock\",\"clock_origin_us\":" << clock_origin_us
            << ",\"capacity\":16,\"samples_per_session\":8,\"interval_us\":100000,\"released\":" << released_
            << ",\"omitted\":" << omitted_ << ",\"pin_failures\":" << pin_failures_
            << ",\"storage_bytes\":" << sizeof(storage);
#if defined(__APPLE__) && defined(TCP_CONNECTION_INFO)
        out << ",\"tcp_struct_size\":" << sizeof(tcp_connection_info)
            << ",\"tcp_rto_offset\":" << offsetof(tcp_connection_info, tcpi_rto)
            << ",\"tcp_cwnd_offset\":" << offsetof(tcp_connection_info, tcpi_snd_cwnd)
            << ",\"tcp_last_required_end\":" << tcp_kernel_info_required_length;
#endif
        out << "}\n";
    }

private:
    struct sample_entry
    {
        snapshot state;
        socket_observation query;
        std::int64_t time_us{}, marker_completed{}, probe_completed{}, query_completed_steady_ns{};
        observation_clock_anchor anchor;
    };

    struct slot
    {
        int descriptor{-1};
        std::uint64_t generation{};
        snapshot state;
        std::int64_t probe_submitted{}, probe_completed{};
        observation_clock_anchor before_cancel;
        std::size_t count{};
        std::array<sample_entry, sample_capacity> samples{};
    };

    struct storage
    {
        std::array<slot, session_capacity> slots{};
    };

    static_assert(sizeof(storage) < 128 * 1024);

    slot* find(identity owner) {
        for (auto& value : data_->slots)
        {
            if (value.generation && value.state.owner == owner)
            {
                return &value;
            }
        }
        return nullptr;
    }

    static void retire(slot& value) {
#if !defined(_WIN32)
        if (value.descriptor >= 0)
        {
            ::close(value.descriptor);
            value.descriptor = -1;
        }
#endif
    }

    static void write_anchor(std::ostream& out, observation_clock_anchor value) {
        // Strings preserve Unix nanoseconds in JSON readers with binary64 numbers.
        out << "{\"steady_before_ns\":" << std::quoted(std::to_string(value.steady_before_ns))
            << ",\"unix_ns\":" << std::quoted(std::to_string(value.unix_ns))
            << ",\"steady_after_ns\":" << std::quoted(std::to_string(value.steady_after_ns)) << '}';
    }

    static void hex(std::ostream& out, const unsigned char* bytes, std::size_t count) {
        constexpr char digits[] = "0123456789abcdef";
        for (std::size_t index = 0; index < count; ++index)
        {
            out << digits[bytes[index] >> 4] << digits[bytes[index] & 15];
        }
    }

    std::unique_ptr<storage> data_;
    std::mutex mutex_;
    std::uint64_t generation_{}, omitted_{}, released_{}, pin_failures_{};
};
} // namespace connection_load
