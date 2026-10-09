#include <arknet/http/ws_client.hpp>
#include <arknet/http/ws_server.hpp>
#include "check.hpp"

class custom_ws_client : public arknet::ws_client_t<custom_ws_client>
{
public:
    using arknet::ws_client_t<custom_ws_client>::ws_client_t;
};

class custom_ws_session : public arknet::ws_session_t<custom_ws_session>
{
public:
    using arknet::ws_session_t<custom_ws_session>::ws_session_t;
};

void websocket_echo(bool binary)
{
    messages received;
    std::atomic<bool> received_binary{};
    arknet::ws_server server;
    server.bind_recv(
        [](auto& session, std::string_view data)
        {
            session->ws_stream().binary(session->ws_stream().got_binary());
            session->async_send(data);
        });
    check(server.start("127.0.0.1", 0), "WebSocket server start");
    arknet::ws_client client;
    client.set_auto_reconnect(false);
    client.bind_connect([&] { client.ws_stream().binary(binary); });
    client.bind_recv(
        [&](std::string_view data)
        {
            received_binary = client.ws_stream().got_binary();
            received.push(data);
        });
    check(client.start("127.0.0.1", server.get_listen_port(), "/echo"), "WebSocket upgrade");
    const std::string data = binary ? std::string("a\0b", 3) : "hello websocket";
    client.async_send(data);
    check(received.wait(1).front() == data, "WebSocket payload");
    check(received_binary == binary, "WebSocket binary/text mode");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("WebSocket text frames preserve payload and frame mode")
{
    websocket_echo(false);
}

DOCTEST_TEST_CASE("WebSocket binary frames preserve embedded null bytes and frame mode")
{
    websocket_echo(true);
}

DOCTEST_TEST_CASE("WebSocket custom endpoints preserve upgrade decorators and target")
{
    messages requests;
    messages received;
    arknet::ws_server_t<custom_ws_session> server;
    server.bind_accept(
        [](auto& session)
        {
            session->ws_stream().set_option(websocket::stream_base::decorator(
                [](websocket::response_type& response) { response.set(http::field::server, "arknet-custom"); }));
        });
    server.bind_upgrade(
        [&](auto& session)
        {
            if (!arknet::get_last_error())
                requests.push(session->get_upgrade_request().target());
        });
    server.bind_recv([](auto& session, std::string_view payload) { session->async_send(payload); });
    check(server.start("127.0.0.1", 0), "custom WebSocket server start");
    custom_ws_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view payload) { received.push(payload); });
    check(client.start("127.0.0.1", server.get_listen_port(), "/custom?value=1"), "custom WebSocket client start");
    check(requests.wait(1).front() == "/custom?value=1", "custom upgrade target");
    check(client.get_upgrade_response()[http::field::server] == "arknet-custom", "custom upgrade decorator");
    client.async_send("custom session");
    check(received.wait(1).front() == "custom session", "custom session echo");
    client.stop();
    server.stop();
}
