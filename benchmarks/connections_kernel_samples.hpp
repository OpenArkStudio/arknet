// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>

#if defined(__APPLE__)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#endif

#include "connections_protocol.hpp"

namespace connection_load {

struct tcp_kernel_info
{
    std::uint32_t state{}, flags{}, rto_ms{}, send_buffer_bytes{}, send_window_bytes{}, receive_window_bytes{};
    std::uint32_t congestion_window_bytes{}, slow_start_threshold_bytes{};
    std::uint64_t tx_bytes{}, rx_bytes{}, retransmit_bytes{}, out_of_order_bytes{};
    enum class status
    {
        available,
        unsupported,
        closed,
        query_failed,
        short_response
    } result = status::unsupported;
    int error{};
};

inline tcp_kernel_info query_tcp_kernel_info(int descriptor) noexcept {
    tcp_kernel_info result;
#if defined(__APPLE__) && defined(TCP_CONNECTION_INFO)
    if (descriptor < 0)
    {
        result.result = tcp_kernel_info::status::closed;
        return result;
    }
    tcp_connection_info info{};
    socklen_t length = sizeof(info);
    if (getsockopt(descriptor, IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &length) != 0)
    {
        result.result = tcp_kernel_info::status::query_failed;
        result.error = errno;
        return result;
    }
    if (length < offsetof(tcp_connection_info, tcpi_rxoutoforderbytes) + sizeof(info.tcpi_rxoutoforderbytes))
    {
        result.result = tcp_kernel_info::status::short_response;
        return result;
    }
    result.result = tcp_kernel_info::status::available;
    result.state = info.tcpi_state;
    result.flags = info.tcpi_flags;
    result.rto_ms = info.tcpi_rto;
    result.send_buffer_bytes = info.tcpi_snd_sbbytes;
    result.send_window_bytes = info.tcpi_snd_wnd;
    result.receive_window_bytes = info.tcpi_rcv_wnd;
    result.congestion_window_bytes = info.tcpi_snd_cwnd;
    result.slow_start_threshold_bytes = info.tcpi_snd_ssthresh;
    result.tx_bytes = info.tcpi_txbytes;
    result.rx_bytes = info.tcpi_rxbytes;
    result.retransmit_bytes = info.tcpi_txretransmitbytes;
    result.out_of_order_bytes = info.tcpi_rxoutoforderbytes;
#else
    (void)descriptor;
#endif
    return result;
}

// Single IO-worker ownership. No per-session allocation or work in disabled mode.
class tcp_kernel_samples
{
public:
    static constexpr std::size_t session_capacity = 16, sample_capacity = 8, memory_limit = 32 * 1024;
    static constexpr std::int64_t inactivity_us = 1000000;

    explicit tcp_kernel_samples(bool enabled)
        : data_(enabled ? std::make_unique<storage>() : nullptr) {}

    static bool stalled(bool active, bool finished, bool closed, bool received_data, std::int64_t idle_us) noexcept {
        return active && !finished && !closed && received_data && idle_us >= inactivity_us;
    }

    void release(identity owner) noexcept {
        if (!data_ || frozen_)
        {
            return;
        }
        if (auto* value = find(owner))
        {
            *value = {};
            value->ever_used = true;
            ++released_;
        }
    }

    void prune(identity owner, bool eligible, std::uint64_t completions) noexcept {
        if (data_ && !frozen_)
        {
            if (auto* value = find(owner); value && (!eligible || value->completions != completions))
            {
                release(owner);
            }
        }
    }

    template<class Query>
    void sample(identity owner, std::int64_t time_us, std::int64_t idle_us, std::uint64_t completions,
        std::uint64_t next_receive, std::uint64_t next_send, Query query) {
        if (!data_ || frozen_ || idle_us < inactivity_us)
        {
            return;
        }
        auto* value = find(owner);
        if (!value)
        {
            for (auto& candidate : data_->slots)
            {
                if (!candidate.used)
                {
                    value = &candidate;
                    reused_ += value->ever_used;
                    value->used = value->ever_used = true;
                    value->owner = owner;
                    break;
                }
            }
        }
        if (!value)
        {
            ++omitted_;
            return; // Persistent stalls keep their slots; never query omitted identities.
        }
        value->completions = completions;
        auto& event = value->events[value->total % sample_capacity];
        event = {time_us, idle_us, next_receive, next_send, query()};
        overwritten_ += value->total >= sample_capacity;
        ++value->total;
        ++queries_;
    }

    void freeze() noexcept {
        frozen_ = true;
    }

    std::size_t storage_bytes() const noexcept {
        return data_ ? sizeof(storage) : 0;
    }

    bool contains(identity owner) const noexcept {
        return data_ && find(owner);
    }

    void write_history(std::ostream& out, identity owner) const {
        const auto* value = data_ ? find(owner) : nullptr;
        out << "\"sample_phase\":\"periodic_io_worker\",\"time_unit\":\"microseconds_since_process_start\",\"samples\":"
               "[";
        if (value)
        {
            const auto size = (std::min)(value->total, std::uint64_t{sample_capacity});
            for (std::uint64_t index = 0; index < size; ++index)
            {
                if (index)
                {
                    out << ',';
                }
                const auto& event = value->events[(value->total - size + index) % sample_capacity];
                const auto& info = event.info;
                out << "{\"sample_time_us\":" << event.time_us << ",\"no_completion_age_us\":" << event.idle_us
                    << ",\"next_receive\":" << event.next_receive << ",\"next_send\":" << event.next_send
                    << ",\"tcp_info\":";
                if (info.result == tcp_kernel_info::status::available)
                {
                    out << "{\"state\":" << info.state << ",\"flags\":" << info.flags << ",\"rto_ms\":" << info.rto_ms
                        << ",\"send_buffer_bytes\":" << info.send_buffer_bytes
                        << ",\"send_window_bytes\":" << info.send_window_bytes
                        << ",\"receive_window_bytes\":" << info.receive_window_bytes
                        << ",\"congestion_window_bytes\":" << info.congestion_window_bytes
                        << ",\"slow_start_threshold_bytes\":" << info.slow_start_threshold_bytes
                        << ",\"tx_bytes\":" << info.tx_bytes << ",\"rx_bytes\":" << info.rx_bytes
                        << ",\"retransmit_bytes\":" << info.retransmit_bytes
                        << ",\"out_of_order_bytes\":" << info.out_of_order_bytes << '}';
                }
                else
                {
                    const char* reason = info.result == tcp_kernel_info::status::unsupported ? "unsupported_platform"
                                         : info.result == tcp_kernel_info::status::closed    ? "socket_closed"
                                         : info.result == tcp_kernel_info::status::short_response
                                             ? "short_tcp_connection_info"
                                             : "getsockopt_failed";
                    out << "null,\"unavailable_reason\":\"" << reason << '\"';
                    if (info.result == tcp_kernel_info::status::query_failed)
                    {
                        out << ",\"error\":" << info.error;
                    }
                }
                out << '}';
            }
            out << "],\"overwritten_samples\":" << value->total - size;
        }
        else
        {
            out << "],\"overwritten_samples\":0";
        }
    }

    void write_summary(std::ostream& out) const {
        std::size_t retained{};
        if (data_)
        {
            for (const auto& value : data_->slots)
            {
                retained += value.used;
            }
        }
        out << "{\"storage_bytes\":" << storage_bytes() << ",\"memory_limit_bytes\":" << memory_limit
            << ",\"session_capacity\":" << session_capacity << ",\"sample_capacity\":" << sample_capacity
            << ",\"retained_identities\":" << retained << ",\"released_histories\":" << released_
            << ",\"slot_reuses\":" << reused_ << ",\"omitted_eligible_observations\":" << omitted_
            << ",\"overwritten_samples\":" << overwritten_ << ",\"sampler_calls\":" << queries_
            << ",\"frozen\":" << (frozen_ ? "true" : "false") << '}';
    }

private:
    struct event
    {
        std::int64_t time_us{}, idle_us{};
        std::uint64_t next_receive{}, next_send{};
        tcp_kernel_info info;
    };

    struct slot
    {
        identity owner{};
        std::array<event, sample_capacity> events{};
        std::uint64_t total{}, completions{};
        bool used{}, ever_used{};
    };

    struct storage
    {
        std::array<slot, session_capacity> slots{};
    };

    static_assert(sizeof(storage) <= memory_limit);

    slot* find(identity owner) const noexcept {
        for (auto& value : data_->slots)
        {
            if (value.used && value.owner.run == owner.run && value.owner.client == owner.client &&
                value.owner.connection == owner.connection)
            {
                return &value;
            }
        }
        return nullptr;
    }

    std::unique_ptr<storage> data_;
    std::uint64_t released_{}, reused_{}, omitted_{}, overwritten_{}, queries_{};
    bool frozen_{};
};

} // namespace connection_load
