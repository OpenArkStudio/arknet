# WebSocket Usage

Use the `arknet::arknet` interface target in a C++20 application following the [build guide](../guide.md).
Plain WebSocket works with either Asio provider without OpenSSL. Enable TLS for
WSS and follow [TLS usage](../tls/usage.md) to configure trust and identity.

## Binary Echo Server

WebSocket has its own message framing; do not pass `use_dgram` or a TCP delimiter.
The server preserves the received message's text/binary type when echoing it.
Build the two blocks as separate executables using the `arknet::arknet` interface
target. Run the server first, then the client in another terminal. Press Enter in
the server terminal to stop it.

```cpp
#include <arknet/websocket/ws_server.hpp>
#include <iostream>
#include <string_view>

int main()
{
    arknet::ws_server server;
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->ws_stream().binary(session->ws_stream().got_binary());
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7001))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "WebSocket listening on ws://127.0.0.1:7001/echo\n";
    std::cin.get();
    server.stop();
}
```

## Binary Echo Client

The client upgrades at `/echo`, sends one binary message and checks both its
content and type. Its main thread waits for the reply; IO callbacks do not block.

```cpp
#include <arknet/websocket/ws_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

int main()
{
    std::promise<std::pair<std::string, bool>> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::ws_client client;
    client.set_auto_reconnect(false);
    client.bind_connect([&]
    {
        if (!arknet::get_last_error())
            client.ws_stream().binary(true);
    });
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value({std::string(bytes), client.ws_stream().got_binary()});
        }
    });
    if (!client.start("127.0.0.1", 7001, "/echo"))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("a\0b", 3)))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    bool matched = false;
    if (ready)
    {
        auto [bytes, binary] = received.get();
        matched = binary && bytes == std::string("a\0b", 3);
    }
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

For text messages, leave the default mode or set `ws_stream().text(true)` on
the IO lane before sending. Text payloads must be valid UTF-8. Use
`std::string{}` for an empty message and a sized string/view/buffer for binary
payloads. The receive callback runs once per assembled message, not per frame.

Client `async_start(host, port, target)` begins connecting and upgrading without
waiting. Its return reports initiation; `bind_upgrade` reports the upgrade
result, and a successful `bind_connect` indicates a usable endpoint. Read
`get_last_error()` in the relevant callback before sending.

## Secure WebSocket (WSS)

With `ARKNET_ENABLE_SSL=ON`, replace the server header/type with
`<arknet/websocket/wss_server.hpp>` / `arknet::wss_server`, and the client
header/type with `<arknet/websocket/wss_client.hpp>` / `arknet::wss_client`.
Load the server certificate/key and the client CA before start using the
[TLS server and client examples](../tls/usage.md). Connect with `localhost` when
using the test certificate, and keep the `/echo` target. Server start still takes
only the listening host and port; neither WSS endpoint uses `use_dgram`.

## Configure Upgrade Headers and Inspect the Result

Before starting the endpoints, register decorators in lifecycle callbacks so
they are reapplied to streams recreated by reconnect:

```cpp
client.bind_init([&]
{
    client.ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::request_type& request)
        {
            request.set(http::field::user_agent, "arknet-example");
        }));
});
server.bind_accept([](auto& session)
{
    session->ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::response_type& response)
        {
            response.set(http::field::server, "arknet-example");
        }));
});
server.bind_upgrade([](auto& session)
{
    if (!arknet::get_last_error())
        std::cout << session->get_upgrade_request().target() << '\n';
});
```

Use `client.start(host, port, "/path?key=value")` or set the default target with
`set_upgrade_target`. After successful start, `get_upgrade_response()` exposes
the HTTP upgrade response. Upgrade callbacks report completion, including
errors; they are not a pre-upgrade HTTP routing/authentication API. Define
origin, credential and subprotocol policy before treating a peer as authorized.

## Limits, Sessions and Shutdown

The constructor's receive maximum also becomes the native stream's maximum
message size; the default is 16 MiB. Configure message and send limits before
start. A send queue defaults to 16 MiB of payload and 1024 operations; adjust
its byte limit with `set_max_send_buffer_size`. Admission failure reports
`no_buffer_space`. `get_queued_send_buffer_size()` observes queued payload bytes.

`async_send` owns accepted payloads. Its boolean return is queue acceptance;
completion reports a local write, not application delivery. Completions accept
`(error_code, bytes)`, `(bytes)` or `()`, and `asio::use_future` returns the
error/byte pair in a future. An immediate failure may invoke completion on the
submitting thread. Accepted sends complete on the serialized lane. Copy received
views before retaining them; do not wait for send futures in IO callbacks.

Extend sessions with `ws_session_t<MySession>` and use
`ws_server_t<MySession>`. Extend clients with `ws_client_t<MyClient>`.
The same scheme is available through `wss_session_t`, `wss_server_t` and
`wss_client_t` when TLS is enabled. Configure state on its owning lane; multiple
external lanes may execute concurrently.

Automatic reconnect recreates the protocol stream and does not replay pending
business messages. Set `set_auto_reconnect(false)` when the application owns
retry policy. Configure connection and disconnect deadlines with
`set_connect_timeout` and `set_disconnect_timeout`.

From an IO callback, use `request_stop()` and wait later on the owner thread
with `wait_stopped()` or `stop()`. Shutdown attempts the close handshake and
then transport cleanup. Stop and destroy endpoints while any external context
still runs; only then stop its runners. See [IO models](../threading.md),
[tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)
and [performance](performance.md).
