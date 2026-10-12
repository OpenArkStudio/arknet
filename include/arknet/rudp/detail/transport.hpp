// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/arq/core.hpp>
#include <arknet/base/error.hpp>

namespace arknet::detail
{
inline std::uint32_t rudp_clock() noexcept
{
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
inline std::uint32_t rudp_next_id() noexcept
{
    static std::atomic<std::uint32_t> next{rudp_clock() | 1U};
    auto value = next.fetch_add(1, std::memory_order_relaxed);
    if (!value)
        value = next.fetch_add(1, std::memory_order_relaxed);
    return value;
}
inline void rudp_put_id(char* destination, std::uint32_t value) noexcept
{
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<char>(value >> (8 * i));
}
inline std::uint32_t rudp_read_id(const char* source) noexcept
{
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(static_cast<unsigned char>(source[i])) << (8 * i);
    return value;
}
// The control envelope is separate from the KCP-compatible ARQ segment format.
inline std::string rudp_control(unsigned char type, std::uint32_t sync, std::uint32_t conversation = 0)
{
    std::string bytes(type == 1 ? 5 : 9, '\0');
    bytes[0] = static_cast<char>(type);
    rudp_put_id(bytes.data() + 1, sync);
    if (bytes.size() == 9)
        rudp_put_id(bytes.data() + 5, conversation);
    return bytes;
}
inline bool rudp_is_control(std::string_view bytes, unsigned char type) noexcept
{
    return bytes.size() == (type == 1 ? 5U : 9U) && static_cast<unsigned char>(bytes[0]) == type;
}

class rudp_channel : public std::enable_shared_from_this<rudp_channel>
{
public:
    using delivery = std::function<void(std::string_view)>;
    using failure = std::function<void(error_code)>;
    rudp_channel(std::shared_ptr<asio::ip::udp::socket> socket, asio::ip::udp::endpoint remote, bool connected,
                 std::uint32_t conversation, const arq::options& options, delivery deliver, failure fail)
        : socket_(std::move(socket)), remote_(std::move(remote)), connected_(connected), core_(conversation, options),
          timer_(socket_->get_executor()), options_(options), deliver_(std::move(deliver)), fail_(std::move(fail))
    {
        core_.set_output([this](std::string_view bytes) { raw_send(bytes); });
    }
    void start()
    {
        core_.update(rudp_clock());
        arm();
    }
    void stop()
    {
        if (stopped_)
            return;
        stopped_ = true;
        timer_.cancel();
        deliver_ = {};
        fail_ = {};
        queue_.clear();
        queued_bytes_ = 0;
    }
    std::uint32_t conversation() const noexcept { return core_.conversation(); }
    int send(std::string_view bytes)
    {
        if (stopped_)
            return -3;
        const auto result = core_.send(bytes);
        if (!result)
        {
            core_.update(rudp_clock());
            core_.flush();
        }
        return stopped_ ? -3 : result;
    }
    void input(std::string_view bytes)
    {
        if (stopped_)
            return;
        const auto result = core_.input(bytes);
        if (result == -5)
        {
            fail(asio::error::no_buffer_space);
            return;
        }
        if (result != 0)
            return;
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
    }
    void raw_send(std::string_view bytes)
    {
        if (stopped_)
            return;
        if (bytes.size() > options_.max_pending_bytes || queued_bytes_ > options_.max_pending_bytes - bytes.size())
        {
            fail(asio::error::no_buffer_space);
            return;
        }
        queued_bytes_ += bytes.size();
        queue_.push_back(std::make_shared<std::string>(bytes));
        if (!writing_)
            write();
    }

private:
    void fail(error_code error)
    {
        if (stopped_)
            return;
        const auto callback = fail_;
        stop();
        callback(error);
    }
    void arm()
    {
        if (stopped_)
            return;
        timer_.expires_after(std::chrono::milliseconds(options_.interval));
        timer_.async_wait(
            [self = shared_from_this()](error_code error)
            {
                if (error || self->stopped_)
                    return;
                self->core_.update(rudp_clock());
                if (self->core_.failed())
                    self->fail(asio::error::timed_out);
                else
                    self->arm();
            });
    }
    void write()
    {
        if (stopped_ || queue_.empty())
            return;
        writing_ = true;
        auto bytes = queue_.front();
        auto completion = [self = shared_from_this(), bytes](error_code error, std::size_t)
        {
            self->writing_ = false;
            if (self->stopped_)
                return;
            self->queued_bytes_ -= bytes->size();
            self->queue_.pop_front();
            if (error)
                self->fail(error);
            else
                self->write();
        };
        if (connected_)
            socket_->async_send(asio::buffer(*bytes), std::move(completion));
        else
            socket_->async_send_to(asio::buffer(*bytes), remote_, std::move(completion));
    }
    std::shared_ptr<asio::ip::udp::socket> socket_;
    asio::ip::udp::endpoint remote_;
    bool connected_{};
    arq::control_block core_;
    asio::steady_timer timer_;
    arq::options options_;
    delivery deliver_;
    failure fail_;
    std::deque<std::shared_ptr<std::string>> queue_;
    std::size_t queued_bytes_{};
    bool stopped_{};
    bool writing_{};
};
}
