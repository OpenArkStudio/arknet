// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <arknet/base/error.hpp>

namespace arknet_interop {

class data_impairment
{
    using udp = asio::ip::udp;
    using clock = std::chrono::steady_clock;

    struct pending_packet
    {
        std::string bytes;
        udp::endpoint destination;
        clock::time_point deadline;
    };

public:
    struct statistics
    {
        std::uint64_t dropped{}, reordered{}, errors{}, fin_forwarded{};
    };

    explicit data_impairment(unsigned short server_port)
        : sockets_{udp::socket(context_), udp::socket(context_)}
        , server_(asio::ip::make_address("127.0.0.1"), server_port) {
        for (auto& socket : sockets_)
        {
            socket.open(udp::v4());
            socket.set_option(asio::socket_base::receive_buffer_size(1024 * 1024));
            socket.bind({asio::ip::make_address("127.0.0.1"), 0});
            socket.non_blocking(true);
        }
        port_ = sockets_[0].local_endpoint().port();
        worker_ = std::thread([this] { run(); });
    }

    ~data_impairment() {
        stop();
    }

    data_impairment(const data_impairment&) = delete;
    data_impairment& operator=(const data_impairment&) = delete;

    unsigned short port() const noexcept {
        return port_;
    }

    statistics snapshot() const {
        return {dropped_.load(), reordered_.load(), errors_.load(), fin_forwarded_.load()};
    }

    bool wait_fin(std::chrono::seconds timeout) {
        std::unique_lock lock(fin_mutex_);
        return fin_ready_.wait_for(lock, timeout, [this] { return fin_forwarded_ != 0; });
    }

    void stop() noexcept {
        stopped_ = true;
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

private:
    void forward(unsigned direction, const std::string& bytes, const udp::endpoint& destination) {
        arknet::error_code error;
        const auto written = sockets_[1 - direction].send_to(asio::buffer(bytes), destination, 0, error);
        if (error || written != bytes.size())
        {
            ++errors_;
        }
        else if (bytes.size() == 9 && bytes[0] == 3)
        {
            std::lock_guard lock(fin_mutex_);
            ++fin_forwarded_;
            fin_ready_.notify_all();
        }
    }

    void release(unsigned direction) {
        auto packet = std::move(*pending_[direction]);
        pending_[direction].reset();
        forward(direction, packet.bytes, packet.destination);
    }

    void receive(unsigned direction) {
        while (!stopped_)
        {
            udp::endpoint sender;
            arknet::error_code error;
            const auto size = sockets_[direction].receive_from(asio::buffer(buffer_), sender, 0, error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                return;
            }
            if (error)
            {
                ++errors_;
                return;
            }
            if (direction == 0 && !client_)
            {
                client_ = sender;
            }
            if ((direction == 0 && sender != *client_) || (direction == 1 && (sender != server_ || !client_)))
            {
                ++errors_;
                continue;
            }
            const auto destination = direction == 0 ? server_ : *client_;
            std::string bytes(buffer_.data(), size);
            // SYN/ACK/FIN are 5 or 9 bytes; preserve them because FIN has no retry/ACK.
            if (size < 24)
            {
                forward(direction, bytes, destination);
                continue;
            }
            const auto sequence = ++sequences_[direction];
            if (sequence % 9 == 0)
            {
                ++dropped_;
                continue;
            }
            if (sequence % 7 == 0 && !pending_[direction])
            {
                pending_[direction] =
                    pending_packet{std::move(bytes), destination, clock::now() + std::chrono::milliseconds(6)};
                continue;
            }
            forward(direction, bytes, destination);
            if (pending_[direction])
            {
                ++reordered_;
                release(direction);
            }
        }
    }

    void run() noexcept {
        try
        {
            while (!stopped_)
            {
                for (unsigned direction = 0; direction < 2; ++direction)
                {
                    receive(direction);
                    if (pending_[direction] && pending_[direction]->deadline <= clock::now())
                    {
                        release(direction);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        catch (...)
        { ++errors_; }
    }

    asio::io_context context_;
    unsigned short port_{};
    std::array<udp::socket, 2> sockets_;
    udp::endpoint server_;
    std::optional<udp::endpoint> client_;
    std::array<char, 65536> buffer_{};
    std::array<std::uint64_t, 2> sequences_{};
    std::array<std::optional<pending_packet>, 2> pending_;
    std::atomic<std::uint64_t> dropped_{}, reordered_{}, errors_{};
    std::atomic<std::uint64_t> fin_forwarded_{};
    std::mutex fin_mutex_;
    std::condition_variable fin_ready_;
    std::atomic<bool> stopped_{};
    std::thread worker_;
};
} // namespace arknet_interop
