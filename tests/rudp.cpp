// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <arknet/rudp/rudp_client.hpp>
#include <arknet/rudp/rudp_server.hpp>
#include "../benchmarks/runtime.hpp"
#include "check.hpp"

namespace {
class lossy_relay
{
public:
    explicit lossy_relay(asio::ip::udp::endpoint server)
        : work_(asio::make_work_guard(context_))
        , socket_(context_, {asio::ip::make_address("127.0.0.1"), 0})
        , server_(server) {
        receive();
        thread_ = std::thread([this] { context_.run(); });
    }
    ~lossy_relay() {
        context_.stop();
        thread_.join();
    }
    unsigned short port() const {
        return socket_.local_endpoint().port();
    }
    std::atomic<unsigned> dropped{}, duplicated{}, reordered{};

private:
    void send(std::shared_ptr<std::string> bytes, asio::ip::udp::endpoint target) {
        socket_.async_send_to(asio::buffer(*bytes), target, [bytes](arknet::error_code, std::size_t) {});
    }
    void receive() {
        socket_.async_receive_from(asio::buffer(buffer_), sender_, [this](arknet::error_code error, std::size_t count) {
            if (error)
            {
                return;
            }
            if (sender_ != server_)
            {
                client_ = sender_;
            }
            const auto target = sender_ == server_ ? client_ : server_;
            auto bytes = std::make_shared<std::string>(buffer_.data(), count);
            const auto index = ++packets_;
            if (index % 7 == 1)
            {
                ++dropped;
            }
            else if (index % 4 == 0)
            {
                ++reordered;
                auto timer = std::make_shared<asio::steady_timer>(context_, 25ms);
                timer->async_wait([this, bytes, target, timer](arknet::error_code error) {
                    if (!error)
                    {
                        send(bytes, target);
                    }
                });
            }
            else
            {
                send(bytes, target);
                if (index % 5 == 0)
                {
                    ++duplicated;
                    send(bytes, target);
                }
            }
            receive();
        });
    }
    asio::io_context context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint server_, client_, sender_;
    std::array<char, 65536> buffer_{};
    unsigned packets_{};
    std::thread thread_;
};
} // namespace

DOCTEST_TEST_CASE("RUDP reassembles empty binary and fragmented messages on IPv4 and IPv6") {
    for (const auto* host : {"127.0.0.1", "::1"})
    {
        DOCTEST_INFO(host);
        messages received;
        std::atomic<unsigned> connects{}, handshakes{}, client_handshakes{}, disconnects{};
        arknet::rudp_server server;
        server.bind_handshake([&](auto&, arknet::error_code error) {
            if (!error)
            {
                ++handshakes;
            }
        });
        server.bind_connect([&](auto& session) {
            session->set_user_data(std::uint32_t(42));
            ++connects;
        });
        server.bind_disconnect([&](auto&) { ++disconnects; });
        server.bind_recv([&](auto& session, std::string_view bytes) {
            check(session->template get_user_data<std::uint32_t>() == 42, "session data survives fragmentation");
            session->async_send(bytes);
        });
        arknet::rudp_client client;
        client.set_auto_reconnect(false);
        client.bind_handshake([&](arknet::error_code error) {
            check(!error, "client handshake callback receives success");
            ++client_handshakes;
        });
        client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
        const std::vector<std::string> expected{
            "", std::string("a\0b", 3), std::string(4119, 'r'), std::string(16384, 'k')};
        for (unsigned round = 0; round < 2; ++round)
        {
            check(server.start(host, 0), "RUDP server start/restart");
            check(client.start(host, server.get_listen_port()), "RUDP start completes handshake");
            for (unsigned i = 0; i < expected.size(); ++i)
            {
                auto sent = client.async_send(expected[i], asio::use_future);
                const auto [error, count] = await(sent);
                check(!error && count == expected[i].size(), "RUDP send accepts a complete message");
                const auto index = round * expected.size() + i;
                check(received.wait(index + 1)[index] == expected[i], "RUDP delivers exact message boundaries");
            }
            client.stop();
            server.stop();
            check(server.get_session_count() == 0, "RUDP stop clears sessions");
        }
        check(connects == 2 && handshakes == 2 && client_handshakes == 2 && disconnects == 2,
            "one connect/handshake/disconnect per session on both endpoints");
    }
}

DOCTEST_TEST_CASE("RUDP handshake and data recover from real UDP drop duplicate and reorder") {
    messages received;
    std::atomic<unsigned> connects{};
    arknet::rudp_server server;
    server.bind_connect([&](auto&) { ++connects; });
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    check(server.start("127.0.0.1", 0), "loss test server starts");
    lossy_relay relay(server.acceptor().local_endpoint());
    arknet::rudp_client client;
    client.set_auto_reconnect(false).set_connect_timeout(2s);
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    check(client.start("127.0.0.1", relay.port()), "handshake retries a dropped SYN");
    std::vector<std::string> expected;
    for (unsigned i = 0; i < 24; ++i)
    {
        auto value = std::to_string(i) + ':';
        value.resize(i % 2 ? 4119 : 16384, static_cast<char>(i));
        expected.push_back(value);
        check(client.async_send(value), "reliable message admitted");
    }
    check(received.wait(expected.size()) == expected, "loss and reorder preserve exact ordered messages");
    check(connects == 1, "repeated handshake does not create duplicate sessions");
    check(relay.dropped > 0 && relay.duplicated > 0 && relay.reordered > 0, "all impairment paths executed");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP rejects non-handshake datagrams and ignores wrong conversation close") {
    arknet::rudp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0), "server starts");
    asio::io_context context;
    asio::ip::udp::socket peer(context, asio::ip::udp::v4());
    peer.connect(server.acceptor().local_endpoint());
    peer.send(asio::buffer("invalid", 7));
    const auto syn = arknet::detail::rudp_control(1, 123);
    peer.send(asio::buffer(syn));
    std::array<char, 65536> storage{};
    const auto size = peer.receive(asio::buffer(storage));
    const std::string_view ack(storage.data(), size);
    check(arknet::detail::rudp_is_control(ack, 2), "valid SYN receives ACK");
    const auto conversation = arknet::detail::rudp_read_id(ack.data() + 5);
    const auto initial_repeat = peer.receive(asio::buffer(storage));
    check(arknet::detail::rudp_is_control({storage.data(), initial_repeat}, 2) &&
              arknet::detail::rudp_read_id(storage.data() + 5) == conversation,
        "initial handshake retransmission retains its conversation");
    check(server.get_session_count() == 1, "invalid initial data never allocates a session");
    peer.send(asio::buffer(arknet::detail::rudp_control(3, 123, conversation + 1)));
    peer.send(asio::buffer(arknet::detail::rudp_control(1, 122)));
    peer.send(asio::buffer(syn));
    const auto repeated = peer.receive(asio::buffer(storage));
    check(arknet::detail::rudp_is_control({storage.data(), repeated}, 2) &&
              arknet::detail::rudp_read_id(storage.data() + 1) == 123 &&
              arknet::detail::rudp_read_id(storage.data() + 5) == conversation,
        "stale SYN and wrong-conversation FIN preserve the active conversation");
    check(server.get_session_count() == 1, "stale controls do not replace an active session");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP handshake times out once and can be stopped while pending") {
    asio::io_context context;
    asio::ip::udp::socket silent(context, {asio::ip::make_address("127.0.0.1"), 0});
    arknet::rudp_client client;
    client.set_auto_reconnect(false).set_connect_timeout(100ms);
    std::atomic<unsigned> handshakes{}, connects{};
    client.bind_handshake([&](arknet::error_code) { ++handshakes; });
    client.bind_connect([&] { ++connects; });
    check(!client.start("127.0.0.1", silent.local_endpoint().port()), "unresponsive handshake fails");
    check(arknet::get_last_error() == asio::error::timed_out, "handshake deadline reported");
    client.stop();
    check(handshakes == 1 && connects == 1, "timeout has one handshake and connect completion");
    client.set_connect_timeout(5s);
    client.async_start("127.0.0.1", silent.local_endpoint().port());
    std::promise<void> entered;
    auto ready = entered.get_future();
    client.post([&] { entered.set_value(); });
    await(ready);
    const auto before = std::chrono::steady_clock::now();
    client.stop();
    check(std::chrono::steady_clock::now() - before < 1s, "stop cancels handshake without waiting its deadline");
    check(client.is_stopped(), "stopped handshake drains");
}

DOCTEST_TEST_CASE("RUDP handshake rejection drains retained sessions without a connect notification") {
    asio::io_context context;
    arknet::rudp_server server(context);
    std::shared_ptr<arknet::rudp_session> retained;
    unsigned connects{};
    server.bind_handshake([&](auto& session, arknet::error_code) {
        retained = session;
        session->stop();
    });
    server.bind_connect([&](auto&) { ++connects; });
    asio::post(context, [&] { server.start("127.0.0.1", 0); });
    context.run_for(20ms);
    check(server.is_started(), "external-context rejection server starts");
    asio::io_context peer_context;
    asio::ip::udp::socket peer(peer_context, asio::ip::udp::v4());
    peer.connect({asio::ip::make_address("127.0.0.1"), server.get_listen_port()});
    const auto syn = arknet::detail::rudp_control(1, 123);
    peer.send(asio::buffer(syn));
    context.run_for(20ms);
    DOCTEST_CHECK(retained);
    DOCTEST_CHECK((retained && retained->is_stopped()));
    DOCTEST_CHECK(connects == 0);
    DOCTEST_CHECK(server.get_session_count() == 0);
    peer.non_blocking(true);
    std::array<char, 64> storage{};
    arknet::error_code error;
    const auto acknowledged = peer.receive(asio::buffer(storage), 0, error);
    const std::string_view ack(storage.data(), acknowledged);
    const bool valid_ack = !error && arknet::detail::rudp_is_control(ack, 2);
    DOCTEST_CHECK(valid_ack);
    const auto conversation = valid_ack ? arknet::detail::rudp_read_id(ack.data() + 5) : 0;
    const auto finished = peer.receive(asio::buffer(storage), 0, error);
    const std::string_view fin(storage.data(), finished);
    DOCTEST_CHECK(
        (!error && arknet::detail::rudp_is_control(fin, 3) && arknet::detail::rudp_read_id(fin.data() + 1) == 123 &&
            arknet::detail::rudp_read_id(fin.data() + 5) == conversation));
    asio::post(context, [&] { server.stop(); });
    context.run_for(100ms);
    const bool drained = context.stopped();
    if (!drained)
    {
        asio::post(context, [&] { retained.reset(); });
        context.run_for(100ms);
    }
    check(drained, "stopped retained sessions do not keep the IO context running");
}

DOCTEST_TEST_CASE("RUDP receive memory limits disconnect once with no buffer space") {
    arknet::rudp_server server;
    arknet::arq::options limits;
    limits.max_pending_bytes = 128;
    server.set_rudp_options(limits);
    std::atomic<unsigned> calls{};
    std::promise<arknet::error_code> disconnected;
    auto completion = disconnected.get_future();
    server.bind_disconnect([&](auto&) {
        if (++calls == 1)
        {
            disconnected.set_value(arknet::get_last_error());
        }
    });
    check(server.start("127.0.0.1", 0), "receive-budget server starts");
    arknet::rudp_client client;
    client.set_auto_reconnect(false);
    check(client.start("127.0.0.1", server.get_listen_port()), "receive-budget client handshake");
    auto sent = client.async_send(std::string(129, 'q'), asio::use_future);
    check(!await(sent).first, "sender admits the message within its own budget");
    check(await(completion) == asio::error::no_buffer_space, "receiver memory exhaustion is observable");
    client.stop();
    server.stop();
    check(calls == 1, "receive-budget failure notifies one disconnect");
}

DOCTEST_TEST_CASE("RUDP sessions work with one io_context and multiple threads") {
    for (const auto* model : {"shared", "sharded"})
    {
        benchmark_runtime runtime(model, 4);
        arknet::rudp_server server(runtime.server_lanes(4));
        server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
        check(server.start("127.0.0.1", 0), "external-context server starts");
        std::vector<std::unique_ptr<arknet::rudp_client>> clients;
        std::array<messages, 4> received;
        for (unsigned index = 0; index < 4; ++index)
        {
            auto client = std::make_unique<arknet::rudp_client>(runtime.context_at(index + 1));
            client->set_auto_reconnect(false);
            client->bind_recv([&, index](std::string_view data) { received[index].push(data); });
            check(client->start("127.0.0.1", server.get_listen_port()), "external-context client handshake");
            clients.push_back(std::move(client));
        }
        std::vector<std::string> expected;
        for (unsigned i = 0; i < 32; ++i)
        {
            std::string payload = std::to_string(i) + ':';
            payload.resize(4119, static_cast<char>(i));
            expected.push_back(payload);
            for (const auto& client : clients)
            {
                check(client->async_send(payload), "concurrent-client send admission");
            }
        }
        for (auto& data : received)
        {
            check(data.wait(expected.size()) == expected, "strands preserve per-session message order");
        }
        for (auto& client : clients)
        {
            client->stop();
        }
        server.stop();
    }
}

DOCTEST_TEST_CASE("RUDP automatic reconnect replaces conversations after server restart") {
    arknet::rudp_server server;
    messages echoed, connections, disconnects;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0), "reconnect server starts");
    const auto port = server.get_listen_port();
    arknet::rudp_client client;
    client.set_auto_reconnect(true, 50ms).set_connect_timeout(200ms);
    client.bind_connect([&] {
        if (!arknet::get_last_error())
        {
            connections.push("connected");
        }
    });
    client.bind_disconnect([&] { disconnects.push("disconnected"); });
    client.bind_recv([&](std::string_view data) { echoed.push(data); });
    check(client.start("127.0.0.1", port), "initial RUDP connection");
    client.async_send("before restart");
    check(echoed.wait(1).front() == "before restart", "initial conversation echoes");
    server.stop();
    disconnects.wait(1);
    check(server.start("127.0.0.1", port), "server restarts on original port");
    connections.wait(2);
    client.async_send("after restart");
    check(echoed.wait(2).back() == "after restart", "new conversation echoes");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP rejects oversized and full-queue messages with one send completion") {
    arknet::rudp_server server;
    check(server.start("127.0.0.1", 0), "limit server starts");
    arknet::rudp_client client;
    client.set_auto_reconnect(false);
    arknet::arq::options limits;
    limits.max_pending_bytes = 128;
    client.set_rudp_options(limits);
    check(client.start("127.0.0.1", server.get_listen_port()), "limit client handshake");
    auto no_space = client.async_send(std::string(129, 'q'), asio::use_future);
    const auto [space_error, space_count] = await(no_space);
    check(space_error == asio::error::no_buffer_space && space_count == 0, "RUDP core queue bound applies");
    auto too_large = client.async_send(std::string(1200 * 257, 'q'), asio::use_future);
    const auto [size_error, size_count] = await(too_large);
    check(size_error == asio::error::message_size && size_count == 0, "RUDP fragment limit applies");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP unacknowledged messages exhaust retries and disconnect") {
    benchmark_runtime runtime("shared", 1);
    asio::ip::udp::socket peer(runtime.context_at(), {asio::ip::make_address("127.0.0.1"), 0});
    std::array<char, 65536> storage{};
    asio::ip::udp::endpoint sender;
    std::function<void()> receive;
    receive = [&] {
        peer.async_receive_from(asio::buffer(storage), sender, [&](arknet::error_code error, std::size_t count) {
            if (error)
            {
                return;
            }
            const std::string_view bytes(storage.data(), count);
            if (arknet::detail::rudp_is_control(bytes, 1))
            {
                auto ack = std::make_shared<std::string>(
                    arknet::detail::rudp_control(2, arknet::detail::rudp_read_id(bytes.data() + 1), 42));
                peer.async_send_to(asio::buffer(*ack), sender, [ack](arknet::error_code, std::size_t) {});
            }
            receive();
        });
    };
    receive();
    arknet::rudp_client client;
    arknet::arq::options settings;
    settings.max_retransmits = 1;
    client.set_rudp_options(settings).set_auto_reconnect(false);
    std::promise<arknet::error_code> disconnected;
    auto completed = disconnected.get_future();
    client.bind_disconnect([&] { disconnected.set_value(arknet::get_last_error()); });
    check(client.start("127.0.0.1", peer.local_endpoint().port()), "black-hole peer completes handshake");
    client.async_send("never acknowledged");
    check(await(completed) == asio::error::timed_out, "retry exhaustion is observable");
    client.stop();
    std::promise<void> closed;
    auto stopped = closed.get_future();
    asio::post(runtime.context_at(), [&] {
        peer.close();
        closed.set_value();
    });
    await(stopped);
}
