#include <arknet/arknet.hpp>
#include <arknet/http/router.hpp>
#include "check.hpp"

#include <atomic>

namespace
{
const std::string payload("ipv6\0payload", 12);

#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
const std::string certificates = std::string(ARKNET_TEST_CERT_DIR) + "/ipv6";

template <class Server, class Client> void configure_identity(Server& server, Client& client)
{
    server.set_cert_file("", certificates + "/server.pem", certificates + "/server-key.pem", "");
    check(!arknet::get_last_error(), "IPv6 server certificate loads");
    client.load_verify_file(certificates + "/ca.pem");
    check(!arknet::get_last_error(), "IPv6 trust anchor loads");
}

template <class Client> void verified_ip(Client& client)
{
    check(SSL_get_verify_result(client.ssl_stream().native_handle()) == X509_V_OK,
          "IPv6 certificate IP identity is verified");
}
#endif

template <class Server, class Client> void stream_echo()
{
    messages received;
    messages peer_addresses;
    messages server_names;
    Server server(1536, 16 * 1024 * 1024, 2);
    Client client;
    client.set_auto_reconnect(false);
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
        configure_identity(server, client);
#endif
    server.bind_recv(
        [&](auto& session, std::string_view data)
        {
            peer_addresses.push(session->get_remote_address());
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
            if constexpr (requires { session->ssl_stream(); })
            {
                const char* name = SSL_get_servername(session->ssl_stream().native_handle(), TLSEXT_NAMETYPE_host_name);
                server_names.push(name ? name : "");
            }
#endif
            session->async_send(data);
        });
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(server.start("::1", 0, asio::transfer_exactly(payload.size())), "IPv6 stream server binds");
    check(server.acceptor().local_endpoint().address().is_v6(), "stream listener uses IPv6");
    check(client.start("::1", server.get_listen_port(), asio::transfer_exactly(payload.size())),
          "IPv6 stream client resolves and connects");
    check(client.socket().remote_endpoint().address().is_v6() && client.socket().local_endpoint().address().is_v6(),
          "stream client endpoints use IPv6");
    auto sent = client.async_send(payload, asio::use_future);
    const auto completion = await(sent);
    check(!completion.first && completion.second == payload.size(), "IPv6 stream send completes");
    check(received.wait(1).front() == payload, "IPv6 stream preserves binary data");
    check(peer_addresses.wait(1).front() == "::1", "IPv6 stream session preserves peer address");
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
    {
        verified_ip(client);
        check(server_names.wait(1).front().empty(), "literal IPv6 address does not send DNS SNI");
    }
#endif
    client.stop();
    server.stop();
}

template <class Server, class Client> void websocket_echo(const char* host)
{
    messages received;
    messages hosts;
    messages targets;
    Server server(1536, 16 * 1024 * 1024, 2);
    Client client;
    client.set_auto_reconnect(false);
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
        configure_identity(server, client);
#endif
    server.bind_upgrade(
        [&](auto& session)
        {
            if (!arknet::get_last_error())
            {
                const auto& request = session->get_upgrade_request();
                hosts.push(request[arknet::http::field::host]);
                targets.push(request.target());
            }
        });
    server.bind_recv(
        [](auto& session, std::string_view data)
        {
            session->ws_stream().binary(session->ws_stream().got_binary());
            session->async_send(data);
        });
    client.bind_connect([&] { client.ws_stream().binary(true); });
    std::atomic<bool> binary{};
    client.bind_recv(
        [&](std::string_view data)
        {
            binary = client.ws_stream().got_binary();
            received.push(data);
        });
    check(server.start(host, 0), "WebSocket address binds");
    check(client.start(host, server.get_listen_port(), "/echo?address=ip"), "WebSocket address upgrades");
    const std::string authority = (std::string_view(host) == "::1" ? "[::1]" : host) + std::string(":") +
                                  std::to_string(server.get_listen_port());
    check(hosts.wait(1).front() == authority, "WebSocket Host brackets IPv6 and includes the actual port");
    check(targets.wait(1).front() == "/echo?address=ip", "WebSocket preserves upgrade target");
    auto sent = client.async_send(payload, asio::use_future);
    check(!await(sent).first, "WebSocket binary send completes");
    check(received.wait(1).front() == payload && binary, "WebSocket preserves binary frame and payload");
    check(client.socket().remote_endpoint().address().is_v6() == (std::string_view(host) == "::1"),
          "WebSocket connects using the requested address family");
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
        verified_ip(client);
#endif
    client.stop();
    server.stop();
}

template <class Server, class Client> void http_echo()
{
    messages received;
    messages statuses;
    auto router = std::make_shared<arknet::http_router>();
    router->add(arknet::http::verb::post, "/echo/:id",
                [](const auto& context, auto& reply)
                {
                    reply.body() = context.request.body() + "|" + std::string(context.param("id")) + "|" +
                                   std::string(context.request[arknet::http::field::host]) + "|" +
                                   std::string(context.query);
                });
    Server server(1536, 16 * 1024 * 1024, 2);
    Client client;
    client.set_auto_reconnect(false);
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
        configure_identity(server, client);
#endif
    arknet::bind_router(server, router);
    client.bind_recv(
        [&](arknet::http_route_response& response)
        {
            statuses.push(std::to_string(response.result_int()));
            received.push(response.body());
        });
    check(server.start("::1", 0), "IPv6 HTTP server binds");
    check(client.start("::1", server.get_listen_port()), "IPv6 HTTP client connects");
    const std::string authority = "[::1]:" + std::to_string(server.get_listen_port());
    arknet::http_route_request request(arknet::http::verb::post, "/echo/a%20b?q=one", 11);
    request.set(arknet::http::field::host, authority);
    request.body() = payload;
    request.prepare_payload();
    auto sent = client.async_send(std::move(request), asio::use_future);
    check(!await(sent).first, "IPv6 HTTP request send completes");
    check(received.wait(1).front() == payload + "|a b|" + authority + "|q=one",
          "IPv6 HTTP routing preserves binary body, bracketed Host and query");
    check(statuses.wait(1).front() == "200", "IPv6 HTTP route returns success");
    check(client.socket().remote_endpoint().address().is_v6() && server.acceptor().local_endpoint().address().is_v6(),
          "HTTP transport uses IPv6");
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
    if constexpr (requires { client.ssl_stream(); })
        verified_ip(client);
#endif
    client.stop();
    server.stop();
}
}

DOCTEST_TEST_CASE("IPv6 endpoint conversion preserves address family and port")
{
    const auto tcp = arknet::to_endpoint<asio::ip::tcp::endpoint>("::1", 32123);
    check(!arknet::get_last_error() && tcp.address().is_v6() && tcp.port() == 32123,
          "TCP endpoint helper resolves a literal IPv6 address");
    const auto udp = arknet::to_endpoint<asio::ip::udp::endpoint>("::1", 32124);
    check(!arknet::get_last_error() && udp.address().is_v6() && udp.port() == 32124,
          "UDP endpoint helper resolves a literal IPv6 address");
}

DOCTEST_TEST_CASE("TCP echoes binary data over IPv6 loopback")
{
    stream_echo<arknet::tcp_server, arknet::tcp_client>();
}

DOCTEST_TEST_CASE("UDP sessions echo empty and binary IPv6 datagrams")
{
    messages received;
    messages peers;
    messages address_errors;
    arknet::udp_server server;
    arknet::udp_client client;
    client.set_auto_reconnect(false);
    server.bind_recv(
        [&](auto& session, std::string_view data)
        {
            arknet::set_last_error(asio::error::host_not_found);
            peers.push(session->get_remote_address());
            address_errors.push(arknet::get_last_error() ? "error" : "clear");
            session->async_send(data);
        });
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(server.start("::1", 0), "IPv6 UDP server binds");
    check(client.start("::1", server.get_listen_port()), "IPv6 UDP client resolves and connects");
    check(server.acceptor().local_endpoint().address().is_v6() && client.socket().remote_endpoint().address().is_v6(),
          "UDP sockets use IPv6");
    const std::vector<std::string> expected{"", payload};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        auto sent = client.async_send(expected[index], asio::use_future);
        const auto completion = await(sent);
        check(!completion.first && completion.second == expected[index].size(), "IPv6 UDP send completes");
        check(received.wait(index + 1)[index] == expected[index], "IPv6 UDP preserves datagram content");
    }
    const std::vector<std::string> expected_peers{"::1", "::1"};
    check(peers.wait(2) == expected_peers && server.get_session_count() == 1, "IPv6 UDP reuses the same peer session");
    const std::vector<std::string> expected_errors{"clear", "clear"};
    check(address_errors.wait(2) == expected_errors, "UDP address access clears stale errors on success");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("UDP cast resolves IPv6 destinations and replies to IPv6 endpoints")
{
    messages received;
    messages peers;
    arknet::udp_cast receiver;
    arknet::udp_cast sender;
    receiver.bind_recv(
        [&](auto& endpoint, std::string_view data)
        {
            peers.push(endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));
            receiver.async_send(endpoint, data);
        });
    sender.bind_recv(
        [&](auto& endpoint, std::string_view data)
        {
            peers.push(endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));
            received.push(data);
        });
    check(receiver.start("::1", 0) && sender.start("::1", 0), "IPv6 cast sockets bind");
    auto resolved = sender.async_send("::1", receiver.get_local_port(), payload, asio::use_future);
    const auto completion = await(resolved);
    check(!completion.first && completion.second == payload.size(), "IPv6 cast resolver send completes");
    check(received.wait(1).front() == payload, "IPv6 cast preserves binary datagram");
    const auto endpoints = peers.wait(2);
    check(endpoints[0] == "::1:" + std::to_string(sender.get_local_port()) &&
              endpoints[1] == "::1:" + std::to_string(receiver.get_local_port()),
          "IPv6 cast preserves both peer endpoints");
    const asio::ip::udp::endpoint target(asio::ip::make_address("::1"), receiver.get_local_port());
    auto direct = sender.async_send(target, std::string{}, asio::use_future);
    const auto empty = await(direct);
    check(!empty.first && empty.second == 0, "IPv6 cast direct endpoint sends an empty datagram");
    check(received.wait(2)[1].empty(), "IPv6 cast echoes an empty datagram");
    sender.stop();
    receiver.stop();
}

DOCTEST_TEST_CASE("WebSocket IPv6 Host uses brackets and the connected port")
{
    websocket_echo<arknet::ws_server, arknet::ws_client>("::1");
}

DOCTEST_TEST_CASE("WebSocket IPv4 Host retains its address and connected port")
{
    websocket_echo<arknet::ws_server, arknet::ws_client>("127.0.0.1");
}

DOCTEST_TEST_CASE("HTTP routes requests over IPv6 with a bracketed Host")
{
    http_echo<arknet::http_server, arknet::http_client>();
}

#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
DOCTEST_TEST_CASE("TLS verifies IPv6 certificate identity and echoes binary data")
{
    stream_echo<arknet::tcps_server, arknet::tcps_client>();
}

DOCTEST_TEST_CASE("WSS verifies IPv6 identity and sends a bracketed Host")
{
    websocket_echo<arknet::wss_server, arknet::wss_client>("::1");
}

DOCTEST_TEST_CASE("HTTPS verifies IPv6 identity and preserves route behavior")
{
    http_echo<arknet::https_server, arknet::https_client>();
}

DOCTEST_TEST_CASE("TLS rejects an IPv4-only certificate for an IPv6 peer")
{
    arknet::tcps_server server(1536, 16 * 1024 * 1024, 2);
    const std::string existing = ARKNET_TEST_CERT_DIR;
    server.set_cert_file("", existing + "/server.pem", existing + "/server-key.pem", "");
    check(server.start("::1", 0), "IPv6 mismatch server binds");
    arknet::tcps_client client;
    client.set_auto_reconnect(false);
    client.load_verify_file(existing + "/ca.pem");
    check(!arknet::get_last_error(), "existing CA trust loads");
    std::atomic<long> verification{X509_V_OK};
    client.bind_handshake([&] { verification = SSL_get_verify_result(client.ssl_stream().native_handle()); });
    check(!client.start("::1", server.get_listen_port()), "IPv6 identity mismatch rejects connection");
    check(verification == X509_V_ERR_IP_ADDRESS_MISMATCH, "IPv6 mismatch fails IP identity verification");
    client.stop();
    server.stop();
}
#endif
