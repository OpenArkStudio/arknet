#include <arknet/arknet.hpp>
#include "check.hpp"

namespace
{
using request = arknet::http::request<arknet::http::string_body>;
using response = arknet::http::response<arknet::http::string_body>;

request make_request(arknet::http::verb method, std::string body = {})
{
    request value(method, "/echo", 11);
    value.set(arknet::http::field::host, "localhost");
    value.body() = std::move(body);
    value.prepare_payload();
    return value;
}

template <class Server> void bind_echo(Server& server)
{
    server.bind_recv(
        [](auto& session, request& incoming)
        {
            response outgoing(arknet::http::status::ok, incoming.version());
            outgoing.keep_alive(incoming.keep_alive());
            outgoing.body() = incoming.body();
            outgoing.prepare_payload();
            session->async_send(std::move(outgoing));
        });
}

template <class Server, class Client> void pipeline_echo(Server& server, Client& client)
{
    messages received;
    bind_echo(server);
    check(server.start("127.0.0.1", 0), "HTTP server start");
    client.set_auto_reconnect(false);
    client.bind_recv([&](response& value) { received.push(value.body()); });
    check(client.start("localhost", server.get_listen_port()), "HTTP client start");
    auto first = make_request(arknet::http::verb::post, "owned");
    auto sent = client.async_send(first, asio::use_future);
    first.body() = "changed after admission";
    client.async_send(make_request(arknet::http::verb::post, std::string("second\0binary", 13)));
    client.async_send(make_request(arknet::http::verb::get, "third"));
    const auto values = received.wait(3);
    check(values[0] == "owned", "request body is owned");
    check(values[1] == std::string("second\0binary", 13), "binary body and pipeline order");
    check(values[2] == "third", "keep-alive pipeline");
    const auto [ec, bytes] = await(sent);
    check(!ec && bytes > 5, "HTTP completion reports wire bytes");
    client.stop();
    server.stop();
}

struct raw_peer
{
    asio::io_context io;
    asio::ip::tcp::socket socket{io};
    asio::streambuf buffer;
    explicit raw_peer(std::uint16_t port) { socket.connect({asio::ip::make_address("127.0.0.1"), port}); }
    void write(std::string_view value) { asio::write(socket, asio::buffer(value)); }
    void close()
    {
        arknet::error_code ec;
        socket.close(ec);
    }
    response read()
    {
        response value;
        arknet::http::read(socket, buffer, value);
        return value;
    }
};
}

DOCTEST_TEST_CASE("HTTP owns requests and preserves keep-alive pipeline order")
{
    arknet::http_server server;
    arknet::http_client client;
    pipeline_echo(server, client);
}

DOCTEST_TEST_CASE("HTTP works with an external io_context and a custom session")
{
    struct custom_session : arknet::http_session_t<custom_session>
    {
        using http_session_t::http_session_t;
    };
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    std::thread worker([&] { io.run(); });
    struct joiner
    {
        asio::io_context& io;
        std::thread& worker;
        ~joiner()
        {
            io.stop();
            worker.join();
        }
    } cleanup{io, worker};
    arknet::http_server_t<custom_session> server(io);
    arknet::http_client client(io);
    pipeline_echo(server, client);
}

DOCTEST_TEST_CASE("HTTP parses chunked requests and responses")
{
    arknet::http_server server;
    server.bind_recv(
        [](auto& session, request& incoming)
        {
            response outgoing(arknet::http::status::ok, 11);
            outgoing.body() = incoming.body();
            outgoing.chunked(true);
            session->async_send(std::move(outgoing));
        });
    check(server.start("127.0.0.1", 0), "chunked server start");
    raw_peer peer(server.get_listen_port());
    peer.write("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
               "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n");
    check(peer.read().body() == "abcde", "chunked wire interoperability");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP client skips HEAD bodies while keeping the following response")
{
    messages received;
    arknet::tcp_server server;
    std::atomic<unsigned> writes{};
    server.bind_recv(
        [&](auto& session, std::string_view)
        {
            if (writes.fetch_add(1) == 0)
                session->async_send(
                    std::string("HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n"
                                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nget\r\n0\r\n\r\n"));
        });
    check(server.start("127.0.0.1", 0), "HEAD raw server start");
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](response& value) { received.push(value.body()); });
    check(client.start("127.0.0.1", server.get_listen_port()), "HEAD client start");
    client.async_send(make_request(arknet::http::verb::head));
    client.async_send(make_request(arknet::http::verb::get));
    const auto values = received.wait(2);
    check(values[0].empty() && values[1] == "get", "HEAD has no body despite Content-Length");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP server sends an empty HEAD response with representation length")
{
    messages received;
    arknet::http_server server;
    server.bind_recv(
        [](auto& session, request&)
        {
            arknet::http::response<arknet::http::empty_body> outgoing(arknet::http::status::ok, 11);
            outgoing.content_length(1234);
            session->async_send(std::move(outgoing));
        });
    check(server.start("127.0.0.1", 0), "HEAD server start");
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv(
        [&](response& value)
        {
            check(value[arknet::http::field::content_length] == "1234", "HEAD representation length");
            received.push(value.body());
        });
    check(client.start("127.0.0.1", server.get_listen_port()), "HEAD connect");
    client.async_send(make_request(arknet::http::verb::head));
    check(received.wait(1).front().empty(), "HEAD response contains no wire body");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP sends 100 Continue before waiting for a request body")
{
    arknet::http_server server;
    bind_echo(server);
    check(server.start("127.0.0.1", 0), "Expect server start");
    raw_peer peer(server.get_listen_port());
    peer.write("POST / HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: 4\r\n\r\n");
    check(peer.read().result() == arknet::http::status::continue_, "interim response precedes body");
    peer.write("body");
    check(peer.read().body() == "body", "body after continue");
    peer.close();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP rejects missing or duplicate Host and unsupported expectations")
{
    for (const auto& [headers, expected] : std::vector<std::pair<std::string, unsigned>>{
             {"", 400}, {"Host: a\r\nHost: b\r\n", 400}, {"Host: localhost\r\nExpect: unsupported\r\n", 417}})
    {
        arknet::http_server server;
        std::atomic<unsigned> calls{};
        server.bind_recv([&](auto&, request&) { ++calls; });
        check(server.start("127.0.0.1", 0), "validation server start");
        raw_peer peer(server.get_listen_port());
        peer.write("GET / HTTP/1.1\r\n" + headers + "\r\n");
        const auto value = peer.read();
        check(value.result_int() == expected && !value.keep_alive(), "rejection status and close");
        check(calls == 0, "invalid request never reaches listener");
        server.stop();
    }
}

DOCTEST_TEST_CASE("HTTP parser rejects ambiguous framing and configured limits")
{
    for (const auto& wire :
         {"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx",
          "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
          "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 33\r\n\r\n",
          "GET / HTTP/1.1\r\nHost: localhost\r\nX-Long: "
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r"
          "\n"})
    {
        arknet::http_server server;
        server.set_http_body_limit(32).set_http_header_limit(128);
        std::promise<arknet::error_code> disconnected;
        auto done = disconnected.get_future();
        std::atomic<unsigned> calls{};
        server.bind_recv([&](auto&, request&) { ++calls; });
        server.bind_disconnect([&](auto&) { disconnected.set_value(arknet::get_last_error()); });
        check(server.start("127.0.0.1", 0), "limits server start");
        raw_peer peer(server.get_listen_port());
        peer.write(wire);
        check(bool(await(done)), "invalid framing disconnects with an error");
        check(calls == 0, "rejected request never reaches listener");
        server.stop();
    }
}

DOCTEST_TEST_CASE("HTTP client applies response limits")
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view)
                     { session->async_send(std::string("HTTP/1.1 200 OK\r\nContent-Length: 33\r\n\r\n")); });
    check(server.start("127.0.0.1", 0), "response limit raw server start");
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.set_http_body_limit(32);
    messages errors;
    client.bind_disconnect([&] { errors.push(arknet::get_last_error().message()); });
    check(client.start("127.0.0.1", server.get_listen_port()), "response limit connect");
    client.async_send(make_request(arknet::http::verb::get));
    check(!errors.wait(1).front().empty(), "oversized response disconnects");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP informational responses preserve HEAD association")
{
    arknet::tcp_server server;
    server.bind_recv(
        [](auto& session, std::string_view)
        {
            session->async_send(std::string("HTTP/1.1 103 Early Hints\r\n\r\n"
                                            "HTTP/1.1 200 OK\r\nContent-Length: 55\r\n\r\n"));
        });
    check(server.start("127.0.0.1", 0), "informational raw server start");
    messages received;
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](response& value) { received.push(std::to_string(value.result_int())); });
    check(client.start("127.0.0.1", server.get_listen_port()), "informational connect");
    client.async_send(make_request(arknet::http::verb::head));
    const auto values = received.wait(2);
    check(values[0] == "103" && values[1] == "200", "interim status does not pop HEAD method");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP close completes its response and rejects later queued writes")
{
    messages completions;
    messages received;
    messages disconnected;
    arknet::http_server server;
    server.bind_recv(
        [&](auto& session, request& incoming)
        {
            response outgoing(arknet::http::status::ok, incoming.version());
            outgoing.body() = "last";
            outgoing.keep_alive(false);
            outgoing.prepare_payload();
            session->async_send(outgoing, [&](const arknet::error_code& ec, std::size_t bytes)
                                { completions.push(!ec && bytes > 4 ? "closed" : "failed"); });
            session->async_send(outgoing, [&](const arknet::error_code& ec, std::size_t bytes)
                                { completions.push(ec && bytes == 0 ? "rejected" : "unexpected"); });
        });
    check(server.start("127.0.0.1", 0), "close server start");
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](response& value) { received.push(value.body()); });
    client.bind_disconnect([&] { disconnected.push("done"); });
    check(client.start("127.0.0.1", server.get_listen_port()), "close client start");
    auto last = make_request(arknet::http::verb::get);
    last.keep_alive(false);
    client.async_send(std::move(last));
    check(received.wait(1).front() == "last", "last response is delivered before disconnect");
    auto completed = completions.wait(2);
    check(std::count(completed.begin(), completed.end(), "closed") == 1 &&
              std::count(completed.begin(), completed.end(), "rejected") == 1,
          "each send completes exactly once");
    disconnected.wait(1);
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("HTTP bounded send rejects large bodies and recovers")
{
    arknet::http_server server;
    bind_echo(server);
    check(server.start("127.0.0.1", 0), "bounded server start");
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.set_max_send_buffer_size(512);
    messages received;
    client.bind_recv([&](response& value) { received.push(value.body()); });
    check(client.start("127.0.0.1", server.get_listen_port()), "bounded connect");
    auto rejected = client.async_send(make_request(arknet::http::verb::post, std::string(1024, 'x')), asio::use_future);
    auto [ec, bytes] = await(rejected);
    check(ec == asio::error::no_buffer_space && bytes == 0, "bounded admission error");
    client.async_send(make_request(arknet::http::verb::post, "recovery"));
    check(received.wait(1).front() == "recovery", "bounded rejection leaves connection usable");
    client.stop();
    server.stop();
}

#ifdef ARKNET_ENABLE_SSL
DOCTEST_TEST_CASE("HTTPS verifies identity and preserves HTTP pipeline behavior")
{
    const std::string certs = ARKNET_TEST_CERT_DIR;
    arknet::https_server server;
    server.set_cert_file("", certs + "/server.pem", certs + "/server-key.pem", "");
    arknet::https_client client;
    client.load_verify_file(certs + "/ca.pem");
    pipeline_echo(server, client);
}

DOCTEST_TEST_CASE("HTTPS rejects untrusted and mismatched server certificates")
{
    const std::string certs = ARKNET_TEST_CERT_DIR;
    for (const auto* certificate : {"server.pem", "wrong.pem"})
    {
        arknet::https_server server;
        server.set_cert_file("", certs + "/" + certificate, certs + "/server-key.pem", "");
        check(server.start("127.0.0.1", 0), "HTTPS rejection server start");
        arknet::https_client client;
        client.set_auto_reconnect(false);
        if (std::string_view(certificate) == "wrong.pem")
            client.load_verify_file(certs + "/ca.pem");
        check(!client.start("localhost", server.get_listen_port()), "HTTPS certificate rejection");
        check(arknet::get_last_error() != asio::error::timed_out, "certificate rejection is immediate");
        client.stop();
        server.stop();
    }
}

DOCTEST_TEST_CASE("HTTPS supports mutual TLS")
{
    const std::string certs = ARKNET_TEST_CERT_DIR;
    arknet::https_server server;
    server.set_cert_file("", certs + "/server.pem", certs + "/server-key.pem", "");
    server.load_verify_file(certs + "/ca.pem");
    server.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
    arknet::https_client client;
    client.set_cert_file(certs + "/ca.pem", certs + "/client.pem", certs + "/client-key.pem", "");
    pipeline_echo(server, client);
}
#endif
