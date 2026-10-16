// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <future>
#include <thread>

#include <arknet/tcp/tcp_client.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/tcp/tcps_client.hpp>
#include "check.hpp"

namespace {

struct io_runner
{
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
    std::thread worker{[this] { context.run(); }};

    ~io_runner() {
        work.reset();
        context.stop();
        worker.join();
    }
};

} // namespace

DOCTEST_TEST_CASE("TCP stop rejects a start queued on its IO lane before initialization and permits explicit restart") {
    io_runner runner;
    std::atomic<unsigned> initialized{}, connected{};
    messages received;
    std::promise<void> submitted;
    auto queued = submitted.get_future();
    arknet::tcp_server server(runner.context);
    arknet::tcp_client client(runner.context);
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    client.set_auto_reconnect(false);
    client.bind_init([&] { ++initialized; });
    client.bind_connect([&] { ++connected; });
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "queued-start listener starts");
    client.post([&] {
        DOCTEST_CHECK(client.async_start("127.0.0.1", server.get_listen_port(), arknet::use_dgram));
        client.stop();
        submitted.set_value();
    });
    await(queued);
    check(client.wait_stopped(), "owner joins cancellation of the queued start");
    DOCTEST_CHECK(initialized.load() == 0);
    DOCTEST_CHECK(connected.load() == 0);
    DOCTEST_CHECK(client.is_stopped());
    DOCTEST_CHECK_FALSE(client.socket().is_open());
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram),
        "an explicit start clears the completed stop request");
    client.async_send("restart");
    DOCTEST_CHECK(received.wait(1).front() == "restart");
    DOCTEST_CHECK(initialized.load() == 1);
    DOCTEST_CHECK(connected.load() == 1);
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP stop from init reports one aborted connect without reaching the listening peer") {
    io_runner runner;
    asio::io_context peer_context;
    std::atomic<unsigned> initialized{}, connected{};
    std::promise<arknet::error_code> completion;
    auto completed = completion.get_future();
    asio::ip::tcp::acceptor listener(peer_context, {asio::ip::address_v4::loopback(), 0});
    listener.non_blocking(true);
    const auto port = listener.local_endpoint().port();
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    client.bind_init([&] {
        ++initialized;
        client.stop();
    });
    client.bind_connect([&] {
        const auto error = arknet::get_last_error();
        if (++connected == 1)
        {
            completion.set_value(error);
        }
    });
    DOCTEST_CHECK(client.async_start("127.0.0.1", port));
    DOCTEST_CHECK(await(completed) == asio::error::operation_aborted);
    asio::ip::tcp::socket accepted(peer_context);
    arknet::error_code error;
    listener.accept(accepted, error);
    // Even an immediately closed connection would leave a kernel accept result.
    DOCTEST_CHECK((error == asio::error::would_block || error == asio::error::try_again));
    // Keep the old-code failure bounded too: it may have reached this peer and
    // otherwise wait for peer EOF during stop's graceful shutdown.
    accepted.close(error);
    check(client.wait_stopped(), "owner joins init-triggered cancellation");
    DOCTEST_CHECK(initialized.load() == 1);
    DOCTEST_CHECK(connected.load() == 1);
    DOCTEST_CHECK(client.is_stopped());
    DOCTEST_CHECK_FALSE(client.socket().is_open());
}

DOCTEST_TEST_CASE("TCP queued-start cancellation preserves another active client on the shared context") {
    io_runner runner;
    messages received;
    std::atomic<unsigned> initialized{}, connected{};
    std::promise<void> submitted;
    auto queued = submitted.get_future();
    arknet::tcp_server server(runner.context);
    arknet::tcp_client active(runner.context);
    arknet::tcp_client cancelled(runner.context);
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    active.set_auto_reconnect(false);
    active.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    cancelled.set_auto_reconnect(false);
    cancelled.bind_init([&] { ++initialized; });
    cancelled.bind_connect([&] { ++connected; });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "shared-context listener starts");
    check(active.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "other client connects");
    active.async_send("before");
    DOCTEST_CHECK(received.wait(1).front() == "before");
    cancelled.post([&] {
        DOCTEST_CHECK(cancelled.async_start("127.0.0.1", server.get_listen_port(), arknet::use_dgram));
        cancelled.stop();
        submitted.set_value();
    });
    await(queued);
    check(cancelled.wait_stopped(), "cancelled endpoint completes its own stop");
    DOCTEST_CHECK(initialized.load() == 0);
    DOCTEST_CHECK(connected.load() == 0);
    DOCTEST_CHECK(active.is_started());
    DOCTEST_CHECK_FALSE(runner.context.stopped());
    active.async_send("after");
    DOCTEST_CHECK(received.wait(2).back() == "after");
    active.stop();
    server.stop();
}

#if defined(ARKNET_ENABLE_SSL)
DOCTEST_TEST_CASE("TCP stop queued from TLS init aborts before beginning a silent peer handshake") {
    io_runner runner;
    asio::io_context peer_context;
    std::atomic<unsigned> handshakes{}, connected{};
    std::promise<void> requested;
    auto stopped = requested.get_future();
    std::promise<arknet::error_code> completion;
    auto completed = completion.get_future();
    asio::ip::tcp::acceptor listener(peer_context, {asio::ip::address_v4::loopback(), 0});
    listener.non_blocking(true);
    const auto port = listener.local_endpoint().port();
    arknet::tcps_client client(runner.context);
    client.set_auto_reconnect(false).set_connect_timeout(30s).set_disconnect_timeout(30s);
    client.bind_init([&] {
        // This stop runs after the synchronous post-init guard, before the
        // already-submitted TCP connect callback can enter the TLS handshake.
        client.post([&] {
            client.stop();
            requested.set_value();
        });
    });
    client.bind_handshake([&] { ++handshakes; });
    client.bind_connect([&] {
        const auto error = arknet::get_last_error();
        if (++connected == 1)
        {
            completion.set_value(error);
        }
    });
    DOCTEST_CHECK(client.async_start("127.0.0.1", port));
    await(stopped);
    // This bound detects waiting for the configured 30s handshake/stop timers.
    const bool cancelled = completed.wait_for(5s) == std::future_status::ready;
    DOCTEST_CHECK(cancelled);
    if (!cancelled)
    {
        // Bound the negative regression too: close the pending kernel peer
        // connection so old code leaves its intentionally silent TLS handshake.
        asio::ip::tcp::socket accepted(peer_context);
        arknet::error_code error;
        listener.accept(accepted, error);
        DOCTEST_CHECK_FALSE(bool(error));
        accepted.close(error);
    }
    DOCTEST_CHECK(await(completed) == asio::error::operation_aborted);
    check(client.wait_stopped(), "owner joins cancellation before TLS handshake");
    DOCTEST_CHECK(connected.load() == 1);
    DOCTEST_CHECK(handshakes.load() == 0);
    DOCTEST_CHECK_FALSE(client.socket().is_open());
}
#endif
