// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/arq/core.hpp>
#include <arknet/base/error.hpp>
#include <arknet/fec/transport.hpp>

namespace arknet::rudp {

struct statistics
{
    std::uint64_t transmissions{}, retransmissions{}, wire_bytes_tx{}, wire_bytes_rx{};
    std::uint32_t smoothed_rtt_ms{};
    std::size_t arq_pending_bytes{}, raw_queued_bytes{};
    arknet::fec::statistics fec{};
};
} // namespace arknet::rudp

namespace arknet::detail {

inline std::uint32_t rudp_clock() noexcept {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

inline std::uint32_t rudp_next_id() noexcept {
    static std::atomic<std::uint32_t> next{rudp_clock() | 1U};
    auto value = next.fetch_add(1, std::memory_order_relaxed);
    if (!value)
    {
        value = next.fetch_add(1, std::memory_order_relaxed);
    }
    return value;
}

inline void rudp_put_id(char* destination, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
    {
        destination[i] = static_cast<char>(value >> (8 * i));
    }
}

inline std::uint32_t rudp_read_id(const char* source) noexcept {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= std::uint32_t(static_cast<unsigned char>(source[i])) << (8 * i);
    }
    return value;
}

// The control envelope is separate from the KCP-compatible ARQ segment format.
inline std::string rudp_control(unsigned char type, std::uint32_t sync, std::uint32_t conversation = 0) {
    std::string bytes(type == 1 ? 5 : 9, '\0');
    bytes[0] = static_cast<char>(type);
    rudp_put_id(bytes.data() + 1, sync);
    if (bytes.size() == 9)
    {
        rudp_put_id(bytes.data() + 5, conversation);
    }
    return bytes;
}

inline bool rudp_is_control(std::string_view bytes, unsigned char type) noexcept {
    return bytes.size() == (type == 1 ? 5U : 9U) && static_cast<unsigned char>(bytes[0]) == type;
}

inline std::string rudp_fec_control(
    unsigned char type, std::uint32_t sync, std::uint32_t conversation = 0, std::size_t mtu = 1200) {
    std::string bytes(type == 4 ? 8 : type == 5 ? 12 : 5, '\0');
    bytes[0] = static_cast<char>(type);
    rudp_put_id(bytes.data() + 1, sync);
    if (type == 4 || type == 5)
    {
        const auto offset = type == 4 ? 5 : 9;
        if (type == 5)
        {
            rudp_put_id(bytes.data() + 5, conversation);
        }
        bytes[offset] = 1;
        bytes[offset + 1] = static_cast<char>(mtu);
        bytes[offset + 2] = static_cast<char>(mtu >> 8);
    }
    return bytes;
}

inline bool rudp_is_fec_control(std::string_view bytes, unsigned char type) noexcept {
    return (type == 4 || type == 5 || type == 6) &&
           bytes.size() == (type == 4      ? 8U
                               : type == 5 ? 12U
                                           : 5U) &&
           static_cast<unsigned char>(bytes[0]) == type;
}

inline std::size_t rudp_fec_mtu(std::string_view bytes, unsigned char type) noexcept {
    const auto offset = type == 4 ? 6U : 10U;
    return static_cast<unsigned char>(bytes[offset]) | std::size_t(static_cast<unsigned char>(bytes[offset + 1])) << 8;
}

inline arq::options rudp_arq_options(arq::options options, const fec::options& fec_options) {
    if (fec_options.mode != fec::policy::disabled)
    {
        if (options.mtu <= fec::transport::overhead + 24 || options.mtu > 65507)
        {
            throw std::invalid_argument("invalid FEC wire MTU");
        }
        options.mtu -= fec::transport::overhead;
    }
    return options;
}

inline bool rudp_ack_only(std::string_view bytes) noexcept {
    while (bytes.size() >= 24)
    {
        if (static_cast<unsigned char>(bytes[4]) == 81)
        {
            return false;
        }
        const auto size = 24 + std::size_t(rudp_read_id(bytes.data() + 20));
        if (size > bytes.size())
        {
            return false;
        }
        bytes.remove_prefix(size);
    }
    return bytes.empty();
}

class rudp_channel : public std::enable_shared_from_this<rudp_channel>
{
public:
    using delivery = std::function<void(std::string_view)>;
    using failure = std::function<void(error_code)>;

    rudp_channel(std::shared_ptr<asio::ip::udp::socket> socket, asio::ip::udp::endpoint remote, bool connected,
        std::uint32_t conversation, const arq::options& options, delivery deliver, failure fail,
        const fec::options& fec_options = {})
        : socket_(std::move(socket))
        , remote_(std::move(remote))
        , connected_(connected)
        , core_(conversation, rudp_arq_options(options, fec_options))
        , timer_(socket_->get_executor())
        , options_(options)
        , deliver_(std::move(deliver))
        , fail_(std::move(fail)) {
        if (fec_options.mode != fec::policy::disabled)
        {
            fec_ = std::make_unique<fec::transport>(
                conversation, options.mtu, fec_options, [this](std::string_view bytes) { raw_send(bytes); },
                [this](std::string_view bytes) { return accept_arq(bytes); });
            tick_interval_ = std::min(options.interval, fec_options.group_wait_ms);
        }
        else
        {
            tick_interval_ = options.interval;
        }
        core_.set_output([this](std::string_view bytes) {
            if (stopped_)
            {
                return;
            }
            if (fec_)
            {
                fec_->send(bytes, rudp_clock(), rudp_ack_only(bytes));
            }
            else
            {
                raw_send(bytes);
            }
        });
    }

    void start() {
        core_.update(rudp_clock());
        arm();
    }

    void stop() {
        if (stopped_)
        {
            return;
        }
        stopped_ = true;
        timer_.cancel();
        if (fec_)
        {
            fec_->stop();
        }
        deliver_ = {};
        fail_ = {};
        queue_.clear();
        queued_bytes_ = 0;
    }

    std::uint32_t conversation() const noexcept {
        return core_.conversation();
    }

    arknet::rudp::statistics statistics() const noexcept {
        arknet::rudp::statistics value;
        value.transmissions = core_.transmissions();
        value.retransmissions = core_.retransmissions();
        value.smoothed_rtt_ms = core_.smoothed_rtt_ms();
        value.arq_pending_bytes = core_.pending_bytes();
        value.wire_bytes_tx = wire_bytes_tx_;
        value.wire_bytes_rx = wire_bytes_rx_;
        value.raw_queued_bytes = queued_bytes_;
        if (fec_)
        {
            value.fec = fec_->snapshot();
        }
        return value;
    }

    int send(std::string_view bytes) {
        if (stopped_)
        {
            return -3;
        }
        const auto result = core_.send(bytes);
        if (!result)
        {
            core_.update(rudp_clock());
            core_.flush();
        }
        return stopped_ ? -3 : result;
    }

    bool input(std::string_view bytes) {
        if (stopped_)
        {
            return false;
        }
        wire_bytes_rx_ += bytes.size();
        return fec_ ? fec_->input(bytes, rudp_clock()) : accept_arq(bytes);
    }

    bool accept_arq(std::string_view bytes) {
        if (stopped_)
        {
            return false;
        }
        const auto result = core_.input(bytes);
        if (result == -5)
        {
            fail(asio::error::no_buffer_space);
            return false;
        }
        if (result != 0)
        {
            return false;
        }
        std::string message;
        while (!stopped_ && core_.receive(message))
        {
            const auto callback = deliver_;
            callback(message);
        }
        if (!stopped_)
        {
            core_.update(rudp_clock());
            core_.flush();
        }
        return !stopped_;
    }

    void raw_send(std::string_view bytes) {
        if (stopped_)
        {
            return;
        }
        if (bytes.size() > options_.max_pending_bytes || queued_bytes_ > options_.max_pending_bytes - bytes.size())
        {
            fail(asio::error::no_buffer_space);
            return;
        }
        queue_.push_back(std::make_shared<std::string>(bytes));
        queued_bytes_ += bytes.size();
        wire_bytes_tx_ += bytes.size();
        if (!writing_)
        {
            write();
        }
    }

private:
    void fail(error_code error) {
        if (stopped_)
        {
            return;
        }
        const auto callback = fail_;
        stop();
        callback(error);
    }

    void arm() {
        if (stopped_)
        {
            return;
        }
        timer_.expires_after(std::chrono::milliseconds(tick_interval_));
        timer_.async_wait([self = shared_from_this()](error_code error) {
            if (error || self->stopped_)
            {
                return;
            }
            const auto now = rudp_clock();
            self->core_.update(now);
            if (self->fec_ && !self->stopped_)
            {
                self->fec_->adapt(now, self->core_.transmissions(), self->core_.retransmissions(),
                    self->core_.smoothed_rtt_ms(), std::max(self->queued_bytes_, self->core_.pending_bytes()),
                    self->options_.max_pending_bytes);
                self->fec_->update(now);
            }
            if (self->core_.failed())
            {
                self->fail(asio::error::timed_out);
            }
            else
            {
                self->arm();
            }
        });
    }

    void write() {
        if (stopped_ || queue_.empty())
        {
            return;
        }
        writing_ = true;
        auto bytes = queue_.front();
        auto completion = [self = shared_from_this(), bytes](error_code error, std::size_t) {
            self->writing_ = false;
            if (self->stopped_)
            {
                return;
            }
            self->queued_bytes_ -= bytes->size();
            self->queue_.pop_front();
            if (error)
            {
                self->fail(error);
            }
            else
            {
                self->write();
            }
        };
        try
        {
            if (connected_)
            {
                socket_->async_send(asio::buffer(*bytes), std::move(completion));
            }
            else
            {
                socket_->async_send_to(asio::buffer(*bytes), remote_, std::move(completion));
            }
        }
        catch (const std::bad_alloc&)
        {
            writing_ = false;
            fail(asio::error::no_memory);
            throw;
        }
    }

    std::shared_ptr<asio::ip::udp::socket> socket_;
    asio::ip::udp::endpoint remote_;
    bool connected_{};
    arq::control_block core_;
    std::unique_ptr<fec::transport> fec_;
    asio::steady_timer timer_;
    arq::options options_;
    delivery deliver_;
    failure fail_;
    std::deque<std::shared_ptr<std::string>> queue_;
    std::size_t queued_bytes_{};
    std::uint64_t wire_bytes_tx_{}, wire_bytes_rx_{};
    std::uint32_t tick_interval_{};
    bool stopped_{};
    bool writing_{};
};
} // namespace arknet::detail
