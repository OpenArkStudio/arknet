// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(ARKNET_INTEROP_LEGACY)
#include <zephyr/udp/udp_client.hpp>
#include <zephyr/udp/udp_server.hpp>
#else
#include <arknet/rudp/rudp_client.hpp>
#include <arknet/rudp/rudp_server.hpp>
#include "impairment.hpp"
#endif

namespace
{
using namespace std::chrono_literals;
#if defined(ARKNET_INTEROP_LEGACY)
namespace network = zephyr;
using client_type = zephyr::udp_client;
using server_type = zephyr::udp_server;
constexpr const char* provider = "zephyr";
#else
namespace network = arknet;
using client_type = arknet::rudp_client;
using server_type = arknet::rudp_server;
constexpr const char* provider = "arknet";
#endif
constexpr std::string_view close_request = "arknet-interop-close-server";
enum class scenario
{
    baseline,
    reconnect,
    impaired
};
const char* scenario_name(scenario value)
{
    return value == scenario::baseline ? "baseline" : value == scenario::reconnect ? "reconnect" : "impaired";
}

void require(bool condition, const std::string& reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}
template <class Integer> void append_native(std::string& bytes, Integer value)
{
    bytes.append(reinterpret_cast<const char*>(&value), sizeof value);
}
std::vector<std::string> application_messages()
{
    constexpr std::array<std::uint32_t, 8> body_sizes{0, 1, 23, 1024, 4096, 4096, 511, 4096};
    std::vector<std::string> result;
    for (std::uint16_t sequence = 0; sequence < 32; ++sequence)
    {
        const auto body_size = body_sizes[sequence % body_sizes.size()];
        std::string bytes;
        // ARK's packed CS header is 7 bytes; SS adds actor/source/destination (16 bytes).
        append_native(bytes, static_cast<std::uint16_t>(0x4000 + sequence));
        append_native(bytes, body_size);
        append_native(bytes, std::uint8_t{0});
        if (sequence % 2)
        {
            append_native(bytes, std::int64_t{0x0102030405060708});
            append_native(bytes, std::int32_t{0x12345678});
            append_native(bytes, std::int32_t{0x76543210});
        }
        for (std::uint32_t i = 0; i < body_size; ++i)
            bytes.push_back(static_cast<char>((i + sequence * 17) % 256));
        result.push_back(std::move(bytes));
    }
    return result;
}
struct peer_state
{
    explicit peer_state(const std::vector<std::string>& messages) : expected(messages) {}
    const std::vector<std::string>& expected;
    std::mutex mutex;
    std::condition_variable ready;
    std::size_t received{}, total_received{};
    unsigned disconnects{};
    std::vector<network::error_code> disconnect_errors;
    bool server_close_requested{};
    std::function<void()> close_session;
    std::string failure;

    bool accept(std::string_view bytes)
    {
        std::lock_guard lock(mutex);
        if (received >= expected.size() || bytes != expected[received])
            failure = "message content/order mismatch at index " + std::to_string(received);
        else
        {
            ++received;
            ++total_received;
        }
        ready.notify_all();
        return failure.empty();
    }
    void fail(std::string reason)
    {
        std::lock_guard lock(mutex);
        failure = std::move(reason);
        ready.notify_all();
    }
    void disconnect(network::error_code error, bool reset_received = false)
    {
        std::lock_guard lock(mutex);
        if (reset_received)
        {
            if (received != expected.size())
                failure = "server disconnected before receiving every message exactly once";
            received = 0;
            server_close_requested = false;
        }
        ++disconnects;
        disconnect_errors.push_back(error);
        ready.notify_all();
    }
    template <class Predicate> void wait(Predicate predicate, const char* reason, std::chrono::seconds timeout = 5s)
    {
        std::unique_lock lock(mutex);
        const auto arrived = ready.wait_for(lock, timeout, [&] { return !failure.empty() || predicate(); });
        require(arrived, std::string(reason) + ": received=" + std::to_string(received) +
                             " disconnects=" + std::to_string(disconnects));
        require(failure.empty(), failure);
    }
};
template <class Endpoint> bool send_async(Endpoint& endpoint, std::string_view bytes)
{
#if defined(ARKNET_INTEROP_LEGACY)
    return endpoint.send(bytes);
#else
    return endpoint.async_send(bytes);
#endif
}
template <class Endpoint> bool start(Endpoint& endpoint, unsigned short port)
{
#if defined(ARKNET_INTEROP_LEGACY)
    return endpoint.start("127.0.0.1", port, zephyr::use_kcp);
#else
    return endpoint.start("127.0.0.1", port);
#endif
}
#if !defined(ARKNET_INTEROP_LEGACY)
template <class Endpoint> void configure_arq(Endpoint& endpoint)
{
    arknet::arq::options options;
    options.congestion_control = false;
    endpoint.set_rudp_options(options);
}
void verify_impairment(std::unique_ptr<arknet_interop::data_impairment>& relay)
{
    if (!relay)
        return;
    require(relay->wait_fin(5s), "relay did not forward FIN before shutdown");
    relay->stop();
    const auto stats = relay->snapshot();
    std::cerr << "ARQ data impairment: drops=" << stats.dropped << " reordered=" << stats.reordered
              << " errors=" << stats.errors << " fin_forwarded=" << stats.fin_forwarded << '\n';
    require(stats.dropped != 0 && stats.reordered != 0 && stats.errors == 0,
            "relay did not inject loss/reorder without socket errors");
}
#endif

void run_client(bool server_closes, scenario test)
{
    unsigned port{};
    require(static_cast<bool>(std::cin >> port) && port > 0 && port <= 65535, "server did not publish a valid port");
    const auto expected = application_messages();
    peer_state state(expected);
    client_type client(1536, 1024 * 1024);
    const auto cycles = test == scenario::reconnect ? 2U : 1U;
#if defined(ARKNET_INTEROP_LEGACY)
    client.auto_reconnect(false).connect_timeout(5s);
    client.bind_disconnect([&](network::error_code error) { state.disconnect(error); });
#else
    configure_arq(client);
    client.set_auto_reconnect(false).set_connect_timeout(5s);
    client.bind_disconnect([&] { state.disconnect(network::get_last_error()); });
    std::unique_ptr<arknet_interop::data_impairment> relay;
    if (test == scenario::impaired)
    {
        relay = std::make_unique<arknet_interop::data_impairment>(static_cast<unsigned short>(port));
        port = relay->port();
    }
#endif
    client.bind_init([&] { client.socket().set_option(asio::socket_base::receive_buffer_size(1024 * 1024)); });
    client.bind_recv([&](std::string_view bytes) { state.accept(bytes); });
    for (unsigned cycle = 0; cycle < cycles; ++cycle)
    {
        const auto started = start(client, static_cast<unsigned short>(port));
        require(started, std::string("client handshake failed: ") + network::get_last_error().message());
        for (std::size_t offset = 0; offset < expected.size(); offset += 4)
        {
            const auto end = std::min(offset + 4, expected.size());
            for (auto i = offset; i < end; ++i)
                require(send_async(client, expected[i]), "client rejected an application message");
            state.wait([&] { return state.received >= end; }, "echo deadline exceeded", 10s);
        }
        if (server_closes)
        {
            require(send_async(client, close_request), "client rejected the server-close request");
            state.wait([&] { return state.disconnects >= cycle + 1; }, "server FIN deadline exceeded");
        }
        client.stop();
        state.wait([&] { return state.disconnects >= cycle + 1; }, "client shutdown deadline exceeded");
        std::lock_guard lock(state.mutex);
        require(state.received == expected.size(), "client did not receive every echo exactly once");
        require(state.disconnects == cycle + 1, "client must disconnect exactly once per session");
        if (server_closes)
            require(state.disconnect_errors.back() == asio::error::eof,
                    "client did not observe the server FIN: " + state.disconnect_errors.back().message());
        require(client.is_stopped(), "client did not finish shutdown");
        if (cycle + 1 < cycles)
            state.received = 0;
    }
    require(state.total_received == expected.size() * cycles, "client session message total mismatch");
#if !defined(ARKNET_INTEROP_LEGACY)
    verify_impairment(relay);
#endif
    std::cout << provider << " client: " << scenario_name(test) << ", " << cycles
              << " sessions, 32 ordered messages/session, " << (server_closes ? "server" : "client")
              << " close passed\n";
}

void run_server(bool server_closes, scenario test)
{
    const auto expected = application_messages();
    peer_state state(expected);
    std::atomic<unsigned> connections{};
    const auto cycles = test == scenario::reconnect ? 2U : 1U;
#if defined(ARKNET_INTEROP_LEGACY)
    server_type server(15000, 1536, 1024 * 1024);
#else
    server_type server(1536, 1024 * 1024, 2);
    configure_arq(server);
#endif
    server.bind_init([&] { server.acceptor().set_option(asio::socket_base::receive_buffer_size(1024 * 1024)); });
    server.bind_connect(
        [&](auto& session)
        {
            ++connections;
            std::lock_guard lock(state.mutex);
            state.close_session = [weak = std::weak_ptr{session}]
            {
                if (auto current = weak.lock())
                    current->stop();
            };
        });
    server.bind_disconnect([&](auto&) { state.disconnect(network::get_last_error(), true); });
    server.bind_recv(
        [&](auto& session, std::string_view bytes)
        {
            if (bytes == close_request)
            {
                std::lock_guard lock(state.mutex);
                if (!server_closes || state.received != expected.size() || state.server_close_requested)
                    state.failure = "unexpected server-close request";
                else
                    state.server_close_requested = true;
                state.ready.notify_all();
            }
            else if (state.accept(bytes) && !send_async(*session, bytes))
                state.fail("server rejected an echo");
        });
    const auto started = start(server, 0);
    require(started, std::string("server start failed: ") + network::get_last_error().message());
    const auto port = server.acceptor().local_endpoint().port();
#if !defined(ARKNET_INTEROP_LEGACY)
    std::unique_ptr<arknet_interop::data_impairment> relay;
    if (test == scenario::impaired)
        relay = std::make_unique<arknet_interop::data_impairment>(port);
    std::cout << (relay ? relay->port() : port) << std::endl;
#else
    std::cout << port << std::endl;
#endif
    for (unsigned cycle = 0; cycle < cycles; ++cycle)
    {
        state.wait([&] { return state.disconnects >= cycle + 1 || state.server_close_requested; },
                   "server deadline exceeded", 30s);
        std::function<void()> close;
        {
            std::lock_guard lock(state.mutex);
            if (state.server_close_requested)
                close = state.close_session;
        }
        if (close)
            close();
        state.wait([&] { return state.disconnects >= cycle + 1; }, "server session shutdown deadline exceeded");
    }
    server.stop();
    {
        std::lock_guard lock(state.mutex);
        require(state.received == 0 && state.total_received == expected.size() * cycles,
                "server did not receive every message exactly once in each session");
        require(state.disconnects == cycles, "server must disconnect exactly once per session");
        if (!server_closes)
            for (const auto& error : state.disconnect_errors)
                require(error == asio::error::eof, "server did not observe the client FIN");
        state.close_session = {};
    }
    require(server.is_stopped() && connections == cycles, "server did not close every complete session");
#if defined(ARKNET_INTEROP_LEGACY)
    require(server.session_count() == 0, "legacy server retained a stopped session");
#else
    require(server.get_session_count() == 0, "arknet server retained a stopped session");
    verify_impairment(relay);
#endif
    std::cerr << provider << " server: " << scenario_name(test) << ", " << cycles
              << " sessions, 32 ordered messages/session, " << (server_closes ? "server" : "client")
              << " close passed\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 3 || argc == 4,
                "usage: rudp_peer <client|server> <client|server close initiator> [baseline|reconnect|impaired]");
        const std::string_view role(argv[1]), closer(argv[2]);
        require(closer == "client" || closer == "server", "close initiator must be client or server");
        const std::string_view name = argc == 4 ? argv[3] : "baseline";
        require(name == "baseline" || name == "reconnect" || name == "impaired", "unknown test scenario");
        const auto test = name == "baseline"    ? scenario::baseline
                          : name == "reconnect" ? scenario::reconnect
                                                : scenario::impaired;
        if (role == "client")
            run_client(closer == "server", test);
        else
        {
            require(role == "server", "role must be client or server");
            run_server(closer == "server", test);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << provider << " interoperability failure: " << error.what() << '\n';
        return 1;
    }
}
