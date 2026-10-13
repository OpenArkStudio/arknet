// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <arknet/rudp/rudp_client.hpp>
#include <arknet/rudp/rudp_server.hpp>
#include "../benchmarks/runtime.hpp"
#include "check.hpp"

namespace
{
class fec_relay
{
public:
    explicit fec_relay(asio::ip::udp::endpoint server)
        : work_(asio::make_work_guard(context_)), socket_(context_, {asio::ip::make_address("127.0.0.1"), 0}),
          server_(server)
    {
        socket_.set_option(asio::socket_base::receive_buffer_size(1024 * 1024));
        receive();
        thread_ = std::thread([this] { context_.run(); });
    }
    ~fec_relay()
    {
        context_.stop();
        thread_.join();
    }
    unsigned short port() const { return socket_.local_endpoint().port(); }
    int receive_buffer_size() const
    {
        asio::socket_base::receive_buffer_size size;
        socket_.get_option(size);
        return size.value();
    }
    std::atomic<unsigned> dropped{}, duplicated{}, reordered{};
    std::atomic<std::size_t> largest{};

private:
    void send(std::shared_ptr<std::string> bytes, asio::ip::udp::endpoint target)
    {
        socket_.async_send_to(asio::buffer(*bytes), target, [bytes](arknet::error_code, std::size_t) {});
    }
    void receive()
    {
        socket_.async_receive_from(asio::buffer(buffer_), sender_,
                                   [this](arknet::error_code error, std::size_t count)
                                   {
                                       if (error)
                                           return;
                                       if (sender_ != server_)
                                           client_ = sender_;
                                       const auto target = sender_ == server_ ? client_ : server_;
                                       auto bytes = std::make_shared<std::string>(buffer_.data(), count);
                                       largest.store(std::max(largest.load(), count));
                                       const auto framed = count >= arknet::fec::transport::header_size &&
                                                           bytes->compare(0, 4, "AFEC") == 0;
                                       const auto kind = framed ? static_cast<unsigned char>((*bytes)[5]) : 255;
                                       const auto data_count = framed ? static_cast<unsigned char>((*bytes)[6]) : 0;
                                       const auto index = framed ? static_cast<unsigned char>((*bytes)[8]) : 0;
                                       if (kind == 0 && data_count > 1 && index == 1)
                                           ++dropped;
                                       else if (kind == 1)
                                       {
                                           ++reordered;
                                           auto timer = std::make_shared<asio::steady_timer>(context_, 5ms);
                                           timer->async_wait(
                                               [this, bytes, target, timer](arknet::error_code error)
                                               {
                                                   if (!error)
                                                   {
                                                       send(bytes, target);
                                                       send(bytes, target);
                                                       ++duplicated;
                                                   }
                                               });
                                       }
                                       else
                                           send(bytes, target);
                                       receive();
                                   });
    }
    asio::io_context context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint server_, client_, sender_;
    std::array<char, 65536> buffer_{};
    std::thread thread_;
};
}

DOCTEST_TEST_CASE("RUDP negotiates fixed and adaptive FEC on IPv4 and IPv6")
{
    for (const auto* host : {"127.0.0.1", "::1"})
        for (const auto mode : {arknet::fec::policy::fixed, arknet::fec::policy::adaptive})
        {
            DOCTEST_INFO(host);
            DOCTEST_INFO(static_cast<int>(mode));
            auto fec = arknet::fec::options::fixed();
            fec.mode = mode;
            arknet::arq::options server_options, client_options;
            server_options.mtu = 1000;
            client_options.mtu = 1200;
            arknet::rudp_server server;
            server.set_rudp_options(server_options).set_fec_options(fec);
            server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
            std::shared_ptr<arknet::rudp_session> accepted;
            std::promise<void> connected;
            auto connection = connected.get_future();
            server.bind_connect(
                [&](auto& session)
                {
                    accepted = session;
                    connected.set_value();
                });
            messages received;
            arknet::rudp_client client;
            client.set_rudp_options(client_options).set_fec_options(fec).set_auto_reconnect(false);
            client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
            check(server.start(host, 0), "FEC server starts");
            check(client.start(host, server.get_listen_port()), "FEC negotiation completes");
            await(connection);
            check(accepted->get_rudp_options().mtu == 1000, "negotiation selects the smaller wire MTU");
            check(accepted->get_fec_options().mode == mode, "session exposes negotiated FEC policy");
            const std::vector<std::string> expected{"", std::string("a\0b", 3), std::string(16384, 'f')};
            for (const auto& bytes : expected)
                check(client.async_send(bytes), "FEC accepts complete binary messages");
            check(received.wait(expected.size()) == expected, "FEC preserves message contents and order");
            auto snapshot = client.get_rudp_statistics();
            const auto stats = await(snapshot);
            check(stats.fec.data_packets_tx > 0 && stats.fec.wire_bytes_rx > 0, "negotiated path uses FEC framing");
            auto server_snapshot = accepted->get_rudp_statistics();
            check(await(server_snapshot).fec.wire_bytes_tx > 0, "session statistics use serialized snapshot");
            DOCTEST_CHECK_THROWS_AS(client.set_fec_options({}), std::logic_error);
            DOCTEST_CHECK_THROWS_AS(server.set_fec_options({}), std::logic_error);
            client.stop();
            server.stop();
            check(server.get_session_count() == 0, "FEC shutdown removes all sessions");
            DOCTEST_CHECK_THROWS_AS(accepted->get_rudp_statistics(), std::logic_error);
        }
}

DOCTEST_TEST_CASE("FEC-enabled server accepts legacy ARQ without changing its wire format")
{
    arknet::rudp_server server;
    server.set_fec_options(arknet::fec::options::fixed());
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    messages received;
    arknet::rudp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    check(server.start("127.0.0.1", 0), "FEC-capable server starts");
    check(client.start("127.0.0.1", server.get_listen_port()), "legacy handshake succeeds");
    check(client.async_send("legacy ARQ"), "legacy message admitted");
    check(received.wait(1).front() == "legacy ARQ", "legacy session echoes");
    auto snapshot = client.get_rudp_statistics();
    check(await(snapshot).fec.data_packets_tx == 0, "ARQ-only client does not add FEC frames");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP rejects FEC mismatch and malformed negotiation without allocating sessions")
{
    arknet::rudp_server server;
    check(server.start("127.0.0.1", 0), "ARQ-only server starts");
    arknet::rudp_client client;
    client.set_fec_options(arknet::fec::options::fixed()).set_auto_reconnect(false).set_connect_timeout(1s);
    check(!client.start("127.0.0.1", server.get_listen_port()), "disabled peer rejects FEC request");
    check(arknet::get_last_error() == asio::error::operation_not_supported, "FEC mismatch has explicit error");
    check(server.get_session_count() == 0, "rejected FEC handshake does not create session");
    client.stop();
    server.stop();
    server.set_fec_options(arknet::fec::options::fixed());
    check(server.start("127.0.0.1", 0), "FEC server starts");
    asio::io_context context;
    asio::ip::udp::socket peer(context, asio::ip::udp::v4());
    peer.connect(server.acceptor().local_endpoint());
    for (const auto invalid_version : {true, false})
    {
        auto request = arknet::detail::rudp_fec_control(4, 321, 0, invalid_version ? 1200 : 50);
        if (invalid_version)
            request[5] = 2;
        peer.send(asio::buffer(request));
        std::array<char, 64> buffer{};
        const auto size = peer.receive(asio::buffer(buffer));
        check(arknet::detail::rudp_is_fec_control({buffer.data(), size}, 6), "unsupported version or MTU rejected");
    }
    check(server.get_session_count() == 0, "malformed FEC handshakes allocate no state");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP FEC refreshes liveness only for matching negotiated handshake")
{
    arknet::rudp_server server;
    server.set_fec_options(arknet::fec::options::fixed()).set_max_sessions(1);
    std::shared_ptr<arknet::rudp_session> accepted;
    std::promise<void> connected;
    auto connection = connected.get_future();
    server.bind_connect(
        [&](auto& session)
        {
            accepted = session;
            connected.set_value();
        });
    check(server.start("127.0.0.1", 0), "bounded FEC server starts");
    asio::io_context context;
    asio::ip::udp::socket peer(context, asio::ip::udp::v4());
    peer.connect(server.acceptor().local_endpoint());
    const auto request = arknet::detail::rudp_fec_control(4, 123, 0, 1400);
    peer.send(asio::buffer(request));
    std::array<char, 64> storage{};
    const auto receive_ack = [&]
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            const auto size = peer.receive(asio::buffer(storage));
            const std::string_view bytes(storage.data(), size);
            if (arknet::detail::rudp_is_fec_control(bytes, 5))
                return true;
            check(bytes.starts_with("AFEC"), "only FEC feedback may precede repeated ACK");
        }
        return false;
    };
    for (unsigned i = 0; i < 2; ++i)
        check(receive_ack(), "initial and repeated SYN return FEC ACK");
    await(connection);
    auto initial = accepted->post([&] { return accepted->last_alive_time(); }, asio::use_future);
    const auto initial_alive = await(initial);
    auto invalid_version = request;
    invalid_version[5] = 2;
    const std::array<std::string, 4> invalid_requests{
        invalid_version, arknet::detail::rudp_fec_control(4, 124, 0, 1400),
        arknet::detail::rudp_fec_control(4, 123, 0, 1399), arknet::detail::rudp_control(1, 123)};
    for (const auto& bytes : invalid_requests)
        peer.send(asio::buffer(bytes));
    auto rejected = accepted->post([&] { return accepted->last_alive_time(); }, 50ms, asio::use_future);
    check(await(rejected) == initial_alive, "stale or inconsistent handshake does not keep session alive");
    arknet::rudp_client second;
    second.set_fec_options(arknet::fec::options::fixed()).set_auto_reconnect(false).set_connect_timeout(50ms);
    check(!second.start("127.0.0.1", server.get_listen_port()), "FEC admission refuses another session when full");
    second.stop();
    check(server.get_session_count() == 1, "FEC admission respects aggregate session limit");
    peer.send(asio::buffer(request));
    check(receive_ack(), "existing session retries handshake while full");
    auto alive = accepted->post([&] { return accepted->last_alive_time(); }, asio::use_future);
    check(await(alive) > initial_alive, "matching original offer refreshes session liveness");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP rejects invalid FEC ACK version conversation and MTU")
{
    for (const unsigned invalid_field : {0U, 1U, 2U, 3U})
    {
        DOCTEST_INFO(invalid_field);
        asio::io_context context;
        asio::ip::udp::socket peer(context, {asio::ip::make_address("127.0.0.1"), 0});
        asio::ip::udp::endpoint remote;
        std::array<char, 64> request{};
        std::shared_ptr<std::string> response;
        bool recognized{};
        peer.async_receive_from(
            asio::buffer(request), remote,
            [&](arknet::error_code error, std::size_t count)
            {
                if (error || !arknet::detail::rudp_is_fec_control({request.data(), count}, 4))
                    return;
                recognized = true;
                response = std::make_shared<std::string>(arknet::detail::rudp_fec_control(
                    5, arknet::detail::rudp_read_id(request.data() + 1), invalid_field == 1 ? 0 : 123,
                    invalid_field == 2   ? 2000
                    : invalid_field == 3 ? 50
                                         : 1200));
                if (invalid_field == 0)
                    (*response)[9] = 2;
                peer.async_send_to(asio::buffer(*response), remote, [](arknet::error_code, std::size_t) {});
            });
        std::thread worker([&] { context.run(); });
        arknet::rudp_client client;
        client.set_fec_options(arknet::fec::options::fixed()).set_auto_reconnect(false).set_connect_timeout(1s);
        const auto started = client.start("127.0.0.1", peer.local_endpoint().port());
        const auto error = arknet::get_last_error();
        context.stop();
        worker.join();
        check(recognized, "fake peer received FEC negotiation");
        check(!started && error == asio::error::invalid_argument, "invalid ACK fails immediately");
        client.stop();
    }
}

DOCTEST_TEST_CASE("RUDP FEC configuration validates limits before start")
{
    arknet::rudp_client client;
    arknet::rudp_server server;
    DOCTEST_CHECK_THROWS_AS(client.get_rudp_statistics(), std::logic_error);
    auto invalid = arknet::fec::options::fixed();
    invalid.data_shards = 0;
    DOCTEST_CHECK_THROWS_AS(client.set_fec_options(invalid), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(server.set_fec_options(invalid), std::invalid_argument);
    arknet::arq::options too_small;
    too_small.mtu = 50;
    client.set_rudp_options(too_small);
    DOCTEST_CHECK_THROWS_AS(client.set_fec_options(arknet::fec::options::fixed()), std::invalid_argument);
    server.set_fec_options(arknet::fec::options::fixed());
    DOCTEST_CHECK_THROWS_AS(server.set_rudp_options(too_small), std::invalid_argument);
    check(client.get_fec_options().mode == arknet::fec::policy::disabled, "failed configuration keeps old policy");
    check(server.get_rudp_options().mtu == 1200, "failed configuration keeps old MTU");
}

DOCTEST_TEST_CASE("RUDP FEC safely stops built-in client session and server inside receive callbacks")
{
    for (const unsigned stop_target : {0U, 1U, 2U})
    {
        DOCTEST_INFO(stop_target);
        messages disconnected;
        arknet::rudp_server server;
        server.set_fec_options(arknet::fec::options::fixed());
        server.bind_recv(
            [&](auto& session, std::string_view bytes)
            {
                if (stop_target == 1)
                    session->stop();
                else if (stop_target == 2)
                    server.stop();
                else
                    session->async_send(bytes);
            });
        arknet::rudp_client client;
        client.set_fec_options(arknet::fec::options::fixed()).set_auto_reconnect(false);
        client.bind_recv([&](std::string_view) { client.stop(); });
        client.bind_disconnect([&] { disconnected.push("disconnected"); });
        check(server.start("127.0.0.1", 0), "built-in callback-stop server starts");
        check(client.start("127.0.0.1", server.get_listen_port()), "built-in callback-stop negotiation succeeds");
        check(client.async_send(std::string(16384, 'x')), "built-in callback-stop fragmented message admitted");
        disconnected.wait(1);
        client.stop();
        server.stop();
        check(client.is_stopped() && server.is_stopped(), "built-in callback shutdown finishes");
        check(server.get_session_count() == 0, "built-in callback shutdown releases sessions");
        DOCTEST_CHECK_THROWS_AS(client.get_rudp_statistics(), std::logic_error);
    }
}

DOCTEST_TEST_CASE("RUDP FEC safely stops client session and server with pending sends and statistics")
{
    for (const auto* model : {"shared", "sharded"})
        for (const auto mode : {arknet::fec::policy::fixed, arknet::fec::policy::adaptive})
            for (const unsigned stop_target : {0U, 1U, 2U})
            {
                DOCTEST_INFO(std::string_view(model));
                DOCTEST_INFO(static_cast<int>(mode));
                DOCTEST_INFO(stop_target);
                benchmark_runtime runtime(model, 4);
                arknet::arq::options arq;
                arq.congestion_control = false;
                auto server_fec = arknet::fec::options::fixed();
                server_fec.group_wait_ms = 100;
                auto client_fec = server_fec;
                client_fec.mode = mode;
                struct shutdown_result
                {
                    arknet::error_code error;
                    std::size_t bytes;
                    std::future<arknet::rudp::statistics> snapshot;
                };
                std::promise<shutdown_result> stopped;
                auto completion = stopped.get_future();
                messages disconnected;
                std::atomic<unsigned> disconnect_count{};
                arknet::rudp_server server(runtime.server_lanes(4));
                server.set_rudp_options(arq).set_fec_options(server_fec);
                server.bind_recv(
                    [&](auto& session, std::string_view bytes)
                    {
                        if (stop_target == 0)
                            session->async_send(bytes);
                        else
                            session->async_send("partial group",
                                                [&, session](arknet::error_code error, std::size_t count)
                                                {
                                                    stopped.set_value({error, count, session->get_rudp_statistics()});
                                                    if (stop_target == 1)
                                                        session->stop();
                                                    else
                                                        server.stop();
                                                });
                    });
                arknet::rudp_client client(runtime.context_at(2));
                client.set_rudp_options(arq).set_fec_options(client_fec).set_auto_reconnect(false);
                client.bind_recv(
                    [&](std::string_view)
                    {
                        if (stop_target == 0)
                            client.async_send("partial group",
                                              [&](arknet::error_code error, std::size_t count)
                                              {
                                                  stopped.set_value({error, count, client.get_rudp_statistics()});
                                                  client.stop();
                                              });
                    });
                client.bind_disconnect(
                    [&]
                    {
                        ++disconnect_count;
                        disconnected.push("disconnected");
                    });
                check(server.start("127.0.0.1", 0), "callback-stop server starts");
                fec_relay relay(server.acceptor().local_endpoint());
                check(client.start("127.0.0.1", relay.port()), "callback-stop FEC negotiation succeeds");
                check(client.async_send(std::string(16384, 'x')), "fragmented callback-stop message admitted");
                auto result = await(completion);
                check(!result.error && result.bytes == 13, "small send completes before partial-group shutdown");
                disconnected.wait(1);
                client.stop();
                server.stop();
                check(result.snapshot.wait_for(5s) == std::future_status::ready,
                      "statistics requested before callback shutdown complete");
                try
                {
                    result.snapshot.get();
                }
                catch (const std::future_error&)
                {
                    check(false, "statistics callback retains its promise through shutdown");
                }
                catch (const std::logic_error&)
                {
                }
                check(client.is_stopped() && server.is_stopped(), "receive callback shutdown finishes");
                check(server.get_session_count() == 0 && disconnect_count == 1,
                      "callback shutdown releases sessions and disconnects once");
                DOCTEST_CHECK_THROWS_AS(client.get_rudp_statistics(), std::logic_error);
            }
}

DOCTEST_TEST_CASE("RUDP FEC serializes concurrent sends and statistics on shared and sharded IO")
{
    for (const auto* model : {"shared", "sharded"})
        for (const auto mode : {arknet::fec::policy::fixed, arknet::fec::policy::adaptive})
        {
            DOCTEST_INFO(std::string_view(model));
            DOCTEST_INFO(static_cast<int>(mode));
            benchmark_runtime runtime(model, 4);
            arknet::arq::options arq;
            arq.congestion_control = false;
            // Keep the relay's injected loss independent of sanitizer-induced socket overflow.
            arq.send_window = 16;
            const auto server_fec = arknet::fec::options::fixed();
            auto client_fec = server_fec;
            client_fec.mode = mode;
            arknet::rudp_server server(runtime.server_lanes(4));
            server.set_rudp_options(arq).set_fec_options(server_fec);
            server.bind_init([&]
                             { server.acceptor().set_option(asio::socket_base::receive_buffer_size(1024 * 1024)); });
            server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
            std::atomic<int> server_disconnect_error{};
            server.bind_disconnect([&](auto&) { server_disconnect_error.store(arknet::get_last_error().value()); });
            check(server.start("127.0.0.1", 0), "parallel FEC server starts");
            std::array<messages, 4> received;
            std::array<std::atomic<unsigned>, 4> received_count{};
            std::array<std::atomic<int>, 4> disconnect_error{};
            std::array<std::promise<void>, 4> delivered;
            std::array<std::future<void>, 4> deliveries;
            std::array<std::unique_ptr<fec_relay>, 4> relays;
            std::array<std::unique_ptr<arknet::rudp_client>, 4> clients;
            for (unsigned i = 0; i < clients.size(); ++i)
            {
                relays[i] = std::make_unique<fec_relay>(server.acceptor().local_endpoint());
                clients[i] = std::make_unique<arknet::rudp_client>(runtime.context_at(i + 1));
                clients[i]->set_rudp_options(arq).set_fec_options(client_fec).set_auto_reconnect(false);
                clients[i]->bind_init(
                    [&, i] { clients[i]->socket().set_option(asio::socket_base::receive_buffer_size(1024 * 1024)); });
                deliveries[i] = delivered[i].get_future();
                clients[i]->bind_recv(
                    [&, i](std::string_view bytes)
                    {
                        received[i].push(bytes);
                        if (++received_count[i] == 32)
                            delivered[i].set_value();
                    });
                clients[i]->bind_disconnect([&, i] { disconnect_error[i].store(arknet::get_last_error().value()); });
                check(clients[i]->start("127.0.0.1", relays[i]->port()), "parallel FEC client negotiates");
            }
            using send_result = std::pair<arknet::error_code, std::size_t>;
            std::array<std::vector<std::future<send_result>>, 4> sends;
            std::array<std::vector<std::future<arknet::rudp::statistics>>, 4> snapshots;
            std::array<std::exception_ptr, 4> errors;
            std::array<std::array<std::vector<std::string>, 4>, 4> expected;
            for (unsigned client = 0; client < clients.size(); ++client)
                for (unsigned writer = 0; writer < sends.size(); ++writer)
                    for (unsigned sequence = 0; sequence < 8; ++sequence)
                    {
                        auto bytes = std::to_string(writer) + ':' + std::to_string(client) + ':' +
                                     std::to_string(sequence) + ':';
                        bytes.resize(sequence % 2 ? 2048 : 4119, static_cast<char>(writer));
                        expected[client][writer].push_back(std::move(bytes));
                    }
            std::barrier start(4);
            std::array<std::thread, 4> writers;
            for (unsigned writer = 0; writer < writers.size(); ++writer)
                writers[writer] = std::thread(
                    [&, writer]
                    {
                        start.arrive_and_wait();
                        try
                        {
                            for (unsigned sequence = 0; sequence < 8; ++sequence)
                                for (unsigned client = 0; client < clients.size(); ++client)
                                {
                                    sends[writer].push_back(clients[client]->async_send(
                                        expected[client][writer][sequence], asio::use_future));
                                    snapshots[writer].push_back(clients[client]->get_rudp_statistics());
                                }
                        }
                        catch (...)
                        {
                            errors[writer] = std::current_exception();
                        }
                    });
            for (auto& writer : writers)
                writer.join();
            for (unsigned writer = 0; writer < writers.size(); ++writer)
            {
                check(!errors[writer], "concurrent endpoint admission and statistics queries do not throw");
                for (unsigned i = 0; i < sends[writer].size(); ++i)
                {
                    const auto result = await(sends[writer][i]);
                    check(!result.first && result.second == expected[i % 4][writer][i / 4].size(),
                          "every concurrent send completes with its exact message size");
                    const auto stats = await(snapshots[writer][i]);
                    check(stats.fec.retained_bytes <= client_fec.max_buffer_bytes,
                          "concurrent snapshots respect the configured FEC memory bound");
                }
            }
            for (unsigned client = 0; client < clients.size(); ++client)
            {
                const auto complete = deliveries[client].wait_for(5s) == std::future_status::ready;
                if (!complete)
                {
                    const auto snapshot = [](auto& endpoint)
                    {
                        if (endpoint.is_started())
                        {
                            try
                            {
                                auto future = endpoint.get_rudp_statistics();
                                if (future.wait_for(1s) == std::future_status::ready)
                                    return future.get();
                            }
                            catch (const std::logic_error&)
                            {
                            }
                        }
                        return arknet::rudp::statistics{};
                    };
                    const auto client_stats = snapshot(*clients[client]);
                    const auto session = server.find_session(
                        asio::ip::udp::endpoint(server.acceptor().local_endpoint().address(), relays[client]->port()));
                    const auto server_stats = session ? snapshot(*session) : arknet::rudp::statistics{};
                    asio::socket_base::receive_buffer_size client_buffer, server_buffer;
                    clients[client]->socket().get_option(client_buffer);
                    server.acceptor().get_option(server_buffer);
                    std::cerr << "FEC timeout: model=" << model << " policy=" << static_cast<int>(mode)
                              << " client=" << client << " received=" << received_count[client].load()
                              << "/32 started=" << clients[client]->is_started()
                              << " disconnect=" << disconnect_error[client].load()
                              << " server_disconnect=" << server_disconnect_error.load()
                              << " client_pending=" << client_stats.arq_pending_bytes
                              << " client_retries=" << client_stats.retransmissions
                              << " client_raw_queue=" << client_stats.raw_queued_bytes
                              << " client_recovered=" << client_stats.fec.recovered_shards
                              << " server_pending=" << server_stats.arq_pending_bytes
                              << " server_retries=" << server_stats.retransmissions
                              << " server_raw_queue=" << server_stats.raw_queued_bytes
                              << " server_recovered=" << server_stats.fec.recovered_shards
                              << " relay_dropped=" << relays[client]->dropped.load()
                              << " client_rcvbuf=" << client_buffer.value()
                              << " server_rcvbuf=" << server_buffer.value()
                              << " relay_rcvbuf=" << relays[client]->receive_buffer_size() << '\n';
                }
                check(complete, "all concurrent messages arrive before the receive deadline");
                const auto actual = received[client].wait(32);
                check(actual.size() == 32, "concurrent senders deliver each complete message once");
                for (unsigned writer = 0; writer < writers.size(); ++writer)
                {
                    std::vector<std::string> ordered;
                    const auto prefix = std::to_string(writer) + ':';
                    for (const auto& bytes : actual)
                        if (bytes.starts_with(prefix))
                            ordered.push_back(bytes);
                    check(ordered == expected[client][writer], "each writer preserves message content and order");
                }
                auto snapshot = clients[client]->get_rudp_statistics();
                check(await(snapshot).fec.recovered_shards > 0, "concurrent FEC traffic repairs missing datagrams");
                check(relays[client]->dropped > 0 && relays[client]->duplicated > 0 && relays[client]->reordered > 0,
                      "concurrent recovery executes loss duplication and reordering");
                check(relays[client]->largest <= arq.mtu, "concurrent FEC packets respect negotiated wire MTU");
                clients[client]->stop();
            }
            server.stop();
            check(server.get_session_count() == 0, "parallel FEC shutdown releases all sessions");
        }
}

DOCTEST_TEST_CASE("RUDP fixed FEC recovers missing datagrams with duplicates and reordered parity")
{
    arknet::arq::options arq;
    arq.congestion_control = false;
    arknet::rudp_server server;
    server.set_rudp_options(arq).set_fec_options(arknet::fec::options::fixed());
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    check(server.start("127.0.0.1", 0), "loss-test server starts");
    fec_relay relay(server.acceptor().local_endpoint());
    messages received;
    std::atomic<int> failure{};
    arknet::rudp_client client;
    client.set_rudp_options(arq).set_fec_options(arknet::fec::options::fixed()).set_auto_reconnect(false);
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    client.bind_disconnect([&] { failure.store(arknet::get_last_error().value()); });
    check(client.start("127.0.0.1", relay.port()), "FEC handshake passes relay");
    std::vector<std::string> expected;
    for (unsigned i = 0; i < 24; ++i)
    {
        auto bytes = std::to_string(i) + ':';
        bytes.resize(i % 2 ? 4119 : 16384, static_cast<char>(i));
        expected.push_back(bytes);
        check(client.async_send(bytes), "weak-network message admitted");
    }
    auto settle = client.post([] {}, 1s, asio::use_future);
    await(settle);
    auto snapshot = client.get_rudp_statistics();
    const auto stats = await(snapshot);
    DOCTEST_INFO("transmissions=" << stats.transmissions << " retransmissions=" << stats.retransmissions
                                  << " recovered=" << stats.fec.recovered_shards << " invalid="
                                  << stats.fec.invalid_packets << " retained=" << stats.fec.retained_bytes
                                  << " dropped=" << relay.dropped.load() << " failure=" << failure.load());
    check(received.wait(expected.size()) == expected, "FEC and ARQ preserve exact ordered delivery");
    check(stats.fec.recovered_shards > 0, "Reed-Solomon restored missing wire datagrams");
    check(relay.dropped > 0 && relay.duplicated > 0 && relay.reordered > 0, "all relay impairments executed");
    check(relay.largest <= arq.mtu, "FEC overhead stays within negotiated wire MTU");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("RUDP FEC renegotiates after automatic reconnect")
{
    arknet::rudp_server server;
    server.set_fec_options(arknet::fec::options::fixed());
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    check(server.start("127.0.0.1", 0), "reconnect server starts");
    const auto port = server.get_listen_port();
    messages received, connections, disconnects;
    arknet::rudp_client client;
    client.set_fec_options(arknet::fec::options::adaptive()).set_auto_reconnect(true, 50ms).set_connect_timeout(200ms);
    client.bind_connect(
        [&]
        {
            if (!arknet::get_last_error())
                connections.push("connected");
        });
    client.bind_disconnect([&] { disconnects.push("disconnected"); });
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    check(client.start("127.0.0.1", port), "initial FEC conversation starts");
    client.async_send("before restart");
    check(received.wait(1).front() == "before restart", "initial FEC session echoes");
    server.stop();
    disconnects.wait(1);
    check(server.start("127.0.0.1", port), "FEC server restarts");
    connections.wait(2);
    client.async_send("after restart");
    check(received.wait(2).back() == "after restart", "renegotiated FEC session echoes");
    auto snapshot = client.get_rudp_statistics();
    check(await(snapshot).fec.data_packets_tx > 0, "reconnected session still uses negotiated FEC");
    client.stop();
    server.stop();
}
