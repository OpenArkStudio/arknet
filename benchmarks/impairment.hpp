// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/base/error.hpp>

namespace arknet_benchmark {
struct impairment_options
{
    unsigned loss_percent{}, duplicate_percent{}, reorder_percent{}, delay_ms{};
    unsigned burst_count{}, burst_period = 100;
    std::uint64_t seed = 0x61726b6e6574ULL;
    void validate() const {
        if (loss_percent > 100 || duplicate_percent > 100 || reorder_percent > 100 || delay_ms > 1000 ||
            burst_count >= burst_period || !burst_period || burst_period > 1000000)
        {
            throw std::invalid_argument("invalid UDP impairment configuration");
        }
    }
};
struct impairment_statistics
{
    std::uint64_t received_packets{}, received_bytes{}, forwarded_packets{}, forwarded_bytes{};
    std::uint64_t random_drops{}, burst_drops{}, duplicate_packets{}, reordered_packets{};
    std::uint64_t overflow_drops{}, errors{};
    std::size_t retained_bytes{}, retained_packets{}, peak_retained_bytes{}, peak_retained_packets{};
};

// A separate serialized context keeps impairment scheduling out of the endpoint IO model.
class udp_impairment
{
    using udp = asio::ip::udp;
    using clock = std::chrono::steady_clock;
    struct route : std::enable_shared_from_this<route>
    {
        struct packet
        {
            std::string bytes;
            udp::endpoint destination;
            unsigned socket{};
        };
        route(asio::io_context& context, udp::endpoint server, impairment_options options)
            : sockets{udp::socket(context), udp::socket(context)}
            , timer(context)
            , server(std::move(server))
            , options(options)
            , random{std::mt19937_64(options.seed), std::mt19937_64(options.seed ^ 0xa17e34bdULL)} {
            for (auto& socket : sockets)
            {
                socket.open(udp::v4());
                socket.set_option(asio::socket_base::receive_buffer_size(4 * 1024 * 1024));
                socket.bind({asio::ip::make_address("127.0.0.1"), 0});
            }
        }
        void start() {
            receive(0);
            receive(1);
        }
        void close() noexcept {
            closed = true;
            try
            { timer.cancel(); }
            catch (...)
            {}
            arknet::error_code error;
            for (auto& socket : sockets)
            {
                socket.close(error);
            }
            delayed.clear();
            for (auto& queue : ready)
            {
                queue.clear();
            }
            stats.retained_bytes = stats.retained_packets = 0;
        }
        void receive(unsigned socket) {
            auto self = shared_from_this();
            sockets[socket].async_receive_from(asio::buffer(buffers[socket]), incoming[socket],
                [self, socket](arknet::error_code error, std::size_t size) {
                    if (self->closed)
                    {
                        return;
                    }
                    if (error)
                    {
                        ++self->stats.errors;
                        self->close();
                        return;
                    }
                    try
                    {
                        self->received(socket, {self->buffers[socket].data(), size});
                        self->receive(socket);
                    }
                    catch (...)
                    {
                        ++self->stats.errors;
                        self->close();
                    }
                });
        }
        bool chance(unsigned socket, unsigned percent) {
            return percent != 0 && std::uniform_int_distribution<unsigned>(0, 99)(random[socket]) < percent;
        }
        void received(unsigned socket, std::string_view bytes) {
            if (socket == 0)
            {
                if (!client)
                {
                    client = incoming[socket];
                }
                if (*client != incoming[socket])
                {
                    ++stats.errors;
                    return;
                }
            }
            else if (incoming[socket] != server || !client)
            {
                ++stats.errors;
                return;
            }
            ++stats.received_packets;
            stats.received_bytes += bytes.size();
            const auto sequence = ++sequences[socket];
            if (options.burst_count && sequence % options.burst_period >= options.burst_period - options.burst_count)
            {
                ++stats.burst_drops;
                return;
            }
            if (chance(socket, options.loss_percent))
            {
                ++stats.random_drops;
                return;
            }
            const bool reorder = chance(socket, options.reorder_percent);
            if (reorder)
            {
                ++stats.reordered_packets;
            }
            const auto due =
                clock::now() + std::chrono::milliseconds(options.delay_ms + (reorder ? options.delay_ms + 5 : 0));
            schedule(bytes, socket, due);
            if (chance(socket, options.duplicate_percent))
            {
                ++stats.duplicate_packets;
                schedule(bytes, socket, due + std::chrono::milliseconds(1));
            }
        }
        void schedule(std::string_view bytes, unsigned received_socket, clock::time_point due) {
            if (stats.retained_bytes + bytes.size() > max_buffer_bytes || stats.retained_packets >= max_packets)
            {
                ++stats.overflow_drops;
                return;
            }
            auto value = std::make_shared<packet>();
            value->bytes.assign(bytes);
            value->socket = 1 - received_socket;
            value->destination = received_socket == 0 ? server : *client;
            ++stats.retained_packets;
            stats.retained_bytes += bytes.size();
            stats.peak_retained_bytes = std::max(stats.peak_retained_bytes, stats.retained_bytes);
            stats.peak_retained_packets = std::max(stats.peak_retained_packets, stats.retained_packets);
            if (due <= clock::now())
            {
                ready[value->socket].push_back(std::move(value));
                send(1 - received_socket);
            }
            else
            {
                delayed.emplace(due, std::move(value));
                arm();
            }
        }
        void arm() {
            if (delayed.empty() || (armed && *armed <= delayed.begin()->first))
            {
                return;
            }
            armed = delayed.begin()->first;
            timer.expires_at(*armed);
            auto self = shared_from_this();
            timer.async_wait([self](arknet::error_code error) {
                if (error || self->closed)
                {
                    return;
                }
                try
                {
                    self->armed.reset();
                    const auto now = clock::now();
                    while (!self->delayed.empty() && self->delayed.begin()->first <= now)
                    {
                        auto value = std::move(self->delayed.begin()->second);
                        self->delayed.erase(self->delayed.begin());
                        self->ready[value->socket].push_back(std::move(value));
                    }
                    self->send(0);
                    self->send(1);
                    self->arm();
                }
                catch (...)
                {
                    ++self->stats.errors;
                    self->close();
                }
            });
        }
        void send(unsigned socket) {
            if (sending[socket] || ready[socket].empty() || closed)
            {
                return;
            }
            sending[socket] = true;
            auto value = std::move(ready[socket].front());
            ready[socket].pop_front();
            auto self = shared_from_this();
            sockets[socket].async_send_to(asio::buffer(value->bytes), value->destination,
                [self, value, socket](arknet::error_code error, std::size_t size) {
                    if (self->closed)
                    {
                        return;
                    }
                    self->sending[socket] = false;
                    self->stats.retained_bytes -= value->bytes.size();
                    --self->stats.retained_packets;
                    if (error || size != value->bytes.size())
                    {
                        ++self->stats.errors;
                    }
                    else
                    {
                        ++self->stats.forwarded_packets;
                        self->stats.forwarded_bytes += size;
                    }
                    try
                    { self->send(socket); }
                    catch (...)
                    {
                        ++self->stats.errors;
                        self->close();
                    }
                });
        }
        std::array<udp::socket, 2> sockets;
        asio::steady_timer timer;
        udp::endpoint server;
        std::optional<udp::endpoint> client;
        impairment_options options;
        std::array<std::mt19937_64, 2> random;
        std::array<std::uint64_t, 2> sequences{};
        std::array<std::array<char, 65536>, 2> buffers{};
        std::array<udp::endpoint, 2> incoming;
        std::multimap<clock::time_point, std::shared_ptr<packet>> delayed;
        std::array<std::deque<std::shared_ptr<packet>>, 2> ready;
        std::array<bool, 2> sending{};
        std::optional<clock::time_point> armed;
        impairment_statistics stats;
        bool closed{};
    };

public:
    static constexpr std::size_t max_buffer_bytes = 8 * 1024 * 1024, max_packets = 8192;
    explicit udp_impairment(impairment_options options)
        : guard(asio::make_work_guard(context))
        , options(options) {
        options.validate();
        worker = std::thread([this] { context.run(); });
    }
    ~udp_impairment() {
        stop();
    }
    udp_impairment(const udp_impairment&) = delete;
    udp_impairment& operator=(const udp_impairment&) = delete;
    unsigned short add(const asio::ip::udp::endpoint& server) {
        return execute([this, server] {
            auto settings = options;
            settings.seed += routes.size() * 0x9e3779b97f4a7c15ULL;
            auto value = std::make_shared<route>(context, server, settings);
            routes.push_back(value);
            try
            { value->start(); }
            catch (...)
            {
                value->close();
                routes.pop_back();
                throw;
            }
            return value->sockets[0].local_endpoint().port();
        });
    }
    impairment_statistics snapshot(bool reset_peak = false) {
        return execute([this, reset_peak] {
            impairment_statistics total;
            for (auto& value : routes)
            {
                const auto& current = value->stats;
                total.received_packets += current.received_packets;
                total.received_bytes += current.received_bytes;
                total.forwarded_packets += current.forwarded_packets;
                total.forwarded_bytes += current.forwarded_bytes;
                total.random_drops += current.random_drops;
                total.burst_drops += current.burst_drops;
                total.duplicate_packets += current.duplicate_packets;
                total.reordered_packets += current.reordered_packets;
                total.overflow_drops += current.overflow_drops;
                total.errors += current.errors;
                total.retained_bytes += current.retained_bytes;
                total.retained_packets += current.retained_packets;
                total.peak_retained_bytes += current.peak_retained_bytes;
                total.peak_retained_packets += current.peak_retained_packets;
                if (reset_peak)
                {
                    value->stats.peak_retained_bytes = current.retained_bytes;
                    value->stats.peak_retained_packets = current.retained_packets;
                }
            }
            return total;
        });
    }
    void stop() noexcept {
        if (!worker.joinable())
        {
            return;
        }
        try
        {
            execute([this] {
                for (auto& value : routes)
                {
                    value->close();
                }
                routes.clear();
            });
        }
        catch (...)
        { context.stop(); }
        guard.reset();
        worker.join();
    }

private:
    template<class Function>
    auto execute(Function function) -> decltype(function()) {
        if (!worker.joinable())
        {
            throw std::logic_error("UDP relay is stopped");
        }
        using result = decltype(function());
        auto task = std::make_shared<std::packaged_task<result()>>(std::move(function));
        auto future = task->get_future();
        asio::post(context, [task] { (*task)(); });
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            throw std::runtime_error("UDP relay context did not respond");
        }
        return future.get();
    }
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    impairment_options options;
    std::thread worker;
    std::vector<std::shared_ptr<route>> routes;
};
} // namespace arknet_benchmark
