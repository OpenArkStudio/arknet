#include <arknet/arknet.hpp>
#include "check.hpp"

#ifndef ARKNET_TEST_CERT_DIR
#error "ARKNET_TEST_CERT_DIR must name the test certificate directory"
#endif

namespace
{
const std::string certs = ARKNET_TEST_CERT_DIR;

template <class Server> void identity(Server& server)
{
    server.set_cert_file("", certs + "/server.pem", certs + "/server-key.pem", "");
    check(!arknet::get_last_error(), "server certificate loading");
}

template <class Client> void trust(Client& client)
{
    client.set_auto_reconnect(false);
    client.set_connect_timeout(10s);
    client.load_verify_file(certs + "/ca.pem");
    check(!arknet::get_last_error(), "CA certificate loading");
}

template <class Client>
void verified_connect(Client& client, const std::string& host, std::uint16_t port, const char* message)
{
    const auto started = std::chrono::steady_clock::now();
    const bool connected = client.start(host, port);
    const auto error = arknet::get_last_error();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << message << " (" << host << "): " << elapsed << " ms\n";
    DOCTEST_REQUIRE_MESSAGE(connected, message, " (", host, "): ", error.category().name(), ":", error.value(), " ",
                            error.message());
}

template <class Server, class Client> void secure_echo(const std::string& host)
{
    messages received;
    messages server_names;
    Server server;
    identity(server);
    server.bind_recv(
        [&](auto& session, std::string_view data)
        {
            const char* name = SSL_get_servername(session->ssl_stream().native_handle(), TLSEXT_NAMETYPE_host_name);
            server_names.push(name ? name : "");
            session->async_send(data);
        });
    check(server.start("127.0.0.1", 0), "secure server start");
    Client client;
    trust(client);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    verified_connect(client, host, server.get_listen_port(), "verified TLS connect");
    check(SSL_version(client.ssl_stream().native_handle()) >= TLS1_2_VERSION, "minimum TLS version");
    client.async_send(std::string("secure"));
    check(received.wait(1).front() == "secure", "secure echo");
    check(server_names.wait(1).front() == (host == "localhost" ? host : ""), "DNS-only SNI");
    client.stop();
    server.stop();
}

template <class Server, class Client> void reject_untrusted()
{
    Server server;
    identity(server);
    check(server.start("127.0.0.1", 0), "secure server start");
    std::atomic<long> verification{X509_V_OK};
    Client client;
    client.set_auto_reconnect(false);
    client.set_connect_timeout(10s);
    client.bind_handshake([&] { verification = SSL_get_verify_result(client.ssl_stream().native_handle()); });
    check(!client.start("localhost", server.get_listen_port()), "untrusted server must fail");
    const auto error = arknet::get_last_error();
    check(error != asio::error::timed_out && error != asio::error::operation_aborted,
          "untrusted server rejection must not be a timeout or cancellation");
    check(verification.load() != X509_V_OK, "untrusted server certificate verification must fail");
    client.stop();
    server.stop();
}

template <class Server, class Client> void reject_identity(const std::string& host)
{
    Server server;
    server.set_cert_file("", certs + "/wrong.pem", certs + "/server-key.pem", "");
    check(!arknet::get_last_error(), "wrong certificate fixture loading");
    check(server.start("127.0.0.1", 0), "wrong identity server start");
    std::atomic<long> verification{X509_V_OK};
    Client client;
    trust(client);
    client.bind_handshake([&] { verification = SSL_get_verify_result(client.ssl_stream().native_handle()); });
    check(!client.start(host, server.get_listen_port()), "DNS/IP identity mismatch must fail");
    const auto error = arknet::get_last_error();
    check(error != asio::error::timed_out && error != asio::error::operation_aborted,
          "identity mismatch rejection must not be a timeout or cancellation");
    check(verification.load() == (host == "localhost" ? X509_V_ERR_HOSTNAME_MISMATCH : X509_V_ERR_IP_ADDRESS_MISMATCH),
          "DNS/IP identity verification error");
    client.stop();
    server.stop();
}

template <class Server> void reject_missing_identity()
{
    Server server;
    check(!server.start("127.0.0.1", 0), "server without identity must fail");
    server.stop();
}

template <class Server> void reject_mismatched_key()
{
    Server server;
    server.set_cert_file("", certs + "/server.pem", certs + "/client-key.pem", "");
    check(!server.start("127.0.0.1", 0), "mismatched certificate/key must fail");
    server.stop();
}

template <class Server> void require_client_certificate(Server& server)
{
    identity(server);
    server.load_verify_file(certs + "/ca.pem");
    check(!arknet::get_last_error(), "server CA certificate loading");
    server.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
    check(server.start("127.0.0.1", 0), "mTLS server start");
}

template <class Server, class Client> void mutual_echo()
{
    messages received;
    Server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    require_client_certificate(server);
    Client client;
    trust(client);
    client.set_cert_file(certs + "/ca.pem", certs + "/client.pem", certs + "/client-key.pem", "");
    check(!arknet::get_last_error(), "client certificate loading");
    client.bind_recv([&](std::string_view data) { received.push(data); });
    verified_connect(client, "localhost", server.get_listen_port(), "mTLS client connect");
    client.async_send(std::string("mutual"));
    check(received.wait(1).front() == "mutual", "mTLS echo");
    client.stop();
    server.stop();
}

template <class Server, class Client> void reject_missing_client_certificate()
{
    Server server;
    require_client_certificate(server);
    std::promise<void> refused;
    auto refused_result = refused.get_future();
    Client client;
    trust(client);
    client.bind_disconnect([&] { refused.set_value(); });
    // TLS 1.3 may finish the client handshake before the server's certificate-required alert.
    if (client.start("localhost", server.get_listen_port()))
        await(refused_result);
    else
    {
        const auto error = arknet::get_last_error();
        check(error != asio::error::timed_out && error != asio::error::operation_aborted,
              "mTLS rejection must not be a timeout or cancellation");
    }
    client.stop();
    server.stop();
}
}

DOCTEST_TEST_CASE("TLS TCP verifies DNS identity and sends SNI")
{
    secure_echo<arknet::tcps_server, arknet::tcps_client>("localhost");
}

DOCTEST_TEST_CASE("TLS TCP verifies IP identity without sending SNI")
{
    secure_echo<arknet::tcps_server, arknet::tcps_client>("127.0.0.1");
}

DOCTEST_TEST_CASE("TLS WebSocket verifies DNS identity and sends SNI")
{
    secure_echo<arknet::wss_server, arknet::wss_client>("localhost");
}

DOCTEST_TEST_CASE("TLS WebSocket verifies IP identity without sending SNI")
{
    secure_echo<arknet::wss_server, arknet::wss_client>("127.0.0.1");
}

DOCTEST_TEST_CASE("TLS TCP rejects an untrusted server certificate")
{
    reject_untrusted<arknet::tcps_server, arknet::tcps_client>();
}

DOCTEST_TEST_CASE("TLS WebSocket rejects an untrusted server certificate")
{
    reject_untrusted<arknet::wss_server, arknet::wss_client>();
}

DOCTEST_TEST_CASE("TLS TCP rejects a DNS identity mismatch")
{
    reject_identity<arknet::tcps_server, arknet::tcps_client>("localhost");
}

DOCTEST_TEST_CASE("TLS TCP rejects an IP identity mismatch")
{
    reject_identity<arknet::tcps_server, arknet::tcps_client>("127.0.0.1");
}

DOCTEST_TEST_CASE("TLS WebSocket rejects a DNS identity mismatch")
{
    reject_identity<arknet::wss_server, arknet::wss_client>("localhost");
}

DOCTEST_TEST_CASE("TLS WebSocket rejects an IP identity mismatch")
{
    reject_identity<arknet::wss_server, arknet::wss_client>("127.0.0.1");
}

DOCTEST_TEST_CASE("TLS TCP rejects a server without an identity")
{
    reject_missing_identity<arknet::tcps_server>();
}

DOCTEST_TEST_CASE("TLS WebSocket rejects a server without an identity")
{
    reject_missing_identity<arknet::wss_server>();
}

DOCTEST_TEST_CASE("TLS TCP rejects a mismatched certificate and private key")
{
    reject_mismatched_key<arknet::tcps_server>();
}

DOCTEST_TEST_CASE("TLS WebSocket rejects a mismatched certificate and private key")
{
    reject_mismatched_key<arknet::wss_server>();
}

DOCTEST_TEST_CASE("mTLS TCP accepts a trusted client certificate")
{
    mutual_echo<arknet::tcps_server, arknet::tcps_client>();
}

DOCTEST_TEST_CASE("mTLS WebSocket accepts a trusted client certificate")
{
    mutual_echo<arknet::wss_server, arknet::wss_client>();
}

DOCTEST_TEST_CASE("mTLS TCP rejects a client without a certificate")
{
    reject_missing_client_certificate<arknet::tcps_server, arknet::tcps_client>();
}

DOCTEST_TEST_CASE("mTLS WebSocket rejects a client without a certificate")
{
    reject_missing_client_certificate<arknet::wss_server, arknet::wss_client>();
}

template <class Server, class Client> void secure_reconnect()
{
    messages connected;
    messages received;
    Server server;
    identity(server);
    server.bind_recv([](auto& session, std::string_view payload) { session->async_send(payload); });
    check(server.start("127.0.0.1", 0), "reconnect TLS server start");
    const auto port = server.get_listen_port();
    Client client;
    trust(client);
    client.set_auto_reconnect(true, 20ms);
    client.bind_connect(
        [&]
        {
            if (!arknet::get_last_error())
                connected.push("connected");
        });
    client.bind_recv([&](std::string_view payload) { received.push(payload); });
    check(client.start("localhost", port), "reconnect TLS first connection");
    client.async_send("before");
    check(received.wait(1).front() == "before", "first TLS echo");
    server.stop();
    check(server.start("127.0.0.1", port), "reconnect TLS server restart");
    connected.wait(2);
    client.async_send("after");
    check(received.wait(2).back() == "after", "fresh TLS stream after automatic reconnect");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TLS TCP recreates its stream on automatic reconnect")
{
    secure_reconnect<arknet::tcps_server, arknet::tcps_client>();
}

DOCTEST_TEST_CASE("TLS WebSocket recreates both protocol streams on automatic reconnect")
{
    secure_reconnect<arknet::wss_server, arknet::wss_client>();
}
