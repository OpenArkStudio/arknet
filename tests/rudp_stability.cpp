// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <arknet/base/impl/alive_time_cp.hpp>
#include <arknet/rudp/rudp_server.hpp>
#include "check.hpp"

namespace {

struct clock_probe : arknet::detail::alive_time_cp<clock_probe, void>
{
    void move_wall_clock(std::chrono::system_clock::duration change) {
        last_alive_time_ += change;
    }
};

template<class Server>
struct polled_server_t
{
    asio::io_context context;
    Server server{context};

    ~polled_server_t() {
        if (context.stopped())
        {
            context.restart();
        }
        asio::post(context, [this] { server.stop(); });
        context.run_for(200ms);
    }

    void start() {
        asio::post(context, [this] { server.start("127.0.0.1", 0); });
        until([&] { return server.is_started(); });
        check(server.is_started(), "server starts on caller's IO context");
    }

    template<class Predicate>
    void until(Predicate done) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done())
            {
                return;
            }
            if (context.stopped())
            {
                context.restart();
            }
            context.run_one_for(5ms);
        }
        check(false, "expected IO event completes before the test deadline");
    }
};

using polled_server = polled_server_t<arknet::rudp_server>;

class raw_peer
{
public:
    explicit raw_peer(asio::ip::udp::endpoint destination)
        : socket_(context_, asio::ip::udp::v4()) {
        socket_.connect(destination);
        socket_.non_blocking(true);
    }

    asio::ip::udp::endpoint endpoint() const {
        return socket_.local_endpoint();
    }

    void send(std::string_view bytes) {
        socket_.send(asio::buffer(bytes));
    }

    std::optional<std::uint32_t> acknowledged(std::uint32_t sync) {
        std::optional<std::uint32_t> conversation;
        std::array<char, 65536> bytes{};
        arknet::error_code error;
        for (;;)
        {
            const auto count = socket_.receive(asio::buffer(bytes), 0, error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                return conversation;
            }
            check(!error, "raw UDP peer receives without a transport error");
            const std::string_view packet(bytes.data(), count);
            if (arknet::detail::rudp_is_control(packet, 2) && arknet::detail::rudp_read_id(packet.data() + 1) == sync)
            {
                conversation = arknet::detail::rudp_read_id(packet.data() + 5);
            }
        }
    }

private:
    asio::io_context context_;
    asio::ip::udp::socket socket_;
};

struct observed_rudp_server : arknet::rudp_server_impl_t<observed_rudp_server>
{
    using base = arknet::rudp_server_impl_t<observed_rudp_server>;
    using base::base;

    std::size_t received_datagrams{};

    template<class Condition>
    void _handle_recv(const arknet::error_code& error, std::size_t count,
        std::shared_ptr<observed_rudp_server> lifetime, std::shared_ptr<arknet::detail::ecs_t<Condition>> condition) {
        received_datagrams += !error;
        base::_handle_recv(error, count, std::move(lifetime), std::move(condition));
    }
};

template<class Server>
std::uint32_t wait_for_ack(polled_server_t<Server>& runtime, raw_peer& peer, std::uint32_t sync) {
    std::optional<std::uint32_t> conversation;
    runtime.until([&] {
        conversation = peer.acknowledged(sync);
        return conversation.has_value();
    });
    return *conversation;
}

std::string arq_message(std::uint32_t conversation) {
    arknet::arq::control_block core(conversation);
    std::string packet;
    core.set_output([&](std::string_view bytes) { packet.append(bytes); });
    check(core.send("validated") == 0, "ARQ admits test message");
    core.update(0);
    core.flush();
    check(!packet.empty(), "ARQ emits a data packet");
    return packet;
}
} // namespace

DOCTEST_TEST_CASE("Silence duration is independent of wall clock changes") {
    clock_probe probe;
    static_assert(std::is_same_v<decltype(probe.last_alive_time()), std::chrono::system_clock::time_point>);
    probe.update_alive_time();
    probe.move_wall_clock(-1h);
    DOCTEST_CHECK(probe.get_silence_duration() >= 0ns);
    DOCTEST_CHECK(probe.get_silence_duration() < 1s);
    probe.move_wall_clock(2h);
    DOCTEST_CHECK(probe.get_silence_duration() >= 0ns);
    DOCTEST_CHECK(probe.get_silence_duration() < 1s);
}

DOCTEST_TEST_CASE("RUDP startup resumes queued work after its caller stops the context") {
    bool predecessor_completed = false;
    polled_server runtime;
    asio::post(runtime.context, [&] {
        // The host idles its context before the queued server.start() executes.
        runtime.context.stop();
        predecessor_completed = true;
    });
    runtime.start();
    DOCTEST_CHECK(predecessor_completed);
    raw_peer peer(runtime.server.acceptor().local_endpoint());
    peer.send(arknet::detail::rudp_control(1, 123));
    const auto conversation = wait_for_ack(runtime, peer, 123);
    const auto session = runtime.server.find_session(peer.endpoint());
    check(session, "resumed external context admits a peer");
    DOCTEST_CHECK(session->conversation() == conversation);
}

DOCTEST_TEST_CASE("RUDP admission is bounded and freed sessions admit new peers") {
    unsigned handshakes{};
    polled_server_t<observed_rudp_server> runtime;
    runtime.server.bind_handshake([&](auto&, arknet::error_code error) { handshakes += !error; });
    DOCTEST_CHECK(runtime.server.get_max_sessions() == 1024);
    DOCTEST_CHECK_THROWS_AS(runtime.server.set_max_sessions(0), std::invalid_argument);
    runtime.server.set_max_sessions(2);
    runtime.start();
    DOCTEST_CHECK_THROWS_AS(runtime.server.set_max_sessions(3), std::logic_error);
    DOCTEST_CHECK(runtime.server.get_max_sessions() == 2);
    std::vector<std::unique_ptr<raw_peer>> peers;
    for (unsigned i = 0; i < 8; ++i)
    {
        peers.push_back(std::make_unique<raw_peer>(runtime.server.acceptor().local_endpoint()));
    }
    // Queue both handshakes before polling, then observe their ACKs rather than
    // assuming a fixed run_for budget processed every receive and send.
    peers[0]->send(arknet::detail::rudp_control(1, 1));
    peers[1]->send(arknet::detail::rudp_control(1, 2));
    const auto first = wait_for_ack(runtime, *peers[0], 1);
    const auto second = wait_for_ack(runtime, *peers[1], 2);
    DOCTEST_CHECK(runtime.server.get_session_count() == 2);
    DOCTEST_CHECK(handshakes == 2);
    DOCTEST_CHECK(first != second);
    const auto first_session = runtime.server.find_session(peers[0]->endpoint());
    const auto second_session = runtime.server.find_session(peers[1]->endpoint());
    check(first_session && second_session, "both peer endpoints have admitted sessions");
    DOCTEST_CHECK(first_session->conversation() == first);
    DOCTEST_CHECK(second_session->conversation() == second);
    for (unsigned i = 2; i < peers.size(); ++i)
    {
        peers[i]->send(arknet::detail::rudp_control(1, i + 1));
    }
    // A missing ACK proves rejection only after every excess SYN was handled.
    runtime.until([&] { return runtime.server.received_datagrams == peers.size(); });
    DOCTEST_CHECK(runtime.server.get_session_count() == 2);
    DOCTEST_CHECK(handshakes == 2);
    for (unsigned i = 2; i < peers.size(); ++i)
    {
        DOCTEST_CHECK_FALSE(peers[i]->acknowledged(i + 1).has_value());
    }
    peers[1]->send(arknet::detail::rudp_control(1, 2));
    DOCTEST_CHECK(wait_for_ack(runtime, *peers[1], 2) == second);
    DOCTEST_CHECK(handshakes == 2);
    peers[0]->send(arknet::detail::rudp_control(3, 1, first));
    runtime.until([&] { return runtime.server.get_session_count() == 1; });
    DOCTEST_CHECK(runtime.server.get_session_count() == 1);
    peers[2]->send(arknet::detail::rudp_control(1, 3));
    const auto replacement = wait_for_ack(runtime, *peers[2], 3);
    DOCTEST_CHECK(replacement != first);
    DOCTEST_CHECK(replacement != second);
    DOCTEST_CHECK(runtime.server.get_session_count() == 2);
    DOCTEST_CHECK(handshakes == 3);
    asio::post(runtime.context, [&] { runtime.server.stop(); });
    runtime.until([&] { return runtime.server.is_stopped(); });
    check(runtime.server.is_stopped(), "bounded server stops and drains all sessions");
    runtime.server.set_max_sessions(3);
    DOCTEST_CHECK(runtime.server.get_max_sessions() == 3);
}

DOCTEST_TEST_CASE("RUDP invalid datagrams cannot refresh session liveness") {
    std::vector<std::string> delivered;
    polled_server_t<observed_rudp_server> runtime;
    runtime.server.bind_recv([&](auto&, std::string_view message) { delivered.emplace_back(message); });
    runtime.start();
    raw_peer peer(runtime.server.acceptor().local_endpoint());
    peer.send(arknet::detail::rudp_control(1, 123));
    const auto conversation = wait_for_ack(runtime, peer, 123);
    const auto session = runtime.server.find_session(peer.endpoint());
    check(session, "initial SYN creates a session");
    const auto alive = session->last_alive_time();
    auto bad_length = arq_message(conversation);
    arknet::detail::rudp_put_id(bad_length.data() + 20, 1000);
    auto bad_command = arq_message(conversation);
    bad_command[4] = '\0';
    const std::vector<std::string> invalid{"junk", arq_message(conversation + 1), bad_length, bad_command,
        arknet::detail::rudp_control(1, 122), arknet::detail::rudp_control(3, 123, conversation + 1),
        arknet::detail::rudp_control(3, 122, conversation)};
    for (const auto& packet : invalid)
    {
        const auto received = runtime.server.received_datagrams;
        peer.send(packet);
        runtime.until([&] { return runtime.server.received_datagrams == received + 1; });
        DOCTEST_CHECK(session->last_alive_time() == alive);
        DOCTEST_CHECK(delivered.empty());
    }
    peer.send(arq_message(conversation));
    runtime.until([&] { return !delivered.empty(); });
    DOCTEST_CHECK(delivered == std::vector<std::string>{"validated"});
    DOCTEST_CHECK(session->last_alive_time() > alive);
    const auto accepted = session->last_alive_time();
    const auto received = runtime.server.received_datagrams;
    peer.send(arknet::detail::rudp_control(1, 123));
    // A duplicate initial ACK cannot prove that this retry was processed.
    runtime.until([&] { return runtime.server.received_datagrams == received + 1; });
    DOCTEST_CHECK(wait_for_ack(runtime, peer, 123) == conversation);
    DOCTEST_CHECK(session->last_alive_time() > accepted);
}

DOCTEST_TEST_CASE("RUDP invalid traffic cannot postpone the silence timeout") {
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        DOCTEST_INFO(mode);
        unsigned disconnected{};
        arknet::rudp_session* original{};
        arknet::error_code disconnect_error;
        // Runtime teardown may disconnect a replacement admitted by a queued
        // SYN; its callback state must remain alive until that teardown finishes.
        polled_server runtime;
        runtime.server.bind_connect([](auto& session) { session->set_silence_timeout(60ms); });
        runtime.server.bind_disconnect([&](auto& session) {
            if (session.get() == original)
            {
                ++disconnected;
                disconnect_error = arknet::get_last_error();
            }
        });
        runtime.start();
        raw_peer peer(runtime.server.acceptor().local_endpoint());
        peer.send(arknet::detail::rudp_control(1, 123));
        const auto conversation = wait_for_ack(runtime, peer, 123);
        const auto session = runtime.server.find_session(peer.endpoint());
        check(session, "timeout test retains its original session");
        original = session.get();
        std::string invalid;
        switch (mode)
        {
        case 0:
            invalid = "junk";
            break;
        case 1:
            invalid = arq_message(conversation + 1);
            break;
        case 2:
            invalid = arknet::detail::rudp_control(1, 122);
            break;
        default:
            invalid = arknet::detail::rudp_control(3, 123, conversation + 1);
            break;
        }
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!session->is_stopped() && std::chrono::steady_clock::now() < deadline)
        {
            peer.send(invalid);
            runtime.context.run_one_for(5ms);
        }
        // A queued SYN may legitimately admit a new session after the original expires.
        DOCTEST_CHECK(session->is_stopped());
        DOCTEST_CHECK(disconnected == 1);
        DOCTEST_CHECK(disconnect_error == asio::error::timed_out);
    }
}

DOCTEST_TEST_CASE("RUDP matching handshake retries refresh the silence clock") {
    polled_server runtime;
    runtime.server.bind_connect([](auto& session) { session->set_silence_timeout(1s); });
    runtime.start();
    raw_peer peer(runtime.server.acceptor().local_endpoint());
    peer.send(arknet::detail::rudp_control(1, 123));
    wait_for_ack(runtime, peer, 123);
    const auto session = runtime.server.find_session(peer.endpoint());
    check(session, "valid retry test retains its original session");
    const auto alive = session->last_alive_time();
    peer.send(arknet::detail::rudp_control(1, 123));
    runtime.until([&] { return session->last_alive_time() > alive; });
    DOCTEST_CHECK(session->is_started());
    DOCTEST_CHECK(runtime.server.find_session(peer.endpoint()) == session);
    runtime.until([&] { return peer.acknowledged(123).has_value(); });
    runtime.until([&] { return runtime.server.get_session_count() == 0; });
    DOCTEST_CHECK(session->is_stopped());
}

DOCTEST_TEST_CASE("Raw UDP retains liveness updates for arbitrary datagrams") {
    unsigned received{};
    polled_server_t<arknet::udp_server> runtime;
    runtime.server.bind_connect([](auto& session) { session->set_silence_timeout(1s); });
    runtime.server.bind_recv([&](auto&, std::string_view) { ++received; });
    runtime.start();
    raw_peer peer(runtime.server.acceptor().local_endpoint());
    peer.send("junk");
    runtime.until([&] { return received == 1; });
    const auto session = runtime.server.find_session(peer.endpoint());
    check(session, "raw UDP accepts an arbitrary first datagram");
    const auto alive = session->last_alive_time();
    peer.send("");
    runtime.until([&] { return received == 2; });
    DOCTEST_CHECK(session->is_started());
    DOCTEST_CHECK(session->last_alive_time() > alive);
    DOCTEST_CHECK(runtime.server.find_session(peer.endpoint()) == session);
    runtime.until([&] { return session->is_stopped(); });
}
