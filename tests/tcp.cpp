// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <atomic>
#include <future>
#include <string>
#include <vector>

#include <arknet/tcp/tcp_client.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include "check.hpp"

class custom_session : public arknet::tcp_session_t<custom_session>
{
public:
    using arknet::tcp_session_t<custom_session>::tcp_session_t;
};

class guarded_client : public arknet::tcp_client_t<guarded_client>
{
public:
    using arknet::tcp_client_t<guarded_client>::tcp_client_t;
    std::atomic<int> filter_calls{};

    std::string_view data_filter_before_send(std::string_view data) {
        ++filter_calls;
        return data;
    }

    auto send_guarded(std::string data) {
        std::promise<std::pair<arknet::error_code, std::size_t>> promise;
        auto future = promise.get_future();
        push_event([this, data = std::move(data), promise = std::move(promise)](
                       arknet::detail::event_queue_guard<guarded_client> guard) mutable {
            internal_async_send(
                selfptr(), std::move(data),
                [promise = std::move(promise)](std::shared_ptr<guarded_client>, const arknet::error_code& ec,
                    std::size_t count,
                    arknet::detail::event_queue_guard<guarded_client>) mutable { promise.set_value({ec, count}); },
                std::move(guard));
        });
        return future;
    }
};

DOCTEST_TEST_CASE("TCP raw sends own buffers and guarded temporary payloads") {
    messages received;
    arknet::tcp_server_t<custom_session> server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0), "raw TCP server start");
    guarded_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port(), asio::transfer_exactly(6)), "raw TCP connect");
    std::string source = "copied";
    std::promise<std::pair<arknet::error_code, std::size_t>> sent;
    auto completion = sent.get_future();
    client.async_send(asio::const_buffer(source.data(), source.size()),
        [&](const arknet::error_code& ec, std::size_t count) { sent.set_value({ec, count}); });
    source.assign(source.size(), 'x');
    auto result = await(completion);
    check(!result.first && result.second == 6, "TCP send completion");
    check(received.wait(1).front() == "copied", "send must own const_buffer data");
    auto guarded_completion = client.send_guarded(std::string(8190, 'g'));
    const auto guarded_result = await(guarded_completion);
    check(!guarded_result.first && guarded_result.second == 8190, "guarded temporary payload completion");
    const auto chunks = received.wait(1366);
    std::string reconstructed;
    for (std::size_t i = 1; i < chunks.size(); ++i)
    {
        reconstructed += chunks[i];
    }
    check(reconstructed == std::string(8190, 'g') && client.get_queued_send_buffer_size() == 0,
        "guarded temporary payload lifetime and queue accounting");
    check(client.filter_calls == 2, "view filter must run once per send");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP datagram framing preserves ordered message boundaries") {
    messages received;
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "framed TCP server start");
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "framed TCP connect");
    client.async_send(std::string("first"));
    client.async_send(std::string(8000, 'b'));
    client.async_send(std::string("third"));
    auto data = received.wait(3);
    check(data[0] == "first" && data[1] == std::string(8000, 'b') && data[2] == "third", "framing preserves messages");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP datagram framing covers each length prefix boundary") {
    messages received;
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "boundary server start");
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "boundary connect");
    std::vector<std::string> expected;
    for (const auto length : {0u, 1u, 253u, 254u, 65535u, 65536u})
    {
        expected.emplace_back(length, static_cast<char>('a' + expected.size()));
        client.async_send(expected.back());
    }
    check(received.wait(expected.size()) == expected, "little-endian prefix boundaries and empty frames");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP datagram receives retain split prefixes and consecutive frames at the buffer limit") {
    messages received;
    // The first frame exactly fills the receive limit, including its prefix.
    arknet::tcp_server server(16, 257, 1);
    server.bind_recv([&](auto&, std::string_view bytes) { received.push(bytes); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "bounded framed server start");
    asio::io_context context;
    asio::ip::tcp::socket peer(context);
    peer.connect({asio::ip::make_address("127.0.0.1"), server.get_listen_port()});

    const std::string first(254, 'a');
    const std::array<unsigned char, 3> prefix{254, 254, 0};
    // A native peer bypasses arknet's sender so receiver and sender cannot share
    // a framing bug. Separate writes also allow incomplete prefix/body reads.
    for (const auto byte : prefix)
    {
        asio::write(peer, asio::buffer(&byte, 1));
    }
    asio::write(peer, asio::buffer(first.data(), 17));
    // One write contains the rest of the first frame, an empty frame, and the
    // following frame. TCP may split or coalesce any of these writes.
    std::string tail = first.substr(17);
    tail += '\0';
    tail += static_cast<char>(4);
    tail += "next";
    asio::write(peer, asio::buffer(tail));
    const std::vector<std::string> expected{first, "", "next"};
    check(
        received.wait(3) == expected, "split prefixes, exact limit and buffered follow-up frames preserve boundaries");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("TCP datagram rejects invalid or oversized frames without delivering partial data") {
    std::string wire;
    arknet::error_code expected;
    DOCTEST_SUBCASE("noncanonical two-byte length") {
        wire = std::string("\xfe\x01\x00", 3);
        expected = asio::error::no_data;
    }
    DOCTEST_SUBCASE("noncanonical eight-byte length") {
        wire = std::string("\xff\xfe\x00\x00\x00\x00\x00\x00\x00", 9);
        expected = asio::error::no_data;
    }
    DOCTEST_SUBCASE("unrepresentable length") {
        wire.assign(9, static_cast<char>(255));
        expected = asio::error::no_data;
    }
    DOCTEST_SUBCASE("frame exceeds the configured receive limit") {
        wire.assign(129, 'x');
        wire.front() = static_cast<char>(128);
        expected = asio::error::not_found;
    }
    std::atomic<unsigned> delivered{};
    std::promise<arknet::error_code> disconnected;
    auto result = disconnected.get_future();
    arknet::tcp_server server(16, 128, 1);
    server.bind_recv([&](auto&, std::string_view) { ++delivered; });
    server.bind_disconnect([&](auto&) { disconnected.set_value(arknet::get_last_error()); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "invalid-frame server start");
    asio::io_context context;
    asio::ip::tcp::socket peer(context);
    peer.connect({asio::ip::make_address("127.0.0.1"), server.get_listen_port()});
    asio::write(peer, asio::buffer(wire));
    check(await(result) == expected, "bad framing closes the session with the expected error");
    check(delivered == 0, "invalid or incomplete payload never reaches the receive callback");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("Stopped TCP clients release ownership after their external context stops") {
    asio::io_context context;
    auto work = asio::make_work_guard(context);
    std::jthread worker([&] { context.run(); });
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view payload) { session->async_send(payload); });
    check(server.start("127.0.0.1", 0), "external client test server");
    auto client = std::make_shared<arknet::tcp_client>(context);
    std::weak_ptr<arknet::tcp_client> weak = client;
    std::promise<void> callback;
    auto received = callback.get_future();
    client->set_auto_reconnect(false);
    client->bind_recv([&](std::string_view) {
        client->stop();
        callback.set_value();
    });
    check(client->start("127.0.0.1", server.get_listen_port()), "external client connect");
    client->async_send(std::string("stop"));
    await(received);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!client->is_stopped() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    check(client->is_stopped(), "callback stop completed");
    std::promise<void> barrier;
    auto drained = barrier.get_future();
    asio::post(context, [&] { barrier.set_value(); });
    await(drained);
    work.reset();
    worker.join();
    context.stop();
    client->stop();
    client.reset();
    check(weak.expired(), "stopped context must not retain a repeated stop task");
    server.stop();
}

DOCTEST_TEST_CASE("Stopped TCP servers release ownership after their external context stops") {
    asio::io_context context;
    auto work = asio::make_work_guard(context);
    std::jthread worker([&] { context.run(); });
    auto server = std::make_shared<arknet::tcp_server>(context);
    std::weak_ptr<arknet::tcp_server> weak = server;
    std::promise<void> stopped;
    auto result = stopped.get_future();
    server->bind_stop([&] { stopped.set_value(); });
    check(server->start("127.0.0.1", 0), "external server start");
    server->post([&] { server->stop(); });
    await(result);
    std::promise<void> barrier;
    auto drained = barrier.get_future();
    asio::post(context, [&] { barrier.set_value(); });
    await(drained);
    check(server->is_stopped(), "external server stopped");
    // Drain cancellation completions before stopping the external context.
    work.reset();
    worker.join();
    context.stop();
    server->stop();
    server.reset();
    check(weak.expired(), "stopped listener must not retain a repeated stop task");
}
