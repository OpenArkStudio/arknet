// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#define ARKNET_CONNECTION_BENCHMARK_NO_MAIN
#include "../benchmarks/connections.cpp"
#include "check.hpp"

namespace {

class silent_peer
{
public:
    explicit silent_peer(std::size_t count)
        : acceptor_(context_, {asio::ip::address_v4::loopback(), 0})
        , port_(acceptor_.local_endpoint().port())
        , remaining_(count) {
        accept();
        worker_ = std::thread([this] { context_.run(); });
    }

    ~silent_peer() {
        asio::post(context_, [this] {
            arknet::error_code ignored;
            acceptor_.close(ignored);
            for (const auto& socket : sockets_)
            {
                socket->close(ignored);
            }
            work_.reset();
        });
        worker_.join();
    }

    std::uint16_t port() const {
        return port_;
    }

    std::size_t accepted() const {
        return accepted_.load();
    }

private:
    void accept() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(context_);
        acceptor_.async_accept(*socket, [this, socket](const arknet::error_code& error) {
            if (!error)
            {
                // Keep the peer open without acknowledging HELLO or closing on FIN.
                sockets_.push_back(socket);
                ++accepted_;
                if (--remaining_)
                {
                    accept();
                }
            }
        });
    }

    asio::io_context context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_{context_.get_executor()};
    asio::ip::tcp::acceptor acceptor_;
    const std::uint16_t port_;
    std::size_t remaining_;
    std::atomic_size_t accepted_{};
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
    std::thread worker_;
};

} // namespace

DOCTEST_TEST_CASE("Connection load cleanup overlaps per-client shutdown deadlines and drains native callbacks") {
    constexpr std::size_t count = 8;
    cleanup_watchdog watchdog(clock_type::now() + 12s);
    silent_peer peer(count);
    arknet::io_pool runtime(1);
    REQUIRE(runtime.start());
    options settings;
    settings.host = "127.0.0.1";
    settings.port = peer.port();
    settings.run = 20261017;
    settings.client = 1;
    settings.connections = count;
    settings.maximum = count;
    settings.cleanup_seconds = 1;
    counters values;
    client_run client(settings, values, runtime);
    for (std::size_t index = 0; index < count; ++index)
    {
        client.launch();
    }
    const auto ready_deadline = clock_type::now() + 2s;
    while (clock_type::now() < ready_deadline &&
           (values.established != count || peer.accepted() != count || values.pending_sends))
    {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(values.established == count);
    CHECK(peer.accepted() == count);
    CHECK(values.pending_sends == 0);
    CHECK(values.ready == 0);
    const auto began = clock_type::now();
    client.stop();
    const auto elapsed = clock_type::now() - began;
    CHECK(elapsed < 4s);
    CHECK(values.active == 0);
    CHECK(values.disconnected == count);
    CHECK(values.pending_sends == 0);
    CHECK(values.pending_bytes == 0);
    CHECK(values.failed == false);
    runtime.stop();
    CHECK(runtime.stopped());
}
