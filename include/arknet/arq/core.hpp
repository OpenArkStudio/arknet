// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arknet::arq
{
struct options
{
    std::size_t mtu = 1200;
    std::uint16_t send_window = 128;
    std::uint16_t receive_window = 512;
    std::uint32_t interval = 10;
    bool nodelay = true;
    bool congestion_control = true;
    std::uint32_t fast_resend = 2;
    std::uint32_t max_retransmits = 20;
    // Maximum queued message wire bytes, independently for each direction.
    std::size_t max_pending_bytes = 4 * 1024 * 1024;
};

// KCP-compatible message ARQ. Serialize calls; output must not reenter and its views last only for the callback
// duration.
class control_block
{
public:
    explicit control_block(std::uint32_t conversation, const options& settings = {})
        : conversation_(conversation), options_(settings)
    {
        if (settings.mtu <= header_size || settings.mtu > 65507 || settings.send_window == 0 ||
            settings.receive_window == 0 || settings.interval == 0 || settings.interval > 5000 ||
            settings.max_retransmits == 0 || settings.max_pending_bytes < header_size)
            throw std::invalid_argument("invalid ARQ options");
        congestion_bytes_ = payload_size();
    }

    void set_output(std::function<void(std::string_view)> output) { output_ = std::move(output); }

    int send(std::string_view message)
    {
        if (failed_)
            return -1;
        const auto count = message.empty() ? 1 : 1 + (message.size() - 1) / payload_size();
        if (count > 256 || count > options_.receive_window)
            return -1;
        // Charge headers as well, so empty messages cannot bypass the queue bound.
        const auto occupied = pending_bytes_ + pending_segments() * header_size;
        if (occupied > options_.max_pending_bytes || count * header_size > options_.max_pending_bytes - occupied ||
            message.size() > options_.max_pending_bytes - occupied - count * header_size)
            return -2;
        std::list<segment> fragments;
        for (std::size_t index = 0; index < count; ++index)
        {
            segment fragment;
            fragment.fragment = static_cast<std::uint8_t>(count - index - 1);
            const auto length = std::min(payload_size(), message.size());
            fragment.data.assign(message.substr(0, length));
            message.remove_prefix(length);
            fragments.push_back(std::move(fragment));
        }
        for (const auto& fragment : fragments)
            pending_bytes_ += fragment.data.size();
        send_queue_.splice(send_queue_.end(), fragments);
        return 0;
    }

    // -5: receive wire-byte budget exhausted; the rejected PUSH is not acknowledged.
    int input(std::string_view datagram)
    {
        if (failed_ || datagram.size() < header_size || datagram.size() > 65507)
            return -1;
        // Validate every frame before acknowledging or accepting any part of a datagram.
        std::vector<frame> frames;
        while (!datagram.empty())
        {
            if (datagram.size() < header_size || read32(datagram, 0) != conversation_)
                return -1;
            frame value;
            value.command = byte(datagram, 4);
            value.fragment = byte(datagram, 5);
            value.window = read16(datagram, 6);
            value.timestamp = read32(datagram, 8);
            value.sequence = read32(datagram, 12);
            value.unacknowledged = read32(datagram, 16);
            const auto length = read32(datagram, 20);
            datagram.remove_prefix(header_size);
            if (length > datagram.size())
                return -2;
            if (value.command < push_command || value.command > window_command)
                return -3;
            if ((value.command != push_command && (length != 0 || value.fragment != 0)) ||
                (value.command == push_command && value.fragment >= options_.receive_window) ||
                difference(value.unacknowledged, next_send_) > 0)
                return -4;
            value.data = datagram.substr(0, length);
            frames.push_back(value);
            datagram.remove_prefix(length);
        }

        const auto previous_oldest = oldest_send();
        bool saw_ack = false;
        std::uint32_t highest_ack = 0;
        for (const auto& value : frames)
        {
            remote_window_ = value.window;
            acknowledge_before(value.unacknowledged);
            switch (value.command)
            {
            case ack_command:
                if (difference(value.sequence, previous_oldest) >= 0 && difference(value.sequence, next_send_) < 0)
                {
                    if (difference(current_, value.timestamp) >= 0)
                        update_timeout(static_cast<std::uint32_t>(difference(current_, value.timestamp)));
                    acknowledge(value.sequence);
                    if (!saw_ack || difference(value.sequence, highest_ack) > 0)
                        highest_ack = value.sequence;
                    saw_ack = true;
                }
                break;
            case push_command:
                if (!accept(value))
                    return -5;
                break;
            case probe_command:
                announce_window_ = true;
                break;
            default:
                break;
            }
        }
        if (saw_ack)
        {
            for (auto& fragment : send_buffer_)
                if (difference(highest_ack, fragment.sequence) > 0 && fragment.fast_acks < options_.fast_resend)
                    ++fragment.fast_acks;
        }
        if (difference(oldest_send(), previous_oldest) > 0)
            grow_window();
        return 0;
    }

    void update(std::uint32_t milliseconds)
    {
        current_ = milliseconds;
        if (!updated_)
        {
            updated_ = true;
            next_flush_ = current_;
        }
        const auto elapsed = difference(current_, next_flush_);
        if (elapsed > 10000 || elapsed < -10000)
            next_flush_ = current_;
        if (difference(current_, next_flush_) >= 0)
        {
            next_flush_ += options_.interval;
            if (difference(current_, next_flush_) >= 0)
                next_flush_ = current_ + options_.interval;
            flush();
        }
    }

    void flush()
    {
        if (!updated_ || !output_ || failed_)
            return;
        std::string datagram;
        datagram.reserve(options_.mtu);
        const auto append = [&](std::uint8_t command, std::uint8_t fragment, std::uint32_t timestamp,
                                std::uint32_t sequence, std::string_view data)
        {
            if (datagram.size() + header_size + data.size() > options_.mtu)
            {
                output_(datagram);
                datagram.clear();
            }
            write32(datagram, conversation_);
            datagram.push_back(static_cast<char>(command));
            datagram.push_back(static_cast<char>(fragment));
            write16(datagram, available_window());
            write32(datagram, timestamp);
            write32(datagram, sequence);
            write32(datagram, next_receive_);
            write32(datagram, static_cast<std::uint32_t>(data.size()));
            datagram.append(data);
        };
        for (const auto& [sequence, timestamp] : acknowledgements_)
            append(ack_command, 0, timestamp, sequence, {});
        acknowledgements_.clear();

        if (remote_window_ == 0)
        {
            if (probe_delay_ == 0)
            {
                probe_delay_ = 7000;
                next_probe_ = current_ + probe_delay_;
            }
            else if (difference(current_, next_probe_) >= 0)
            {
                append(probe_command, 0, 0, 0, {});
                probe_delay_ = std::min<std::uint32_t>(120000, probe_delay_ + probe_delay_ / 2);
                next_probe_ = current_ + probe_delay_;
            }
        }
        else
            probe_delay_ = 0;
        if (announce_window_)
        {
            append(window_command, 0, 0, 0, {});
            announce_window_ = false;
        }

        auto window = std::min<std::uint32_t>(options_.send_window, remote_window_);
        if (options_.congestion_control)
            window = std::min(window, congestion_window_);
        while (!send_queue_.empty() && next_send_ - oldest_send() < window)
        {
            auto& fragment = send_queue_.front();
            fragment.sequence = next_send_++;
            send_buffer_.splice(send_buffer_.end(), send_queue_, send_queue_.begin());
        }
        bool timeout_loss = false;
        bool fast_loss = false;
        for (auto& fragment : send_buffer_)
        {
            const bool initial = fragment.transmissions == 0;
            const bool timeout = !initial && difference(current_, fragment.deadline) >= 0;
            // Bound ACK-driven retries as KCP does; delayed recovery must still have time to arrive.
            const bool fast = !initial && fragment.transmissions <= 5 && options_.fast_resend != 0 &&
                              fragment.fast_acks >= options_.fast_resend;
            if (!initial && !timeout && !fast)
                continue;
            if (fragment.transmissions > options_.max_retransmits)
            {
                failed_ = true;
                break;
            }
            if (initial)
                fragment.timeout = retransmit_timeout_;
            else if (timeout)
            {
                fragment.timeout = std::min<std::uint32_t>(
                    60000, fragment.timeout + (options_.nodelay ? retransmit_timeout_ / 2 : retransmit_timeout_));
                timeout_loss = true;
            }
            else
                fast_loss = true;
            ++fragment.transmissions;
            ++transmissions_;
            if (!initial)
                ++retransmissions_;
            fragment.fast_acks = 0;
            fragment.deadline = current_ + fragment.timeout + (initial && !options_.nodelay ? fragment.timeout / 8 : 0);
            append(push_command, fragment.fragment, current_, fragment.sequence, fragment.data);
        }
        if (!datagram.empty())
            output_(datagram);
        if (options_.congestion_control)
        {
            if (fast_loss)
            {
                slow_start_threshold_ = std::max<std::uint32_t>(2, (next_send_ - oldest_send()) / 2);
                congestion_window_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    remote_window_, static_cast<std::uint64_t>(slow_start_threshold_) + options_.fast_resend));
                congestion_bytes_ = static_cast<std::uint64_t>(congestion_window_) * payload_size();
            }
            if (timeout_loss)
            {
                slow_start_threshold_ = std::max<std::uint32_t>(2, window / 2);
                congestion_window_ = 1;
                congestion_bytes_ = payload_size();
            }
        }
    }

    bool receive(std::string& message)
    {
        if (receive_queue_.empty() || failed_)
            return false;
        const auto count = static_cast<std::size_t>(receive_queue_.front().fragment) + 1;
        std::size_t length = 0;
        auto position = receive_queue_.begin();
        for (std::size_t index = 0; index < std::min(count, receive_queue_.size()); ++index, ++position)
        {
            if (position->fragment != count - index - 1)
            {
                failed_ = true;
                return false;
            }
            length += position->data.size();
        }
        if (receive_queue_.size() < count)
            return false;
        std::string assembled;
        assembled.reserve(length);
        position = receive_queue_.begin();
        for (std::size_t index = 0; index < count; ++index, ++position)
            assembled.append(position->data);
        const bool full = available_window() == 0;
        receive_bytes_ -= length + count * header_size;
        receive_queue_.erase(receive_queue_.begin(), position);
        promote_received();
        if (full && available_window() != 0)
            announce_window_ = true;
        message.swap(assembled);
        return true;
    }

    std::size_t pending_segments() const { return send_queue_.size() + send_buffer_.size(); }
    std::size_t pending_bytes() const { return pending_bytes_; }
    std::uint32_t conversation() const { return conversation_; }
    bool failed() const { return failed_; }
    std::uint64_t transmissions() const noexcept { return transmissions_; }
    std::uint64_t retransmissions() const noexcept { return retransmissions_; }
    std::uint32_t smoothed_rtt_ms() const noexcept { return static_cast<std::uint32_t>(smoothed_rtt_); }

private:
    static constexpr std::size_t header_size = 24;
    static constexpr std::uint8_t push_command = 81;
    static constexpr std::uint8_t ack_command = 82;
    static constexpr std::uint8_t probe_command = 83;
    static constexpr std::uint8_t window_command = 84;

    struct segment
    {
        std::string data;
        std::uint32_t sequence = 0;
        std::uint32_t deadline = 0;
        std::uint32_t timeout = 0;
        std::uint64_t transmissions = 0;
        std::uint32_t fast_acks = 0;
        std::uint8_t fragment = 0;
    };
    struct frame
    {
        std::string_view data;
        std::uint32_t timestamp = 0;
        std::uint32_t sequence = 0;
        std::uint32_t unacknowledged = 0;
        std::uint16_t window = 0;
        std::uint8_t command = 0;
        std::uint8_t fragment = 0;
    };

    static std::int32_t difference(std::uint32_t left, std::uint32_t right)
    {
        return std::bit_cast<std::int32_t>(left - right);
    }
    static std::uint8_t byte(std::string_view value, std::size_t offset)
    {
        return static_cast<std::uint8_t>(value[offset]);
    }
    static std::uint16_t read16(std::string_view value, std::size_t offset)
    {
        return static_cast<std::uint16_t>(byte(value, offset) |
                                          (static_cast<std::uint16_t>(byte(value, offset + 1)) << 8));
    }
    static std::uint32_t read32(std::string_view value, std::size_t offset)
    {
        std::uint32_t result = 0;
        for (std::size_t index = 0; index < 4; ++index)
            result |= static_cast<std::uint32_t>(byte(value, offset + index)) << (index * 8);
        return result;
    }
    static void write16(std::string& target, std::uint16_t value)
    {
        target.push_back(static_cast<char>(value & 0xff));
        target.push_back(static_cast<char>(value >> 8));
    }
    static void write32(std::string& target, std::uint32_t value)
    {
        for (std::size_t index = 0; index < 4; ++index)
            target.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
    }
    std::size_t payload_size() const { return options_.mtu - header_size; }
    std::uint32_t oldest_send() const { return send_buffer_.empty() ? next_send_ : send_buffer_.front().sequence; }
    std::uint16_t available_window() const
    {
        return static_cast<std::uint16_t>(options_.receive_window - receive_queue_.size());
    }
    void acknowledge_before(std::uint32_t sequence)
    {
        while (!send_buffer_.empty() && difference(sequence, send_buffer_.front().sequence) > 0)
        {
            pending_bytes_ -= send_buffer_.front().data.size();
            send_buffer_.pop_front();
        }
    }
    void acknowledge(std::uint32_t sequence)
    {
        for (auto position = send_buffer_.begin(); position != send_buffer_.end(); ++position)
        {
            if (position->sequence == sequence)
            {
                pending_bytes_ -= position->data.size();
                send_buffer_.erase(position);
                break;
            }
        }
    }
    void update_timeout(std::uint32_t elapsed)
    {
        const auto sample = static_cast<std::int64_t>(std::min<std::uint32_t>(elapsed, 60000));
        if (smoothed_rtt_ == 0)
        {
            smoothed_rtt_ = std::max<std::int64_t>(1, sample);
            rtt_variance_ = sample / 2;
        }
        else
        {
            const auto deviation = sample > smoothed_rtt_ ? sample - smoothed_rtt_ : smoothed_rtt_ - sample;
            rtt_variance_ = (3 * rtt_variance_ + deviation) / 4;
            smoothed_rtt_ = std::max<std::int64_t>(1, (7 * smoothed_rtt_ + sample) / 8);
        }
        retransmit_timeout_ = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(smoothed_rtt_ + std::max<std::int64_t>(options_.interval, 4 * rtt_variance_),
                                     options_.nodelay ? 30 : 100, 60000));
    }
    bool accept(const frame& value)
    {
        if (difference(value.sequence, next_receive_ + options_.receive_window) >= 0)
            return true;
        if (difference(value.sequence, next_receive_) >= 0)
        {
            auto position = receive_buffer_.begin();
            while (position != receive_buffer_.end() && difference(value.sequence, position->sequence) > 0)
                ++position;
            if (position == receive_buffer_.end() || position->sequence != value.sequence)
            {
                const auto cost = value.data.size() + header_size;
                if (cost > options_.max_pending_bytes - receive_bytes_)
                    return false;
                segment fragment;
                fragment.sequence = value.sequence;
                fragment.fragment = value.fragment;
                fragment.data.assign(value.data);
                receive_buffer_.insert(position, std::move(fragment));
                receive_bytes_ += cost;
                promote_received();
            }
        }
        if (acknowledgements_.size() >= options_.receive_window && !acknowledgements_.contains(value.sequence))
            acknowledgements_.erase(acknowledgements_.begin());
        acknowledgements_[value.sequence] = value.timestamp;
        return true;
    }
    void promote_received()
    {
        while (!receive_buffer_.empty() && receive_buffer_.front().sequence == next_receive_ &&
               receive_queue_.size() < options_.receive_window)
        {
            receive_queue_.splice(receive_queue_.end(), receive_buffer_, receive_buffer_.begin());
            ++next_receive_;
        }
    }
    void grow_window()
    {
        if (!options_.congestion_control || congestion_window_ >= remote_window_)
            return;
        if (congestion_window_ < slow_start_threshold_)
        {
            ++congestion_window_;
            congestion_bytes_ += payload_size();
        }
        else
        {
            const auto payload = static_cast<std::uint64_t>(payload_size());
            congestion_bytes_ = std::max(congestion_bytes_, payload);
            congestion_bytes_ += payload * payload / congestion_bytes_ + payload / 16;
            if ((static_cast<std::uint64_t>(congestion_window_) + 1) * payload <= congestion_bytes_)
                ++congestion_window_;
        }
        congestion_window_ = std::min<std::uint32_t>(congestion_window_, remote_window_);
    }

    std::uint32_t conversation_;
    options options_;
    std::function<void(std::string_view)> output_;
    std::list<segment> send_queue_;
    std::list<segment> send_buffer_;
    std::list<segment> receive_queue_;
    std::list<segment> receive_buffer_;
    std::unordered_map<std::uint32_t, std::uint32_t> acknowledgements_;
    std::size_t pending_bytes_ = 0;
    std::size_t receive_bytes_ = 0;
    std::uint64_t congestion_bytes_ = 0;
    std::uint64_t transmissions_ = 0, retransmissions_ = 0;
    std::int64_t smoothed_rtt_ = 0;
    std::int64_t rtt_variance_ = 0;
    std::uint32_t current_ = 0;
    std::uint32_t next_flush_ = 0;
    std::uint32_t next_send_ = 0;
    std::uint32_t next_receive_ = 0;
    std::uint32_t remote_window_ = 128;
    std::uint32_t congestion_window_ = 1;
    std::uint32_t slow_start_threshold_ = 2;
    std::uint32_t retransmit_timeout_ = 200;
    std::uint32_t probe_delay_ = 0;
    std::uint32_t next_probe_ = 0;
    bool announce_window_ = false;
    bool updated_ = false;
    bool failed_ = false;
};
}
